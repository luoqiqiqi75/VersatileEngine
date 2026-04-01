"""Type definitions for VE client."""

from typing import Any, Dict, List, Union

VarValue = Union[None, bool, int, float, str, bytes, List[Any], Dict[str, Any]]

class NodeResponse:
    """Response from node.get operation."""
    def __init__(self, data: Dict[str, Any]):
        self.path = data.get("path", "")
        self.value = data.get("value")
        self.type = data.get("type", "NONE")

    def __repr__(self):
        return f"NodeResponse(path={self.path!r}, value={self.value!r}, type={self.type})"
