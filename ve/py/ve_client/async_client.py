"""Async VersatileEngine client."""

from typing import Any, Callable, Dict, List, Optional
from .async_transports import (
    AsyncTransport, AsyncHttpRestTransport, AsyncJsonRpcTransport, _UNSET
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

        # Operations (aligned with JS veservice.js)
        tree = await client.get("/config")           # Get tree (depth=-1)
        value = await client.val("/config/port")     # Get single value
        await client.val("/test", 42)                # Set single value
        await client.set("/config", {"port": 8080})  # Set tree structure
        await client.trigger("/test")                # Trigger NODE_CHANGED
        children = await client.list("/")            # List children

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

    async def get(self, path: str = "/", depth: int = -1) -> Any:
        """Get node tree or value (default depth=-1 returns full tree)."""
        return await self._transport.get(path, depth)

    async def set(self, path: str, tree: Any) -> bool:
        """Set node tree structure (node.put)."""
        return await self._transport.set(path, tree)

    async def val(self, path: str, value: Any = _UNSET) -> Any:
        """Get or set single node value (node.get/node.set).

        val(path) -> returns current value
        val(path, value) -> sets value, returns success status
        """
        return await self._transport.val(path, value)

    async def trigger(self, path: str) -> bool:
        """Trigger NODE_CHANGED on node (re-fire signal without changing value)."""
        return await self._transport.trigger(path)

    async def rm(self, path: str) -> bool:
        """Remove node at path."""
        return await self._transport.rm(path)

    async def list(self, path: str = "/") -> List[Dict]:
        """List children at path."""
        return await self._transport.list(path)

    async def tree(self, path: str = "/") -> Dict:
        """Get subtree as dict."""
        return await self._transport.tree(path)

    async def command(self, name: str, args: Optional[Dict] = None) -> Any:
        """Run a command."""
        return await self._transport.command(name, args)

    async def cmds(self) -> List[str]:
        """List available commands."""
        return await self._transport.cmds()

    async def batch(self, items: List[Dict]) -> List[Any]:
        """Execute batch operations."""
        return await self._transport.batch(items)

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
