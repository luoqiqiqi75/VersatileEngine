"""
VersatileEngine Python Client Library

Usage:
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
"""

from .client import VeClient
from .types import VarValue, NodeResponse
from .transports import NotifyCallback

__version__ = "0.3.0"
__all__ = ["VeClient", "VarValue", "NodeResponse", "NotifyCallback"]
