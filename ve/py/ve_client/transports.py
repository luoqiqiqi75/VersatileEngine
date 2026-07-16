"""Transport implementations for VeClient.

Envelope v2.1: requests use top-level `op` / `cmd` / `batch` with arguments
nested under `params`; replies are `{id, code, data?, message?}` where
`code >= 0` is success and `code < 0` is failure.
"""

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
NotifyCallback = Callable[[str, Any], None]  # (path, data)

CODE_NOT_FOUND = -3


def _normalize_path(path: str) -> str:
    return (path or "").lstrip("/")


def _command_params(args: Optional[Dict]) -> Dict[str, Any]:
    """Build the v2.1 `params` object for a user command.

    Accepts a dict of named params, or a positional list/scalar wrapped under
    `args`. Legacy envelope keys (`wait`, `id`) are dropped.
    """
    if args is None:
        return {}
    if isinstance(args, dict):
        return {k: v for k, v in args.items() if k not in ("wait", "id")}
    return {"args": args}


def _ok(reply: Dict[str, Any]) -> bool:
    return reply.get("code", -1) >= 0


def _reply_error(reply: Dict[str, Any]) -> RuntimeError:
    return RuntimeError(f"{reply.get('code', 'error')}: {reply.get('message', 'unknown error')}")


class RpcError(RuntimeError):
    """JSON-RPC error carrying the numeric VE code."""

    def __init__(self, code: Any, message: str):
        super().__init__(f"JSON-RPC error {code}: {message}")
        self.code = code


_UNSET = object()


class Transport(ABC):
    """Abstract transport for VE Node operations."""

    @abstractmethod
    def get(self, path: str, depth: int = -1) -> Any:
        """Export node subtree or value (default depth=-1 returns full tree)."""
        pass

    @abstractmethod
    def set(self, path: str, tree: Any) -> bool:
        """Import a subtree at path (merge)."""
        pass

    @abstractmethod
    def val(self, path: str, value: Any = _UNSET) -> Any:
        """Get or set a single node value."""
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
    def op(self, op: str, **params) -> Any:
        """Raw op call — generic envelope passthrough.

        Sends a v2.1 envelope `{op: name, params: {...}}` and returns the
        reply's `data` payload (or raises on failure). Use this to reach any
        op the server registers without a dedicated client wrapper.
        """
        pass

    @abstractmethod
    def ping(self) -> bool:
        pass

    @abstractmethod
    def rm(self, path: str) -> bool:
        """Remove node at path (erase)."""
        pass

    def subscribe(self, path: str, callback: NotifyCallback,
                  depth: int = -1, once: bool = False,
                  immediate: bool = False) -> Callable[[], None]:
        raise NotImplementedError("subscribe not supported on this transport")

    def unsubscribe(self, path: str) -> None:
        raise NotImplementedError("unsubscribe not supported on this transport")

    def trigger(self, path: str) -> bool:
        raise NotImplementedError("trigger not supported on this transport")


# ---------------------------------------------------------------------------
# HTTP native protocol
# ---------------------------------------------------------------------------


class HttpRestTransport(Transport):
    """HTTP transport: POST /ve + convenience GET /at/*"""

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

    def _send(self, message: Dict[str, Any]) -> Dict[str, Any]:
        resp = self._request("POST", "/ve", json=message,
                             headers={"Content-Type": "application/json"})
        if resp.status_code == 202:  # Result::accept
            return {"code": 0, "data": {"accepted": True}}
        return resp.json()

    def _op(self, op: str, **params) -> Dict[str, Any]:
        return self._send({"op": op, "params": params})

    def get(self, path: str, depth: int = -1) -> Any:
        """Export node subtree or value (default depth=-1 returns full tree)."""
        try:
            reply = self._op("export", path=_normalize_path(path), depth=depth)
            if not _ok(reply):
                if reply.get("code") == CODE_NOT_FOUND:
                    return None
                raise _reply_error(reply)
            data = reply.get("data", {})
            if isinstance(data, dict):
                return data.get("tree") if data.get("tree") is not None else data.get("value")
            return data
        except requests.HTTPError as exc:
            if exc.response is not None and exc.response.status_code == 404:
                return None
            raise

    def set(self, path: str, tree: Any) -> bool:
        """Import a subtree at path (merge)."""
        reply = self._op("import", path=_normalize_path(path), tree=tree)
        return _ok(reply)

    def val(self, path: str, value: Any = _UNSET) -> Any:
        """Get or set a single node value."""
        if value is _UNSET:
            try:
                reply = self._op("get", path=_normalize_path(path))
                if not _ok(reply):
                    if reply.get("code") == CODE_NOT_FOUND:
                        return None
                    raise _reply_error(reply)
                data = reply.get("data", {})
                return data.get("value") if isinstance(data, dict) else data
            except requests.HTTPError as exc:
                if exc.response is not None and exc.response.status_code == 404:
                    return None
                raise
        else:
            reply = self._op("set", path=_normalize_path(path), value=value)
            return _ok(reply)

    def trigger(self, path: str) -> bool:
        try:
            reply = self._op("trigger", path=_normalize_path(path))
            return _ok(reply)
        except Exception:
            return False

    def rm(self, path: str) -> bool:
        reply = self._op("erase", path=_normalize_path(path))
        return _ok(reply)

    def list(self, path: str) -> List[Dict]:
        reply = self._op("children", path=_normalize_path(path))
        if not _ok(reply):
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
        reply = self._send({"cmd": name, "params": _command_params(args)})
        if not _ok(reply):
            raise _reply_error(reply)
        return reply.get("data")

    def cmds(self) -> List[Dict]:
        reply = self._op("commands")
        if not _ok(reply):
            return []
        data = reply.get("data", {})
        if isinstance(data, dict):
            return data.get("commands", [])
        return data if isinstance(data, list) else []

    def op(self, op: str, **params) -> Any:
        reply = self._op(op, **params)
        if not _ok(reply):
            raise _reply_error(reply)
        return reply.get("data")

    def batch(self, items: List[Dict]) -> List[Any]:
        reply = self._send({"batch": items})
        if not _ok(reply):
            raise _reply_error(reply)
        data = reply.get("data", [])
        return data if isinstance(data, list) else []

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
    """JSON-RPC 2.0 transport: POST /jsonrpc.

    The JSON-RPC `method` is the v2.1 op name (or a command name); the result
    is the op's `data` payload.
    """

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
            raise RpcError(err.get("code"), err.get("message"))

        return result.get("result")

    def get(self, path: str, depth: int = -1) -> Any:
        """Export node subtree or value (default depth=-1 returns full tree)."""
        try:
            data = self._call("export", {"path": _normalize_path(path), "depth": depth})
        except RpcError as exc:
            if exc.code == CODE_NOT_FOUND:
                return None
            raise
        if isinstance(data, dict):
            return data.get("tree") if data.get("tree") is not None else data.get("value")
        return data

    def set(self, path: str, tree: Any) -> bool:
        """Import a subtree at path (merge)."""
        try:
            self._call("import", {"path": _normalize_path(path), "tree": tree})
            return True
        except RpcError:
            return False

    def val(self, path: str, value: Any = _UNSET) -> Any:
        """Get or set a single node value."""
        if value is _UNSET:
            try:
                data = self._call("get", {"path": _normalize_path(path)})
            except RpcError as exc:
                if exc.code == CODE_NOT_FOUND:
                    return None
                raise
            return data.get("value") if isinstance(data, dict) else data
        else:
            try:
                self._call("set", {"path": _normalize_path(path), "value": value})
                return True
            except RpcError:
                return False

    def trigger(self, path: str) -> bool:
        try:
            self._call("trigger", {"path": _normalize_path(path)})
            return True
        except RpcError:
            return False

    def rm(self, path: str) -> bool:
        try:
            self._call("erase", {"path": _normalize_path(path)})
            return True
        except RpcError:
            return False

    def list(self, path: str) -> List[Dict]:
        data = self._call("children", {"path": _normalize_path(path)})
        return data.get("children", []) if isinstance(data, dict) else []

    def tree(self, path: str) -> Dict:
        data = self._call("export", {"path": _normalize_path(path), "depth": -1})
        if isinstance(data, dict):
            return data.get("tree", data.get("value", {}))
        return data

    def command(self, name: str, args: Optional[Dict] = None) -> Any:
        return self._call(name, _command_params(args))

    def cmds(self) -> List[Dict]:
        data = self._call("commands", {})
        if isinstance(data, dict):
            return data.get("commands", [])
        return data if isinstance(data, list) else []

    def op(self, op: str, **params) -> Any:
        # JSON-RPC method = op name; result is already the data payload.
        # RpcError propagates on failure.
        return self._call(op, params)

    def ping(self) -> bool:
        try:
            self._call("get", {"path": "/"})
            return True
        except Exception:
            return False


# ---------------------------------------------------------------------------
# TCP JSON (port 12200) — persistent connection, subscribe support
# ---------------------------------------------------------------------------


class TcpJsonTransport(Transport):
    """TCP JSON transport: newline-delimited JSON envelope over persistent TCP.

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
        self._disconnect_error = None
        self._connect()

    def _connect(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        # timeout applies ONLY to the connect() handshake; a persistent
        # subscription socket must not silently die on idle. Request-level
        # deadlines are enforced by event.wait(timeout=...) in _send.
        self.sock.settimeout(self.timeout)
        self.sock.connect((self.host, self.port))
        self.sock.settimeout(None)   # switch to blocking mode for recv_loop
        self._running = True
        self._recv_thread = threading.Thread(target=self._recv_loop, daemon=True)
        self._recv_thread.start()

    def _recv_loop(self):
        sock = self.sock
        disconnect_error = ConnectionError("Connection closed by peer")
        try:
            while self._running:
                data = sock.recv(8192)
                if not data:
                    break
                self.recv_buf += data.decode('utf-8')
                self._process_messages()
        except Exception as exc:
            if self._running:
                disconnect_error = ConnectionError(f"Connection lost: {exc}")
            else:
                disconnect_error = ConnectionError("Transport closed")
        finally:
            self._disconnect(disconnect_error, sock)

    def _disconnect(self, error: ConnectionError, sock=None):
        """Close the socket and fail every request waiting on this connection."""
        with self._lock:
            if self._disconnect_error is None:
                self._disconnect_error = error
            self._running = False
            if sock is None:
                sock = self.sock
            if self.sock is sock:
                self.sock = None
            pending = list(self._pending.values())
            self._pending.clear()

        if sock is not None:
            try:
                sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            try:
                sock.close()
            except OSError:
                pass

        for event, result_holder in pending:
            result_holder[0] = self._disconnect_error
            event.set()

    def _is_connected(self) -> bool:
        with self._lock:
            return (self._running and self.sock is not None
                    and self._disconnect_error is None
                    and self._recv_thread is not None
                    and self._recv_thread.is_alive())

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
                data = msg.get("data")
                with self._sub_lock:
                    callbacks = list(self._subscriptions.get(path, set()))
                for cb in callbacks:
                    try:
                        cb(path, data)
                    except Exception:
                        pass
                continue

            # Response to a pending request
            with self._lock:
                pending = self._pending.pop(msg_id, None) if msg_id is not None else None
            if pending is not None:
                event, result_holder = pending
                result_holder[0] = msg
                event.set()

    def _send(self, message: dict) -> dict:
        with self._lock:
            if (not self._running or self.sock is None
                    or self._disconnect_error is not None
                    or self._recv_thread is None
                    or not self._recv_thread.is_alive()):
                raise self._disconnect_error or ConnectionError("Connection is not available")
            self._id += 1
            message["id"] = self._id
            mid = self._id
            event = threading.Event()
            result_holder = [None]
            self._pending[mid] = (event, result_holder)
            sock = self.sock

        try:
            try:
                sock.sendall((json.dumps(message, separators=(',', ':')) + "\n").encode('utf-8'))
            except OSError as exc:
                error = ConnectionError(f"Failed to send request: {exc}")
                self._disconnect(error, sock)
                raise error from exc
            if not event.wait(timeout=self.timeout):
                raise TimeoutError("Request timeout")
            result = result_holder[0]
            if isinstance(result, BaseException):
                raise result
            return result
        finally:
            with self._lock:
                self._pending.pop(mid, None)

    def _op(self, op: str, **params) -> dict:
        return self._send({"op": op, "params": params})

    def get(self, path: str, depth: int = -1) -> Any:
        """Export node subtree or value (default depth=-1 returns full tree)."""
        resp = self._op("export", path=_normalize_path(path), depth=depth)
        if not _ok(resp):
            return None
        data = resp.get("data", {})
        if isinstance(data, dict):
            return data.get("tree") if data.get("tree") is not None else data.get("value")
        return data

    def set(self, path: str, tree: Any) -> bool:
        """Import a subtree at path (merge)."""
        resp = self._op("import", path=_normalize_path(path), tree=tree)
        return _ok(resp)

    def val(self, path: str, value: Any = _UNSET) -> Any:
        """Get or set a single node value."""
        if value is _UNSET:
            resp = self._op("get", path=_normalize_path(path))
            if not _ok(resp):
                return None
            data = resp.get("data", {})
            return data.get("value") if isinstance(data, dict) else data
        else:
            resp = self._op("set", path=_normalize_path(path), value=value)
            return _ok(resp)

    def trigger(self, path: str) -> bool:
        resp = self._op("trigger", path=_normalize_path(path))
        return _ok(resp)

    def rm(self, path: str) -> bool:
        resp = self._op("erase", path=_normalize_path(path))
        return _ok(resp)

    def list(self, path: str) -> List[Dict]:
        resp = self._op("children", path=_normalize_path(path))
        if not _ok(resp):
            return []
        data = resp.get("data", {})
        return data.get("children", []) if isinstance(data, dict) else []

    def tree(self, path: str) -> Dict:
        resp = self._op("export", path=_normalize_path(path), depth=-1)
        if not _ok(resp):
            return {}
        data = resp.get("data", {})
        if isinstance(data, dict):
            return data.get("tree", data.get("value", {}))
        return data

    def command(self, name: str, args: Optional[Dict] = None) -> Any:
        resp = self._send({"cmd": name, "params": _command_params(args)})
        if not _ok(resp):
            raise _reply_error(resp)
        return resp.get("data")

    def cmds(self) -> List[Dict]:
        resp = self._op("commands")
        if not _ok(resp):
            return []
        data = resp.get("data", {})
        if isinstance(data, dict):
            return data.get("commands", [])
        return data if isinstance(data, list) else []

    def op(self, op: str, **params) -> Any:
        resp = self._op(op, **params)
        if not _ok(resp):
            raise _reply_error(resp)
        return resp.get("data")

    def batch(self, items: List[Dict]) -> List[Any]:
        resp = self._send({"batch": items})
        if not _ok(resp):
            raise _reply_error(resp)
        data = resp.get("data", [])
        return data if isinstance(data, list) else []

    def subscribe(self, path: str, callback: NotifyCallback,
                  depth: int = -1, once: bool = False,
                  immediate: bool = False) -> Callable[[], None]:
        """Subscribe to node changes. Returns an unsubscribe function."""
        path = _normalize_path(path)
        with self._sub_lock:
            is_new = path not in self._subscriptions or not self._subscriptions[path]
            if path not in self._subscriptions:
                self._subscriptions[path] = set()
            self._subscriptions[path].add(callback)

        if is_new:
            params: dict = {"path": path, "depth": depth}
            if once:
                params["once"] = True
            if immediate:
                params["immediate"] = True
            resp = self._send({"op": "subscribe", "params": params})
            if immediate and _ok(resp) and resp.get("data") is not None:
                try:
                    callback(path, resp.get("data"))
                except Exception:
                    pass

        def unsub():
            with self._sub_lock:
                cbs = self._subscriptions.get(path)
                if cbs:
                    cbs.discard(callback)
                    if not cbs:
                        del self._subscriptions[path]
                        try:
                            self._send({"op": "unsubscribe", "params": {"path": path}})
                        except Exception:
                            pass
        return unsub

    def unsubscribe(self, path: str) -> None:
        path = _normalize_path(path)
        with self._sub_lock:
            self._subscriptions.pop(path, None)
        try:
            self._send({"op": "unsubscribe", "params": {"path": path}})
        except Exception:
            pass

    def ping(self) -> bool:
        if not self._is_connected():
            return False
        try:
            self.val("/")
            return True
        except Exception:
            return False

    def close(self):
        self._disconnect(ConnectionError("Transport closed"))

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

    Frame: [flag:1][length:4 LE][payload: msgpack envelope dict]
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
        self._disconnect_error = None
        self._connect()

    def _connect(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(self.timeout)
        self.sock.connect((self.host, self.port))
        self.sock.settimeout(None)
        self._running = True
        self._recv_thread = threading.Thread(target=self._recv_loop, daemon=True)
        self._recv_thread.start()

    def _recv_loop(self):
        sock = self.sock
        disconnect_error = ConnectionError("Connection closed by peer")
        try:
            while self._running:
                data = sock.recv(8192)
                if not data:
                    break
                self.recv_buf += data
                self._process_frames()
        except Exception as exc:
            if self._running:
                disconnect_error = ConnectionError(f"Connection lost: {exc}")
            else:
                disconnect_error = ConnectionError("Transport closed")
        finally:
            self._disconnect(disconnect_error, sock)

    def _disconnect(self, error: ConnectionError, sock=None):
        """Close the socket and fail every request waiting on this connection."""
        with self._lock:
            if self._disconnect_error is None:
                self._disconnect_error = error
            self._running = False
            if sock is None:
                sock = self.sock
            if self.sock is sock:
                self.sock = None
            pending = list(self._pending.values())
            self._pending.clear()

        if sock is not None:
            try:
                sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            try:
                sock.close()
            except OSError:
                pass

        for event, result_holder in pending:
            result_holder[0] = self._disconnect_error
            event.set()

    def _is_connected(self) -> bool:
        with self._lock:
            return (self._running and self.sock is not None
                    and self._disconnect_error is None
                    and self._recv_thread is not None
                    and self._recv_thread.is_alive())

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
                if msg.get("event") == "node.changed":
                    path = msg.get("path", "")
                    data = msg.get("data")
                    with self._sub_lock:
                        callbacks = list(self._subscriptions.get(path, set()))
                    for cb in callbacks:
                        try:
                            cb(path, data)
                        except Exception:
                            pass
                continue

            # RESPONSE or ERROR — match to pending request
            msg_id = msg.get("id")
            with self._lock:
                pending = self._pending.pop(msg_id, None) if msg_id is not None else None
            if pending is not None:
                event, result_holder = pending
                result_holder[0] = (flag, msg)
                event.set()

    def _send_message(self, message: dict) -> tuple:
        with self._lock:
            if (not self._running or self.sock is None
                    or self._disconnect_error is not None
                    or self._recv_thread is None
                    or not self._recv_thread.is_alive()):
                raise self._disconnect_error or ConnectionError("Connection is not available")
            self._id += 1
            message["id"] = self._id
            mid = self._id
            payload = msgpack.packb(message)
            header = struct.pack("<BI", _FLAG_REQUEST, len(payload))

            event = threading.Event()
            result_holder = [None]
            self._pending[mid] = (event, result_holder)
            sock = self.sock

        try:
            try:
                sock.sendall(header + payload)
            except OSError as exc:
                error = ConnectionError(f"Failed to send request: {exc}")
                self._disconnect(error, sock)
                raise error from exc
            if not event.wait(timeout=self.timeout):
                raise TimeoutError("Request timeout")
            result = result_holder[0]
            if isinstance(result, BaseException):
                raise result
            return result
        finally:
            with self._lock:
                self._pending.pop(mid, None)

    def _op(self, op: str, **params) -> tuple:
        return self._send_message({"op": op, "params": params})

    @staticmethod
    def _failed(flag: int, resp: dict) -> bool:
        return (flag & _FLAG_MASK) == _FLAG_ERROR or not _ok(resp)

    def get(self, path: str, depth: int = -1) -> Any:
        """Export node subtree or value (default depth=-1 returns full tree)."""
        flag, resp = self._op("export", path=_normalize_path(path), depth=depth)
        if self._failed(flag, resp):
            return None
        data = resp.get("data", {})
        if isinstance(data, dict):
            return data.get("tree") if data.get("tree") is not None else data.get("value")
        return data

    def set(self, path: str, tree: Any) -> bool:
        """Import a subtree at path (merge)."""
        flag, resp = self._op("import", path=_normalize_path(path), tree=tree)
        return not self._failed(flag, resp)

    def val(self, path: str, value: Any = _UNSET) -> Any:
        """Get or set a single node value."""
        if value is _UNSET:
            flag, resp = self._op("get", path=_normalize_path(path))
            if self._failed(flag, resp):
                return None
            data = resp.get("data", {})
            return data.get("value") if isinstance(data, dict) else data
        else:
            flag, resp = self._op("set", path=_normalize_path(path), value=value)
            return not self._failed(flag, resp)

    def trigger(self, path: str) -> bool:
        flag, resp = self._op("trigger", path=_normalize_path(path))
        return not self._failed(flag, resp)

    def rm(self, path: str) -> bool:
        flag, resp = self._op("erase", path=_normalize_path(path))
        return not self._failed(flag, resp)

    def list(self, path: str) -> List[Dict]:
        flag, resp = self._op("children", path=_normalize_path(path))
        if self._failed(flag, resp):
            return []
        data = resp.get("data", {})
        if isinstance(data, dict):
            children = data.get("children", [])
            return children if isinstance(children, list) else []
        return []

    def tree(self, path: str) -> Dict:
        flag, resp = self._op("export", path=_normalize_path(path), depth=-1)
        if self._failed(flag, resp):
            return {}
        data = resp.get("data", {})
        if isinstance(data, dict):
            return data.get("tree", data.get("value", {}))
        return data

    def command(self, name: str, args: Optional[Dict] = None) -> Any:
        flag, resp = self._send_message({"cmd": name, "params": _command_params(args)})
        if self._failed(flag, resp):
            raise _reply_error(resp)
        return resp.get("data")

    def cmds(self) -> List[Dict]:
        flag, resp = self._op("commands")
        if self._failed(flag, resp):
            return []
        data = resp.get("data", {})
        if isinstance(data, dict):
            return data.get("commands", [])
        return data if isinstance(data, list) else []

    def op(self, op: str, **params) -> Any:
        flag, resp = self._op(op, **params)
        if self._failed(flag, resp):
            raise _reply_error(resp)
        return resp.get("data")

    def batch(self, items: List[Dict]) -> List[Any]:
        flag, resp = self._send_message({"batch": items})
        if self._failed(flag, resp):
            raise _reply_error(resp)
        data = resp.get("data", [])
        return data if isinstance(data, list) else []

    def subscribe(self, path: str, callback: NotifyCallback,
                  depth: int = -1, once: bool = False,
                  immediate: bool = False) -> Callable[[], None]:
        """Subscribe to node changes. Returns an unsubscribe function."""
        path = _normalize_path(path)
        with self._sub_lock:
            is_new = path not in self._subscriptions or not self._subscriptions[path]
            if path not in self._subscriptions:
                self._subscriptions[path] = set()
            self._subscriptions[path].add(callback)

        if is_new:
            params: dict = {"path": path, "depth": depth}
            if once:
                params["once"] = True
            if immediate:
                params["immediate"] = True
            flag, resp = self._send_message({"op": "subscribe", "params": params})
            if immediate and not self._failed(flag, resp) and resp.get("data") is not None:
                try:
                    callback(path, resp.get("data"))
                except Exception:
                    pass

        def unsub():
            with self._sub_lock:
                cbs = self._subscriptions.get(path)
                if cbs:
                    cbs.discard(callback)
                    if not cbs:
                        del self._subscriptions[path]
                        try:
                            self._send_message({"op": "unsubscribe", "params": {"path": path}})
                        except Exception:
                            pass
        return unsub

    def unsubscribe(self, path: str) -> None:
        path = _normalize_path(path)
        with self._sub_lock:
            self._subscriptions.pop(path, None)
        try:
            self._send_message({"op": "unsubscribe", "params": {"path": path}})
        except Exception:
            pass

    def ping(self) -> bool:
        if not self._is_connected():
            return False
        try:
            self.val("/")
            return True
        except Exception:
            return False

    def close(self):
        self._disconnect(ConnectionError("Transport closed"))

    def __del__(self):
        self.close()
