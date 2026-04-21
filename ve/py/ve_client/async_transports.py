"""Async transport implementations for VeClient."""

import json
import asyncio
import struct
from abc import ABC, abstractmethod
from typing import Any, Dict, List, Optional, Callable

try:
    import httpx
except ImportError:
    httpx = None

try:
    import msgpack
except ImportError:
    msgpack = None


NotifyCallback = Callable[[str, Any], None]


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


_UNSET = object()


class AsyncTransport(ABC):
    """Abstract async transport for VE Node operations."""

    @abstractmethod
    async def get(self, path: str, depth: int = -1) -> Any:
        """Get node tree or value (default depth=-1 returns full tree)."""
        pass

    @abstractmethod
    async def set(self, path: str, tree: Any) -> bool:
        """Set node tree structure (node.put)."""
        pass

    @abstractmethod
    async def val(self, path: str, value: Any = _UNSET) -> Any:
        """Get or set single node value (node.get/node.set)."""
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
        """Remove node at path (node.remove)."""
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

    async def _call(self, op: str, **payload) -> Dict[str, Any]:
        resp = await self._request("POST", "/ve",
                                    json={"op": op, **payload},
                                    headers={"Content-Type": "application/json"})
        return resp.json()

    async def get(self, path: str, depth: int = -1) -> Any:
        """Get node tree or value (default depth=-1 returns full tree)."""
        try:
            reply = await self._call("node.get", path=_normalize_path(path), depth=depth)
            if not reply.get("ok"):
                if reply.get("code") == "not_found":
                    return None
                raise _reply_error(reply)
            data = reply.get("data", {})
            if isinstance(data, dict):
                return data.get("tree") or data.get("value")
            return data
        except httpx.HTTPStatusError as exc:
            if exc.response.status_code == 404:
                return None
            raise

    async def set(self, path: str, tree: Any) -> bool:
        """Set node tree structure (node.put)."""
        reply = await self._call("node.put", path=_normalize_path(path), tree=tree)
        return bool(reply.get("ok"))

    async def val(self, path: str, value: Any = _UNSET) -> Any:
        """Get or set single node value (node.get/node.set)."""
        if value is _UNSET:
            try:
                reply = await self._call("node.get", path=_normalize_path(path))
                if not reply.get("ok"):
                    if reply.get("code") == "not_found":
                        return None
                    raise _reply_error(reply)
                data = reply.get("data", {})
                return data.get("value") if isinstance(data, dict) else data
            except httpx.HTTPStatusError as exc:
                if exc.response.status_code == 404:
                    return None
                raise
        else:
            reply = await self._call("node.set", path=_normalize_path(path), value=value)
            return bool(reply.get("ok"))

    async def trigger(self, path: str) -> bool:
        try:
            reply = await self._call("node.trigger", path=_normalize_path(path))
            return bool(reply.get("ok"))
        except Exception:
            return False

    async def rm(self, path: str) -> bool:
        reply = await self._call("node.remove", path=_normalize_path(path))
        return bool(reply.get("ok"))

    async def list(self, path: str) -> List[Dict]:
        reply = await self._call("node.list", path=_normalize_path(path))
        if not reply.get("ok"):
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
        reply = await self._call("command.run", name=name, **_command_payload(args))
        if not reply.get("ok"):
            raise _reply_error(reply)
        if reply.get("accepted"):
            return {"accepted": True, "task_id": reply.get("task_id")}
        return reply.get("data")

    async def cmds(self) -> List[str]:
        reply = await self._call("command.list")
        if not reply.get("ok"):
            return []
        data = reply.get("data", {})
        return data if isinstance(data, list) else []

    async def batch(self, items: List[Dict]) -> List[Any]:
        reply = await self._call("batch", items=items)
        if not reply.get("ok"):
            raise _reply_error(reply)
        data = reply.get("data", {})
        return data.get("items", []) if isinstance(data, dict) else []

    async def ping(self) -> bool:
        try:
            resp = await self._request("GET", "/health")
            return resp.json().get("status") == "ok"
        except Exception:
            return False

    async def close(self):
        await self.client.aclose()


class AsyncJsonRpcTransport(AsyncTransport):
    """Async JSON-RPC 2.0 transport using httpx."""

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
            raise RuntimeError(f"JSON-RPC error {err.get('code')}: {err.get('message')}")

        return result.get("result")

    async def get(self, path: str, depth: int = -1) -> Any:
        """Get node tree or value (default depth=-1 returns full tree)."""
        try:
            data = await self._call("node.get", {"path": _normalize_path(path), "depth": depth})
        except RuntimeError as exc:
            if "not_found" in str(exc):
                return None
            raise
        if isinstance(data, dict):
            return data.get("tree") or data.get("value")
        return data

    async def set(self, path: str, tree: Any) -> bool:
        """Set node tree structure (node.put)."""
        data = await self._call("node.put", {"path": _normalize_path(path), "tree": tree})
        return isinstance(data, dict) and "path" in data

    async def val(self, path: str, value: Any = _UNSET) -> Any:
        """Get or set single node value (node.get/node.set)."""
        if value is _UNSET:
            try:
                data = await self._call("node.get", {"path": _normalize_path(path)})
            except RuntimeError as exc:
                if "not_found" in str(exc):
                    return None
                raise
            return data.get("value") if isinstance(data, dict) else data
        else:
            data = await self._call("node.set", {"path": _normalize_path(path), "value": value})
            return isinstance(data, dict) and "path" in data

    async def trigger(self, path: str) -> bool:
        data = await self._call("node.trigger", {"path": _normalize_path(path)})
        return isinstance(data, dict) and "path" in data

    async def rm(self, path: str) -> bool:
        data = await self._call("node.remove", {"path": _normalize_path(path)})
        return isinstance(data, dict) and "path" in data

    async def list(self, path: str) -> List[Dict]:
        data = await self._call("node.list", {"path": _normalize_path(path)})
        return data.get("children", []) if isinstance(data, dict) else []

    async def tree(self, path: str) -> Dict:
        data = await self._call("node.get", {"path": _normalize_path(path), "depth": -1})
        if isinstance(data, dict):
            return data.get("tree", data.get("value", {}))
        return data

    async def command(self, name: str, args: Optional[Dict] = None) -> Any:
        data = await self._call("command.run", {"name": name, **_command_payload(args)})
        return data

    async def cmds(self) -> List[str]:
        data = await self._call("command.list", {})
        return data if isinstance(data, list) else []

    async def batch(self, items: List[Dict]) -> List[Any]:
        data = await self._call("batch", {"items": items})
        return data.get("items", []) if isinstance(data, dict) else []

    async def ping(self) -> bool:
        try:
            await self._call("node.get", {"path": "/"})
            return True
        except Exception:
            return False

    async def close(self):
        await self.client.aclose()
