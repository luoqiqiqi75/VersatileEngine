"""Async VersatileEngine client."""

from typing import Any, Callable, Dict, List, Optional
from .async_transports import (
    AsyncTransport, AsyncHttpRestTransport, AsyncJsonRpcTransport
)


def parse_url(url: str) -> tuple:
    """Parse URL into (scheme, host, port)."""
    if "://" in url:
        scheme, rest = url.split("://", 1)
    else:
        scheme = "http"
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


class AsyncVeClient:
    """Async VersatileEngine client supporting multiple transports.

    Usage:
        # HTTP native protocol (/ve + /at)
        client = AsyncVeClient()
        client = AsyncVeClient("http://localhost:12000")

        # JSON-RPC
        client = AsyncVeClient("http://localhost:12000", transport="jsonrpc")

        # Operations
        value = await client.get("/config/port")
        await client.set("/test", 42)
        await client.trigger("/test")
        children = await client.list("/")
        tree = await client.tree("/")

        # Command
        result = await client.command("search", {"args": ["config"]})

        # Close
        await client.close()
    """

    def __init__(self, url: str = "http://localhost:12000",
                 transport: str = None, timeout: int = 30):
        if transport is None:
            transport = self._detect_transport(url)

        self._transport = self._create_transport(transport, url, timeout)

    @staticmethod
    def _detect_transport(url: str) -> str:
        scheme, _, _ = parse_url(url)
        if scheme in ("http", "https"):
            return "http"
        raise ValueError(f"Unsupported scheme for async client: {scheme}")

    @staticmethod
    def _create_transport(name: str, url: str, timeout: int) -> AsyncTransport:
        scheme, host, port = parse_url(url)

        if name == "http":
            base_url = url if "://" in url else f"http://{url}"
            return AsyncHttpRestTransport(base_url, timeout)
        elif name == "jsonrpc":
            base_url = url if "://" in url else f"http://{url}"
            return AsyncJsonRpcTransport(base_url, timeout)
        else:
            raise ValueError(f"Unknown async transport: {name!r}")

    async def get(self, path: str = "/") -> Any:
        """Get node value at path."""
        return await self._transport.get(path)

    async def set(self, path: str, value: Any) -> bool:
        """Set node value at path."""
        return await self._transport.set(path, value)

    async def trigger(self, path: str) -> bool:
        """Trigger NODE_CHANGED on node (re-fire signal without changing value)."""
        return await self._transport.trigger(path)

    async def list(self, path: str = "/") -> List[Dict]:
        """List children at path."""
        return await self._transport.list(path)

    async def tree(self, path: str = "/") -> Dict:
        """Get subtree as dict."""
        return await self._transport.tree(path)

    async def command(self, name: str, args: Optional[Dict] = None) -> Any:
        """Run a command."""
        return await self._transport.command(name, args)

    async def ping(self) -> bool:
        """Test connection."""
        return await self._transport.ping()

    async def close(self):
        """Close connection."""
        if hasattr(self._transport, 'close'):
            await self._transport.close()

    async def __aenter__(self):
        """Async context manager entry."""
        return self

    async def __aexit__(self, exc_type, exc_val, exc_tb):
        """Async context manager exit."""
        await self.close()
