"""Transport implementations for VeClient."""

import json
import socket
import struct
import threading
from abc import ABC, abstractmethod
from typing import Any, Dict, List, Optional, Callable

try:
    import requests
except ImportError:
    requests = None

try:
    import msgpack
except ImportError:
    msgpack = None


# ---------------------------------------------------------------------------
# Callback type for subscribe notifications
# ---------------------------------------------------------------------------
NotifyCallback = Callable[[str, Any], None]  # (path, value)


class Transport(ABC):
    """Abstract transport for VE Node operations."""

    @abstractmethod
    def get(self, path: str) -> Any:
        pass

    @abstractmethod
    def set(self, path: str, value: Any) -> bool:
        pass

    @abstractmethod
    def list(self, path: str) -> List[Dict]:
        pass

    @abstractmethod
    def tree(self, path: str) -> Dict:
        pass

    @abstractmethod
    def command(self, name: str, args: Optional[Dict]) -> Any:
        pass

    @abstractmethod
    def ping(self) -> bool:
        pass

    def subscribe(self, path: str, callback: NotifyCallback) -> Callable[[], None]:
        raise NotImplementedError("subscribe not supported on this transport")

    def unsubscribe(self, path: str) -> None:
        raise NotImplementedError("unsubscribe not supported on this transport")

    def trigger(self, path: str) -> bool:
        raise NotImplementedError("trigger not supported on this transport")


# ---------------------------------------------------------------------------
# HTTP REST
# ---------------------------------------------------------------------------


class HttpRestTransport(Transport):
    """REST API transport: GET/PUT/POST /api/node/*, GET /api/tree/*, POST /api/cmd/*"""

    def __init__(self, base_url: str, timeout: int = 30):
        if requests is None:
            raise ImportError("requests required: pip install requests")
        self.base = base_url.rstrip("/")
        self.timeout = timeout
        self.session = requests.Session()

    def _request(self, method: str, path: str, **kwargs) -> "requests.Response":
        resp = self.session.request(method, f"{self.base}{path}",
                                     timeout=self.timeout, **kwargs)
        resp.raise_for_status()
        return resp

    def get(self, path: str) -> Any:
        p = f"/{path.lstrip('/')}" if path and path != "/" else ""
        try:
            resp = self._request("GET", f"/api/node{p}")
            data = resp.json()
            return data.get("value")
        except requests.HTTPError as e:
            if e.response.status_code == 404:
                return None
            raise

    def set(self, path: str, value: Any) -> bool:
        p = f"/{path}" if path else ""
        resp = self._request("PUT", f"/api/node{p}",
                             json=value,
                             headers={"Content-Type": "application/json"})
        return resp.json().get("ok", False)

    def trigger(self, path: str) -> bool:
        """POST /api/node/{path} — fire NODE_CHANGED."""
        p = f"/{path}" if path else ""
        try:
            resp = self._request("POST", f"/api/node{p}")
            return resp.json().get("ok", False)
        except Exception:
            return False

    def list(self, path: str) -> List[Dict]:
        p = f"/{path}" if path and path != "/" else ""
        resp = self._request("GET", f"/api/children{p}")
        return resp.json()

    def tree(self, path: str) -> Dict:
        p = f"/{path}" if path and path != "/" else ""
        resp = self._request("GET", f"/api/tree{p}")
        return resp.json()

    def command(self, name: str, args: Optional[Dict] = None) -> Any:
        resp = self._request("POST", f"/api/cmd/{name}",
                             json=args or {},
                             headers={"Content-Type": "application/json"})
        data = resp.json()
        if "error" in data:
            raise RuntimeError(data["error"])
        return data.get("result")

    def ping(self) -> bool:
        try:
            resp = self._request("GET", "/health")
            return resp.json().get("status") == "ok"
        except Exception:
            return False


# ---------------------------------------------------------------------------
# JSON-RPC 2.0
# ---------------------------------------------------------------------------


class JsonRpcTransport(Transport):
    """JSON-RPC 2.0 transport: POST /jsonrpc"""

    def __init__(self, base_url: str, timeout: int = 30):
        if requests is None:
            raise ImportError("requests required: pip install requests")
        self.url = base_url.rstrip("/") + "/jsonrpc"
        self.timeout = timeout
        self._id = 0
        self.session = requests.Session()

    def _call(self, method: str, params: Optional[Dict] = None) -> Any:
        self._id += 1
        payload = {
            "jsonrpc": "2.0",
            "method": method,
            "params": params or {},
            "id": self._id
        }

        resp = self.session.post(self.url, json=payload, timeout=self.timeout,
                                 headers={"Content-Type": "application/json"})
        resp.raise_for_status()
        result = resp.json()

        if "error" in result:
            err = result["error"]
            raise RuntimeError(f"JSON-RPC error {err.get('code')}: {err.get('message')}")

        return result.get("result")

    def get(self, path: str) -> Any:
        data = self._call("node.get", {"path": path})
        if isinstance(data, dict) and not data.get("found", True):
            return None
        return data.get("value") if isinstance(data, dict) else data

    def set(self, path: str, value: Any) -> bool:
        data = self._call("node.set", {"path": path, "value": value})
        return data.get("success", False) if isinstance(data, dict) else False

    def trigger(self, path: str) -> bool:
        """JSON-RPC has no native trigger — fall back to set(get())."""
        cur = self.get(path)
        return self.set(path, cur)

    def list(self, path: str) -> List[Dict]:
        data = self._call("node.list", {"path": path})
        if isinstance(data, dict) and not data.get("found", True):
            return []
        return data.get("children", []) if isinstance(data, dict) else []

    def tree(self, path: str) -> Dict:
        return self._call("node.get", {"path": path})

    def command(self, name: str, args: Optional[Dict] = None) -> Any:
        data = self._call("command.run", {"name": name, "args": args or {}})
        return data.get("result") if isinstance(data, dict) else data

    def ping(self) -> bool:
        try:
            self._call("node.get", {"path": "/"})
            return True
        except Exception:
            return False


# ---------------------------------------------------------------------------
# TCP JSON (port 12200) — persistent connection, subscribe support
# ---------------------------------------------------------------------------


class TcpJsonTransport(Transport):
    """TCP JSON transport: pretty-printed JSON over persistent TCP.

    Supports subscribe/unsubscribe with real-time event push.
    Uses json.JSONDecoder.raw_decode to handle pretty-printed (multi-line) JSON.
    """

    def __init__(self, host: str, port: int = 12200, timeout: int = 30):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.sock = None
        self.recv_buf = ""
        self._id = 0
        self._lock = threading.Lock()
        self._pending = {}  # id -> (event, result_holder)
        self._subscriptions = {}  # path -> set of callbacks
        self._sub_lock = threading.Lock()
        self._recv_thread = None
        self._running = False
        self._connect()

    def _connect(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(self.timeout)
        self.sock.connect((self.host, self.port))
        self._running = True
        self._recv_thread = threading.Thread(target=self._recv_loop, daemon=True)
        self._recv_thread.start()

    def _recv_loop(self):
        while self._running:
            try:
                data = self.sock.recv(8192)
                if not data:
                    break
                self.recv_buf += data.decode('utf-8')
                self._process_messages()
            except Exception:
                break

    def _process_messages(self):
        """Parse complete JSON objects from buffer using raw_decode (handles pretty JSON)."""
        decoder = json.JSONDecoder()
        while self.recv_buf:
            self.recv_buf = self.recv_buf.lstrip()
            if not self.recv_buf:
                break
            try:
                msg, idx = decoder.raw_decode(self.recv_buf)
                self.recv_buf = self.recv_buf[idx:]
            except json.JSONDecodeError:
                break  # incomplete JSON, wait for more data

            msg_type = msg.get("type", "")
            msg_id = msg.get("id")

            # Event push from subscription
            if msg_type == "event":
                path = msg.get("path", "")
                value = msg.get("value")
                with self._sub_lock:
                    callbacks = list(self._subscriptions.get(path, set()))
                for cb in callbacks:
                    try:
                        cb(path, value)
                    except Exception:
                        pass
                continue

            # Response to a pending request
            if msg_id is not None and msg_id in self._pending:
                event, result_holder = self._pending.pop(msg_id)
                result_holder[0] = msg
                event.set()

    def _send(self, cmd: dict) -> dict:
        with self._lock:
            self._id += 1
            cmd["id"] = self._id
            event = threading.Event()
            result_holder = [None]
            self._pending[self._id] = (event, result_holder)

        try:
            self.sock.sendall((json.dumps(cmd, separators=(',', ':')) + "\n").encode('utf-8'))
            if not event.wait(timeout=self.timeout):
                raise TimeoutError("Request timeout")
            return result_holder[0]
        finally:
            self._pending.pop(cmd.get("id"), None)

    def get(self, path: str) -> Any:
        resp = self._send({"cmd": "get", "path": path})
        if resp.get("type") == "error":
            return None
        return resp.get("value")

    def set(self, path: str, value: Any) -> bool:
        resp = self._send({"cmd": "set", "path": path, "value": value})
        return resp.get("type") == "ok"

    def trigger(self, path: str) -> bool:
        """Trigger NODE_CHANGED on the node (set without value)."""
        resp = self._send({"cmd": "set", "path": path})
        return resp.get("type") == "ok"

    def list(self, path: str) -> List[Dict]:
        resp = self._send({"cmd": "list", "path": path})
        if resp.get("type") == "error":
            return []
        return resp.get("children", [])

    def tree(self, path: str) -> Dict:
        resp = self._send({"cmd": "get", "path": path})
        return resp.get("value", {})

    def command(self, name: str, args: Optional[Dict] = None) -> Any:
        body = {"cmd": "command.run", "name": name}
        if args is not None:
            if isinstance(args, dict) and "args" in args:
                body["args"] = args["args"]
            elif isinstance(args, list):
                body["args"] = args
            else:
                body["args"] = args
        if "wait" not in body:
            body["wait"] = True
        resp = self._send(body)
        if resp.get("type") == "error":
            raise RuntimeError(resp.get("msg", "command failed"))
        return resp.get("result", resp.get("value"))

    def subscribe(self, path: str, callback: NotifyCallback) -> Callable[[], None]:
        """Subscribe to node changes. Returns an unsubscribe function."""
        with self._sub_lock:
            is_new = path not in self._subscriptions or not self._subscriptions[path]
            if path not in self._subscriptions:
                self._subscriptions[path] = set()
            self._subscriptions[path].add(callback)

        if is_new:
            self._send({"cmd": "subscribe", "path": path})

        def unsub():
            with self._sub_lock:
                cbs = self._subscriptions.get(path)
                if cbs:
                    cbs.discard(callback)
                    if not cbs:
                        del self._subscriptions[path]
                        try:
                            self._send({"cmd": "unsubscribe", "path": path})
                        except Exception:
                            pass
        return unsub

    def unsubscribe(self, path: str) -> None:
        with self._sub_lock:
            self._subscriptions.pop(path, None)
        try:
            self._send({"cmd": "unsubscribe", "path": path})
        except Exception:
            pass

    def ping(self) -> bool:
        try:
            self.get("/")
            return True
        except Exception:
            return False

    def close(self):
        self._running = False
        if self.sock:
            try:
                self.sock.close()
            except Exception:
                pass

    def __del__(self):
        self.close()


# ---------------------------------------------------------------------------
# MessagePack Binary TCP (port 11000) — high-performance, subscribe support
# ---------------------------------------------------------------------------

_FLAG_REQUEST  = 0x00
_FLAG_RESPONSE = 0x40
_FLAG_NOTIFY   = 0x80
_FLAG_ERROR    = 0xC0
_FLAG_MASK     = 0xC0


class MsgPackTransport(Transport):
    """MessagePack binary transport: frame-based protocol over TCP.

    Frame: [flag:1][length:4 LE][payload: msgpack dict]
    Supports subscribe/unsubscribe with NOTIFY frame push.
    """

    def __init__(self, host: str, port: int = 11000, timeout: int = 30):
        if msgpack is None:
            raise ImportError("msgpack required: pip install msgpack")
        self.host = host
        self.port = port
        self.timeout = timeout
        self.sock = None
        self.recv_buf = b""
        self._id = 0
        self._lock = threading.Lock()
        self._pending = {}
        self._subscriptions = {}  # path -> set of callbacks
        self._sub_lock = threading.Lock()
        self._recv_thread = None
        self._running = False
        self._connect()

    def _connect(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(self.timeout)
        self.sock.connect((self.host, self.port))
        self._running = True
        self._recv_thread = threading.Thread(target=self._recv_loop, daemon=True)
        self._recv_thread.start()

    def _recv_loop(self):
        while self._running:
            try:
                data = self.sock.recv(8192)
                if not data:
                    break
                self.recv_buf += data
                self._process_frames()
            except Exception:
                break

    def _process_frames(self):
        while len(self.recv_buf) >= 5:
            flag = self.recv_buf[0]
            length = struct.unpack("<I", self.recv_buf[1:5])[0]
            if len(self.recv_buf) < 5 + length:
                break
            payload = self.recv_buf[5:5+length]
            self.recv_buf = self.recv_buf[5+length:]

            try:
                msg = msgpack.unpackb(payload, raw=False)
            except Exception:
                continue

            frame_type = flag & _FLAG_MASK

            # NOTIFY frame — subscription push
            if frame_type == _FLAG_NOTIFY:
                path = msg.get("path", "")
                value = msg.get("value")
                with self._sub_lock:
                    callbacks = list(self._subscriptions.get(path, set()))
                for cb in callbacks:
                    try:
                        cb(path, value)
                    except Exception:
                        pass
                continue

            # RESPONSE or ERROR — match to pending request
            msg_id = msg.get("id")
            if msg_id is not None and msg_id in self._pending:
                event, result_holder = self._pending.pop(msg_id)
                result_holder[0] = (flag, msg)
                event.set()

    def _send_frame(self, op: str, path: str, data=None) -> tuple:
        with self._lock:
            self._id += 1
            payload_dict = {"op": op, "path": path, "id": self._id}
            if data is not None:
                payload_dict["data"] = data

            payload = msgpack.packb(payload_dict)
            header = struct.pack("<BI", _FLAG_REQUEST, len(payload))

            event = threading.Event()
            result_holder = [None]
            self._pending[self._id] = (event, result_holder)

        try:
            self.sock.sendall(header + payload)
            if not event.wait(timeout=self.timeout):
                raise TimeoutError("Request timeout")
            return result_holder[0]
        finally:
            self._pending.pop(payload_dict["id"], None)

    def get(self, path: str) -> Any:
        flag, resp = self._send_frame("get", path)
        if (flag & _FLAG_MASK) == _FLAG_ERROR:
            return None
        return resp.get("data")

    def set(self, path: str, value: Any) -> bool:
        flag, resp = self._send_frame("set", path, value)
        return (flag & _FLAG_MASK) != _FLAG_ERROR

    def trigger(self, path: str) -> bool:
        """Trigger NODE_CHANGED on the node (set without data)."""
        flag, resp = self._send_frame("set", path)
        return (flag & _FLAG_MASK) != _FLAG_ERROR

    def list(self, path: str) -> List[Dict]:
        flag, resp = self._send_frame("ls", path)
        if (flag & _FLAG_MASK) == _FLAG_ERROR:
            return []
        data = resp.get("data", [])
        return data if isinstance(data, list) else []

    def tree(self, path: str) -> Dict:
        flag, resp = self._send_frame("get", path)
        return resp.get("data", {})

    def command(self, name: str, args: Optional[Dict] = None) -> Any:
        flag, resp = self._send_frame(name, "/", args)
        if (flag & _FLAG_MASK) == _FLAG_ERROR:
            raise RuntimeError(resp.get("data", "Command failed"))
        return resp.get("data")

    def subscribe(self, path: str, callback: NotifyCallback) -> Callable[[], None]:
        """Subscribe to node changes. Returns an unsubscribe function."""
        with self._sub_lock:
            is_new = path not in self._subscriptions or not self._subscriptions[path]
            if path not in self._subscriptions:
                self._subscriptions[path] = set()
            self._subscriptions[path].add(callback)

        if is_new:
            self._send_frame("subscribe", path)

        def unsub():
            with self._sub_lock:
                cbs = self._subscriptions.get(path)
                if cbs:
                    cbs.discard(callback)
                    if not cbs:
                        del self._subscriptions[path]
                        try:
                            self._send_frame("unsubscribe", path)
                        except Exception:
                            pass
        return unsub

    def unsubscribe(self, path: str) -> None:
        with self._sub_lock:
            self._subscriptions.pop(path, None)
        try:
            self._send_frame("unsubscribe", path)
        except Exception:
            pass

    def ping(self) -> bool:
        try:
            self.get("/")
            return True
        except Exception:
            return False

    def close(self):
        self._running = False
        if self.sock:
            try:
                self.sock.close()
            except Exception:
                pass

    def __del__(self):
        self.close()
