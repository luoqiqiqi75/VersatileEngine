"""Transport implementations for VeClient."""

import json
from abc import ABC, abstractmethod
from typing import Any, Dict, List, Optional
import requests


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

    def _request(self, method: str, path: str, **kwargs) -> requests.Response:
        resp = requests.request(method, f"{self.base}{path}",
                                timeout=self.timeout, **kwargs)
        resp.raise_for_status()
        return resp

    def get(self, path: str) -> Any:
        p = f"/{path}" if path and path != "/" else ""
        resp = self._request("GET", f"/api/node{p}")
        data = resp.json()
        return data.get("value")

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

    def _call(self, method: str, params: Optional[Dict] = None) -> Any:
        self._id += 1
        payload = {
            "jsonrpc": "2.0",
            "method": method,
            "params": params or {},
            "id": self._id
        }

        resp = requests.post(self.url, json=payload, timeout=self.timeout,
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
