"""Unified VersatileEngine client."""

from typing import Any, Dict, List, Optional
from .transports import Transport, HttpRestTransport, JsonRpcTransport


class VeClient:
    """VersatileEngine client supporting multiple transports.

    Usage:
        # REST API (default)
        client = VeClient("http://localhost:8080")

        # JSON-RPC
        client = VeClient("http://localhost:8080", transport="jsonrpc")

        # Operations
        client.get("/config/port")
        client.set("/test", 42)
        client.list("/")
        client.command("ls", {"path": "/"})
    """

    def __init__(self, base_url: str = "http://localhost:8080",
                 transport: str = "http", timeout: int = 30):
        """
        Args:
            base_url: Server base URL (e.g. "http://localhost:8080")
            transport: "http" (REST) or "jsonrpc" (JSON-RPC 2.0)
            timeout: Request timeout in seconds
        """
        self._transport = self._create_transport(transport, base_url, timeout)

    @staticmethod
    def _create_transport(name: str, base_url: str, timeout: int) -> Transport:
        if name == "http":
            return HttpRestTransport(base_url, timeout)
        elif name == "jsonrpc":
            return JsonRpcTransport(base_url, timeout)
        else:
            raise ValueError(f"Unknown transport: {name!r}. Use 'http' or 'jsonrpc'.")

    def get(self, path: str = "/") -> Any:
        """Get node value at path."""
        return self._transport.get(path)

    def set(self, path: str, value: Any) -> bool:
        """Set node value at path."""
        return self._transport.set(path, value)

    def list(self, path: str = "/") -> List[Dict]:
        """List children at path."""
        return self._transport.list(path)

    def tree(self, path: str = "/") -> Dict:
        """Get subtree as dict."""
        return self._transport.tree(path)

    def command(self, name: str, args: Optional[Dict] = None) -> Any:
        """Run a command."""
        return self._transport.command(name, args)

    def ping(self) -> bool:
        """Test connection."""
        return self._transport.ping()
