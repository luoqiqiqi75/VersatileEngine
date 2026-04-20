"""Unified VersatileEngine client."""

from typing import Any, Callable, Dict, List, Optional
from .transports import (
    Transport, HttpRestTransport, JsonRpcTransport,
    TcpJsonTransport, MsgPackTransport, NotifyCallback
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
        client = VeClient("tcp://localhost:12200")

        # HTTP native protocol (/ve + /at)
        client = VeClient("http://localhost:12000")

        # JSON-RPC
        client = VeClient("http://localhost:12000", transport="jsonrpc")

        # MessagePack (high-performance)
        client = VeClient("tcp://localhost:11000", transport="msgpack")

        # Operations
        client.get("/config/port")
        client.set("/test", 42)
        client.trigger("/test")
        client.list("/")

        # Subscribe (TCP JSON and MsgPack only)
        unsub = client.subscribe("/test", lambda path, value: print(f"{path} = {value}"))
        unsub()  # unsubscribe

        # Command
        client.command("ros/topic/list", {"args": []})
    """

    def __init__(self, url: str = "tcp://localhost:12200",
                 transport: str = None, timeout: int = 30):
        if transport is None:
            transport = self._detect_transport(url)

        self._transport = self._create_transport(transport, url, timeout)

    @staticmethod
    def _detect_transport(url: str) -> str:
        scheme, _, _ = parse_url(url)
        if scheme in ("http", "https"):
            return "http"
        return "tcp"

    @staticmethod
    def _create_transport(name: str, url: str, timeout: int) -> Transport:
        scheme, host, port = parse_url(url)

        if name == "tcp":
            port = port or 12200
            return TcpJsonTransport(host, port, timeout)
        elif name == "msgpack":
            port = port or 11000
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

    def trigger(self, path: str) -> bool:
        """Trigger NODE_CHANGED on node (re-fire signal without changing value)."""
        return self._transport.trigger(path)

    def list(self, path: str = "/") -> List[Dict]:
        """List children at path."""
        return self._transport.list(path)

    def tree(self, path: str = "/") -> Dict:
        """Get subtree as dict."""
        return self._transport.tree(path)

    def command(self, name: str, args: Optional[Dict] = None) -> Any:
        """Run a command."""
        return self._transport.command(name, args)

    def subscribe(self, path: str, callback: NotifyCallback) -> Callable[[], None]:
        """Subscribe to node changes. Returns an unsubscribe function.

        callback(path: str, value: Any) is called on each NODE_CHANGED event.
        Supported on TCP JSON and MsgPack transports.
        """
        return self._transport.subscribe(path, callback)

    def unsubscribe(self, path: str) -> None:
        """Remove all subscriptions for a path."""
        self._transport.unsubscribe(path)

    def ping(self) -> bool:
        """Test connection."""
        return self._transport.ping()

    def close(self):
        """Close connection."""
        if hasattr(self._transport, 'close'):
            self._transport.close()
