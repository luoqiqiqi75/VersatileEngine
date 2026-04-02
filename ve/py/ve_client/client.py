"""Unified VersatileEngine client."""

from typing import Any, Dict, List, Optional
from .transports import (
    Transport, HttpRestTransport, JsonRpcTransport,
    TcpJsonTransport, MsgPackTransport
)


def parse_url(url: str) -> tuple:
    """Parse URL into (scheme, host, port)."""
    if "://" in url:
        scheme, rest = url.split("://", 1)
    else:
        scheme = "tcp"
        rest = url

    if ":" in rest:
        host, port_str = rest.rsplit(":", 1)
        try:
            port = int(port_str)
        except ValueError:
            host = rest
            port = None
    else:
        host = rest
        port = None

    return scheme, host, port


class VeClient:
    """VersatileEngine client supporting multiple transports.

    Usage:
        # TCP JSON (default, pure stdlib)
        client = VeClient()
        client = VeClient("tcp://localhost:5082")

        # HTTP REST
        client = VeClient("http://localhost:5080")

        # JSON-RPC
        client = VeClient("http://localhost:5080", transport="jsonrpc")

        # MessagePack (high-performance)
        client = VeClient("tcp://localhost:5065", transport="msgpack")

        # Operations
        client.get("/config/port")
        client.set("/test", 42)
        client.list("/")
    """

    def __init__(self, url: str = "tcp://localhost:5082",
                 transport: str = None, timeout: int = 30):
        """
        Args:
            url: Server URL (tcp://host:port, http://host:port)
            transport: Override transport ("tcp", "http", "jsonrpc", "msgpack")
            timeout: Request timeout in seconds
        """
        if transport is None:
            transport = self._detect_transport(url)

        self._transport = self._create_transport(transport, url, timeout)

    @staticmethod
    def _detect_transport(url: str) -> str:
        scheme, _, _ = parse_url(url)
        if scheme in ("http", "https"):
            return "http"
        elif scheme == "tcp":
            return "tcp"
        else:
            return "tcp"

    @staticmethod
    def _create_transport(name: str, url: str, timeout: int) -> Transport:
        scheme, host, port = parse_url(url)

        if name == "tcp":
            port = port or 5082
            return TcpJsonTransport(host, port, timeout)
        elif name == "msgpack":
            port = port or 5065
            return MsgPackTransport(host, port, timeout)
        elif name == "http":
            base_url = url if "://" in url else f"http://{url}"
            return HttpRestTransport(base_url, timeout)
        elif name == "jsonrpc":
            base_url = url if "://" in url else f"http://{url}"
            return JsonRpcTransport(base_url, timeout)
        else:
            raise ValueError(f"Unknown transport: {name!r}")

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

    def close(self):
        """Close connection."""
        if hasattr(self._transport, 'close'):
            self._transport.close()
