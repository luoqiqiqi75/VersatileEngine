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


def _normalize_path(path: str) -> str:
    return (path or "").lstrip("/")


def _command_payload(args: Optional[Dict], default_wait: bool = True) -> Dict[str, Any]:
    payload: Dict[str, Any] = {"wait": default_wait}
    if args is None:
        payload["args"] = []
        return payload
    if isinstance(args, dict):
        if "wait" in args:
            payload["wait"] = bool(args["wait"])
        if "id" in args:
            payload["id"] = args["id"]
        if "args" in args:
            payload["args"] = args["args"]
            return payload
        if "argv" in args:
            payload["args"] = args["argv"]
            return payload
        payload["args"] = {k: v for k, v in args.items() if k not in ("wait", "id")}
        return payload
    payload["args"] = args
    return payload


def _reply_error(reply: Dict[str, Any]) -> RuntimeError:
    return RuntimeError(f"{reply.get('code', 'error')}: {reply.get('error', 'unknown error')}")


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
# HTTP native protocol
# ---------------------------------------------------------------------------


class HttpRestTransport(Transport):
    """HTTP transport: POST /ve + convenience GET/PUT /at/*"""

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

    def _call(self, op: str, **payload) -> Dict[str, Any]:
        resp = self._request("POST", "/ve",
                             json={"op": op, **payload},
                             headers={"Content-Type": "application/json"})
        return resp.json()

    def get(self, path: str) -> Any:
        try:
            reply = self._call("node.get", path=_normalize_path(path))
            if not reply.get("ok"):
                if reply.get("code") == "not_found":
                    return None
                raise _reply_error(reply)
            data = reply.get("data", {})
            return data.get("value") if isinstance(data, dict) else data
        except requests.HTTPError as exc:
            if exc.response is not None and exc.response.status_code == 404:
                return None
            raise

    def set(self, path: str, value: Any) -> bool:
        reply = self._call("node.set", path=_normalize_path(path), value=value)
        return bool(reply.get("ok"))

    def trigger(self, path: str) -> bool:
        try:
            reply = self._call("node.trigger", path=_normalize_path(path))
            return bool(reply.get("ok"))
        except Exception:
            return False

    def list(self, path: str) -> List[Dict]:
        reply = self._call("node.list", path=_normalize_path(path))
        if not reply.get("ok"):
            return []
        data = reply.get("data", {})
        if isinstance(data, dict):
            return data.get("children", [])
        return []

    def tree(self, path: str) -> Dict:
        p = f"/{_normalize_path(path)}" if path and path != "/" else ""
        resp = self._request("GET", f"/at{p}")
        return resp.json()

    def command(self, name: str, args: Optional[Dict] = None) -> Any:
        reply = self._call("command.run", name=name, **_command_payload(args))
        if not reply.get("ok"):
            raise _reply_error(reply)
        if reply.get("accepted"):
            return {"accepted": True, "task_id": reply.get("task_id")}
        return reply.get("data")

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
        try:
            data = self._call("node.get", {"path": _normalize_path(path)})
        except RuntimeError as exc:
            if "not_found" in str(exc):
                return None
            raise
        return data.get("value") if isinstance(data, dict) else data

    def set(self, path: str, value: Any) -> bool:
        data = self._call("node.set", {"path": _normalize_path(path), "value": value})
        return isinstance(data, dict) and "path" in data

    def trigger(self, path: str) -> bool:
        data = self._call("node.trigger", {"path": _normalize_path(path)})
        return isinstance(data, dict) and "path" in data

    def list(self, path: str) -> List[Dict]:
        data = self._call("node.list", {"path": _normalize_path(path)})
        return data.get("children", []) if isinstance(data, dict) else []

    def tree(self, path: str) -> Dict:
        data = self._call("node.get", {"path": _normalize_path(path), "depth": -1})
        if isinstance(data, dict):
            return data.get("tree", data.get("value", {}))
        return data

    def command(self, name: str, args: Optional[Dict] = None) -> Any:
        data = self._call("command.run", {"name": name, **_command_payload(args)})
        return data

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

            msg_event = msg.get("event", "")
            msg_id = msg.get("id")

            # Event push from subscription
            if msg_event == "node.changed":
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
        resp = self._send({"op": "node.get", "path": _normalize_path(path)})
        if not resp.get("ok"):
            return None
        data = resp.get("data", {})
        return data.get("value") if isinstance(data, dict) else data

    def set(self, path: str, value: Any) -> bool:
        resp = self._send({"op": "node.set", "path": _normalize_path(path), "value": value})
        return bool(resp.get("ok"))

    def trigger(self, path: str) -> bool:
        resp = self._send({"op": "node.trigger", "path": _normalize_path(path)})
        return bool(resp.get("ok"))

    def list(self, path: str) -> List[Dict]:
        resp = self._send({"op": "node.list", "path": _normalize_path(path)})
        if not resp.get("ok"):
            return []
        data = resp.get("data", {})
        return data.get("children", []) if isinstance(data, dict) else []

    def tree(self, path: str) -> Dict:
        resp = self._send({"op": "node.get", "path": _normalize_path(path), "depth": -1})
        if not resp.get("ok"):
            return {}
        data = resp.get("data", {})
        if isinstance(data, dict):
            return data.get("tree", data.get("value", {}))
        return data

    def command(self, name: str, args: Optional[Dict] = None) -> Any:
        body = {"op": "command.run", "name": name, **_command_payload(args)}
        resp = self._send(body)
        if not resp.get("ok"):
            raise _reply_error(resp)
        if resp.get("accepted"):
            return {"accepted": True, "task_id": resp.get("task_id")}
        return resp.get("data")

    def subscribe(self, path: str, callback: NotifyCallback) -> Callable[[], None]:
        """Subscribe to node changes. Returns an unsubscribe function."""
        path = _normalize_path(path)
        with self._sub_lock:
            is_new = path not in self._subscriptions or not self._subscriptions[path]
            if path not in self._subscriptions:
                self._subscriptions[path] = set()
            self._subscriptions[path].add(callback)

        if is_new:
            self._send({"op": "subscribe", "path": path})

        def unsub():
            with self._sub_lock:
                cbs = self._subscriptions.get(path)
                if cbs:
                    cbs.discard(callback)
                    if not cbs:
                        del self._subscriptions[path]
                        try:
                            self._send({"op": "unsubscribe", "path": path})
                        except Exception:
                            pass
        return unsub

    def unsubscribe(self, path: str) -> None:
        path = _normalize_path(path)
        with self._sub_lock:
            self._subscriptions.pop(path, None)
        try:
            self._send({"op": "unsubscribe", "path": path})
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

            # NOTIFY frame — subscription push / async result
            if frame_type == _FLAG_NOTIFY:
                if msg.get("event") == "node.changed":
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
            payload_dict = {"op": op, "id": self._id}
            if path:
                payload_dict["path"] = path
            if isinstance(data, dict):
                payload_dict.update(data)
            elif data is not None:
                payload_dict["value"] = data

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
        flag, resp = self._send_frame("node.get", _normalize_path(path))
        if (flag & _FLAG_MASK) == _FLAG_ERROR or not resp.get("ok"):
            return None
        data = resp.get("data", {})
        return data.get("value") if isinstance(data, dict) else data

    def set(self, path: str, value: Any) -> bool:
        flag, resp = self._send_frame("node.set", _normalize_path(path), {"value": value})
        return (flag & _FLAG_MASK) != _FLAG_ERROR and bool(resp.get("ok"))

    def trigger(self, path: str) -> bool:
        flag, resp = self._send_frame("node.trigger", _normalize_path(path))
        return (flag & _FLAG_MASK) != _FLAG_ERROR and bool(resp.get("ok"))

    def list(self, path: str) -> List[Dict]:
        flag, resp = self._send_frame("node.list", _normalize_path(path))
        if (flag & _FLAG_MASK) == _FLAG_ERROR or not resp.get("ok"):
            return []
        data = resp.get("data", {})
        if isinstance(data, dict):
            children = data.get("children", [])
            return children if isinstance(children, list) else []
        return []

    def tree(self, path: str) -> Dict:
        flag, resp = self._send_frame("node.get", _normalize_path(path), {"depth": -1})
        if (flag & _FLAG_MASK) == _FLAG_ERROR or not resp.get("ok"):
            return {}
        data = resp.get("data", {})
        if isinstance(data, dict):
            return data.get("tree", data.get("value", {}))
        return data

    def command(self, name: str, args: Optional[Dict] = None) -> Any:
        flag, resp = self._send_frame("command.run", "", {"name": name, **_command_payload(args)})
        if (flag & _FLAG_MASK) == _FLAG_ERROR or not resp.get("ok"):
            raise _reply_error(resp)
        if resp.get("accepted"):
            return {"accepted": True, "task_id": resp.get("task_id")}
        return resp.get("data")

    def subscribe(self, path: str, callback: NotifyCallback) -> Callable[[], None]:
        """Subscribe to node changes. Returns an unsubscribe function."""
        path = _normalize_path(path)
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
        path = _normalize_path(path)
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
