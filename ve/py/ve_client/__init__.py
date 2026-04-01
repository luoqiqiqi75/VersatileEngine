"""
VersatileEngine Python Client Library

Usage:
    from ve_client import VeClient

    client = VeClient("http://localhost:5080")
    client.get("/config")
    client.set("/test", 42)
    client.list("/")
"""

from .client import VeClient
from .types import VarValue, NodeResponse

__version__ = "0.2.0"
__all__ = ["VeClient", "VarValue", "NodeResponse"]
