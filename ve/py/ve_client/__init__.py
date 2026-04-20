"""
VersatileEngine Python Client Library

Usage:
    # Sync client
    from ve_client import VeClient

    client = VeClient()                    # TCP JSON (default)
    client = VeClient("http://localhost:12000")  # HTTP /ve + /at
    client = VeClient("tcp://localhost:11000", transport="msgpack")  # MsgPack

    client.get("/config")
    client.set("/test", 42)
    client.trigger("/test")
    client.list("/")

    # Subscribe (TCP JSON and MsgPack only)
    unsub = client.subscribe("/test", lambda path, value: print(f"{path} = {value}"))
    unsub()  # unsubscribe

    # Async client (for FastAPI, asyncio apps)
    from ve_client import AsyncVeClient

    async with AsyncVeClient("http://localhost:12000") as client:
        value = await client.get("/config")
        await client.set("/test", 42)
        children = await client.list("/")
"""

from .client import VeClient
from .types import VarValue, NodeResponse
from .transports import NotifyCallback

try:
    from .async_client import AsyncVeClient
    __all__ = ["VeClient", "AsyncVeClient", "VarValue", "NodeResponse", "NotifyCallback"]
except ImportError:
    # httpx not installed, async client not available
    __all__ = ["VeClient", "VarValue", "NodeResponse", "NotifyCallback"]

__version__ = "0.4.0"
