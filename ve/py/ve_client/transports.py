"""Transport implementations for VeClient."""

import json
import socket
import struct
import threading
from abc import ABC, abstractmethod
from typing import Any, Dict, List, Optional, Callable
import requests

try:
    import msgpack
except ImportError:
    msgpack = None


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


class HttpRestTransport(Transport):
    """REST API transport: GET/PUT /api/node/*, GET /api/tree/*, POST /api/cmd/*"""

    def __init__(self, base_url: str, timeout: int = 30):
        self.base = base_url.rstrip("/")
        self.timeout = timeout
        self.session = requests.Session()  # Reuse connections

    def _request(self, method: str, path: str, **kwargs) -> requests.Response:
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
                # Node not found, return None
                return None
            raise

    def set(self, path: str, value: Any) -> bool:
        p = f"/{path}" if path else ""
        resp = self._request("PUT", f"/api/node{p}",
                             json=value,
                             headers={"Content-Type": "application/json"})
        return resp.json().get("ok", False)

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


class JsonRpcTransport(Transport):
    """JSON-RPC 2.0 transport: POST /jsonrpc"""

    def __init__(self, base_url: str, timeout: int = 30):
        self.url = base_url.rstrip("/") + "/jsonrpc"
        self.timeout = timeout
        self._id = 0
        self.session = requests.Session()  # Reuse connections

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

    def list(self, path: str) -> List[Dict]:
        data = self._call("node.list", {"path": path})
        if isinstance(data, dict) and not data.get("found", True):
            return []
        return data.get("children", []) if isinstance(data, dict) else []

    def tree(self, path: str) -> Dict:
        # JSON-RPC currently has no tree method, fall back to get
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


class TcpJsonTransport(Transport):
    """TCP JSON transport: newline-delimited JSON over persistent TCP connection."""

    def __init__(self, host: str, port: int = 5082, timeout: int = 30):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.sock = None
        self.recv_buf = ""
        self._id = 0
        self._lock = threading.Lock()
        self._pending = {}  # id -> event, result
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
                data = self.sock.recv(4096)
                if not data:
                    break
                self.recv_buf += data.decode('utf-8')
                self._process_messages()
            except Exception:
                break

    def _process_messages(self):
        """Process complete JSON messages from buffer."""
        while self.recv_buf:
            self.recv_buf = self.recv_buf.lstrip()
            if not self.recv_buf:
                break

            # Try to parse JSON from start of buffer
            decoder = json.JSONDecoder()
            try:
                msg, idx = decoder.raw_decode(self.recv_buf)
                self.recv_buf = self.recv_buf[idx:]

                msg_id = msg.get("id")
                if msg_id and msg_id in self._pending:
                    event, result_holder = self._pending.pop(msg_id)
                    result_holder[0] = msg
                    event.set()
            except json.JSONDecodeError:
                # Incomplete JSON, wait for more data
                break

    def _send(self, cmd: dict) -> dict:
        with self._lock:
            self._id += 1
            cmd["id"] = self._id
            event = threading.Event()
            result_holder = [None]
            self._pending[self._id] = (event, result_holder)

        try:
            self.sock.sendall((json.dumps(cmd) + "\n").encode('utf-8'))
            if not event.wait(timeout=self.timeout):
                raise TimeoutError("Request timeout")
            return result_holder[0]
        finally:
            self._pending.pop(cmd["id"], None)

    def get(self, path: str) -> Any:
        resp = self._send({"cmd": "get", "path": path})
        if resp.get("type") == "error":
            # Node not found, return None
            return None
        return resp.get("value")

    def set(self, path: str, value: Any) -> bool:
        resp = self._send({"cmd": "set", "path": path, "value": value})
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
        # TCP JSON doesn't have command support, fall back to get
        raise NotImplementedError("Command not supported on TCP JSON transport")

    def ping(self) -> bool:
        try:
            self.get("/")
            return True
        except Exception:
            return False

    def close(self):
        self._running = False
        if self.sock:
            self.sock.close()

    def __del__(self):
        self.close()


class MsgPackTransport(Transport):
    """MessagePack binary transport: frame-based protocol over TCP."""

    def __init__(self, host: str, port: int = 5065, timeout: int = 30):
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
                data = self.sock.recv(4096)
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
                msg_id = msg.get("id")
                if msg_id and msg_id in self._pending:
                    event, result_holder = self._pending.pop(msg_id)
                    result_holder[0] = (flag, msg)
                    event.set()
            except Exception:
                pass

    def _send_frame(self, op: str, path: str, args=None) -> tuple:
        with self._lock:
            self._id += 1
            payload_dict = {"op": op, "path": path, "id": self._id}
            if args:
                payload_dict["args"] = args

            payload = msgpack.packb(payload_dict)
            header = struct.pack("<BI", 0x00, len(payload))  # FLAG_REQUEST

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
        if flag == 0xC0:  # FLAG_ERROR
            # Node not found or command not found, return None
            return None
        return resp.get("data")

    def set(self, path: str, value: Any) -> bool:
        flag, resp = self._send_frame("set", path, [value])
        return flag != 0xC0

    def list(self, path: str) -> List[Dict]:
        flag, resp = self._send_frame("ls", path)
        if flag == 0xC0:
            return []
        data = resp.get("data", [])
        return data if isinstance(data, list) else []

    def tree(self, path: str) -> Dict:
        flag, resp = self._send_frame("get", path)
        return resp.get("data", {})

    def command(self, name: str, args: Optional[Dict] = None) -> Any:
        flag, resp = self._send_frame(name, "/", args)
        if flag == 0xC0:
            raise RuntimeError(resp.get("data", "Command failed"))
        return resp.get("data")

    def ping(self) -> bool:
        try:
            self.get("/")
            return True
        except Exception:
            return False

    def close(self):
        self._running = False
        if self.sock:
            self.sock.close()

    def __del__(self):
        self.close()
