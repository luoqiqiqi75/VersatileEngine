"""Async transport implementations for VeClient.

Envelope v2.1 — same wire format as the sync transports (see transports.py).
"""

from abc import ABC, abstractmethod
from typing import Any, Dict, List, Optional, Callable

from .transports import (
    NotifyCallback, CODE_NOT_FOUND, RpcError, _UNSET,
    _normalize_path, _command_params, _ok, _reply_error,
)

try:
    import httpx
except ImportError:
    httpx = None


class AsyncTransport(ABC):
    """Abstract async transport for VE Node operations."""

    @abstractmethod
    async def get(self, path: str, depth: int = -1) -> Any:
        """Export node subtree or value (default depth=-1 returns full tree)."""
        pass

    @abstractmethod
    async def set(self, path: str, tree: Any) -> bool:
        """Import a subtree at path (merge)."""
        pass

    @abstractmethod
    async def val(self, path: str, value: Any = _UNSET) -> Any:
        """Get or set a single node value."""
        pass

    @abstractmethod
    async def list(self, path: str) -> List[Dict]:
        pass

    @abstractmethod
    async def tree(self, path: str) -> Dict:
        pass

    @abstractmethod
    async def command(self, name: str, args: Optional[Dict]) -> Any:
        pass

    @abstractmethod
    async def ping(self) -> bool:
        pass

    @abstractmethod
    async def rm(self, path: str) -> bool:
        """Remove node at path (erase)."""
        pass

    async def trigger(self, path: str) -> bool:
        raise NotImplementedError("trigger not supported on this transport")


class AsyncHttpRestTransport(AsyncTransport):
    """Async HTTP transport using httpx."""

    def __init__(self, base_url: str, timeout: int = 30):
        if httpx is None:
            raise ImportError("httpx required: pip install httpx")
        self.base = base_url.rstrip("/")
        self.timeout = timeout
        self.client = httpx.AsyncClient(timeout=timeout)

    async def _request(self, method: str, path: str, **kwargs):
        resp = await self.client.request(method, f"{self.base}{path}", **kwargs)
        resp.raise_for_status()
        return resp

    async def _send(self, message: Dict[str, Any]) -> Dict[str, Any]:
        resp = await self._request("POST", "/ve", json=message,
                                    headers={"Content-Type": "application/json"})
        if resp.status_code == 202:  # Result::accept
            return {"code": 0, "data": {"accepted": True}}
        return resp.json()

    async def _op(self, op: str, **params) -> Dict[str, Any]:
        return await self._send({"op": op, "params": params})

    async def get(self, path: str, depth: int = -1) -> Any:
        """Export node subtree or value (default depth=-1 returns full tree)."""
        try:
            reply = await self._op("export", path=_normalize_path(path), depth=depth)
            if not _ok(reply):
                if reply.get("code") == CODE_NOT_FOUND:
                    return None
                raise _reply_error(reply)
            data = reply.get("data", {})
            if isinstance(data, dict):
                return data.get("tree") if data.get("tree") is not None else data.get("value")
            return data
        except httpx.HTTPStatusError as exc:
            if exc.response.status_code == 404:
                return None
            raise

    async def set(self, path: str, tree: Any) -> bool:
        """Import a subtree at path (merge)."""
        reply = await self._op("import", path=_normalize_path(path), tree=tree)
        return _ok(reply)

    async def val(self, path: str, value: Any = _UNSET) -> Any:
        """Get or set a single node value."""
        if value is _UNSET:
            try:
                reply = await self._op("get", path=_normalize_path(path))
                if not _ok(reply):
                    if reply.get("code") == CODE_NOT_FOUND:
                        return None
                    raise _reply_error(reply)
                data = reply.get("data", {})
                return data.get("value") if isinstance(data, dict) else data
            except httpx.HTTPStatusError as exc:
                if exc.response.status_code == 404:
                    return None
                raise
        else:
            reply = await self._op("set", path=_normalize_path(path), value=value)
            return _ok(reply)

    async def trigger(self, path: str) -> bool:
        try:
            reply = await self._op("trigger", path=_normalize_path(path))
            return _ok(reply)
        except Exception:
            return False

    async def rm(self, path: str) -> bool:
        reply = await self._op("erase", path=_normalize_path(path))
        return _ok(reply)

    async def list(self, path: str) -> List[Dict]:
        reply = await self._op("children", path=_normalize_path(path))
        if not _ok(reply):
            return []
        data = reply.get("data", {})
        if isinstance(data, dict):
            return data.get("children", [])
        return []

    async def tree(self, path: str) -> Dict:
        p = f"/{_normalize_path(path)}" if path and path != "/" else ""
        resp = await self._request("GET", f"/at{p}")
        return resp.json()

    async def command(self, name: str, args: Optional[Dict] = None) -> Any:
        reply = await self._send({"cmd": name, "params": _command_params(args)})
        if not _ok(reply):
            raise _reply_error(reply)
        return reply.get("data")

    async def cmds(self) -> List[Dict]:
        reply = await self._op("commands")
        if not _ok(reply):
            return []
        data = reply.get("data", {})
        if isinstance(data, dict):
            return data.get("commands", [])
        return data if isinstance(data, list) else []

    async def batch(self, items: List[Dict]) -> List[Any]:
        reply = await self._send({"batch": items})
        if not _ok(reply):
            raise _reply_error(reply)
        data = reply.get("data", [])
        return data if isinstance(data, list) else []

    async def ping(self) -> bool:
        try:
            resp = await self._request("GET", "/health")
            return resp.json().get("status") == "ok"
        except Exception:
            return False

    async def close(self):
        await self.client.aclose()


class AsyncJsonRpcTransport(AsyncTransport):
    """Async JSON-RPC 2.0 transport using httpx.

    The JSON-RPC `method` is the v2.1 op name (or a command name); the result
    is the op's `data` payload.
    """

    def __init__(self, base_url: str, timeout: int = 30):
        if httpx is None:
            raise ImportError("httpx required: pip install httpx")
        self.url = base_url.rstrip("/") + "/jsonrpc"
        self.timeout = timeout
        self._id = 0
        self.client = httpx.AsyncClient(timeout=timeout)

    async def _call(self, method: str, params: Optional[Dict] = None) -> Any:
        self._id += 1
        payload = {
            "jsonrpc": "2.0",
            "method": method,
            "params": params or {},
            "id": self._id
        }

        resp = await self.client.post(self.url, json=payload,
                                       headers={"Content-Type": "application/json"})
        resp.raise_for_status()
        result = resp.json()

        if "error" in result:
            err = result["error"]
            raise RpcError(err.get("code"), err.get("message"))

        return result.get("result")

    async def get(self, path: str, depth: int = -1) -> Any:
        """Export node subtree or value (default depth=-1 returns full tree)."""
        try:
            data = await self._call("export", {"path": _normalize_path(path), "depth": depth})
        except RpcError as exc:
            if exc.code == CODE_NOT_FOUND:
                return None
            raise
        if isinstance(data, dict):
            return data.get("tree") if data.get("tree") is not None else data.get("value")
        return data

    async def set(self, path: str, tree: Any) -> bool:
        """Import a subtree at path (merge)."""
        try:
            await self._call("import", {"path": _normalize_path(path), "tree": tree})
            return True
        except RpcError:
            return False

    async def val(self, path: str, value: Any = _UNSET) -> Any:
        """Get or set a single node value."""
        if value is _UNSET:
            try:
                data = await self._call("get", {"path": _normalize_path(path)})
            except RpcError as exc:
                if exc.code == CODE_NOT_FOUND:
                    return None
                raise
            return data.get("value") if isinstance(data, dict) else data
        else:
            try:
                await self._call("set", {"path": _normalize_path(path), "value": value})
                return True
            except RpcError:
                return False

    async def trigger(self, path: str) -> bool:
        try:
            await self._call("trigger", {"path": _normalize_path(path)})
            return True
        except RpcError:
            return False

    async def rm(self, path: str) -> bool:
        try:
            await self._call("erase", {"path": _normalize_path(path)})
            return True
        except RpcError:
            return False

    async def list(self, path: str) -> List[Dict]:
        data = await self._call("children", {"path": _normalize_path(path)})
        return data.get("children", []) if isinstance(data, dict) else []

    async def tree(self, path: str) -> Dict:
        data = await self._call("export", {"path": _normalize_path(path), "depth": -1})
        if isinstance(data, dict):
            return data.get("tree", data.get("value", {}))
        return data

    async def command(self, name: str, args: Optional[Dict] = None) -> Any:
        return await self._call(name, _command_params(args))

    async def cmds(self) -> List[Dict]:
        data = await self._call("commands", {})
        if isinstance(data, dict):
            return data.get("commands", [])
        return data if isinstance(data, list) else []

    async def ping(self) -> bool:
        try:
            await self._call("get", {"path": "/"})
            return True
        except Exception:
            return False

    async def close(self):
        await self.client.aclose()
