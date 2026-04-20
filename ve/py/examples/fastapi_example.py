"""FastAPI integration example with AsyncVeClient.

Usage:
    pip install ve-client[async] fastapi uvicorn
    python fastapi_example.py

Then visit:
    http://localhost:8000/docs
    http://localhost:8000/ve/config
    http://localhost:8000/ve/search?pattern=config
"""

from fastapi import FastAPI, HTTPException
from typing import Any, Dict, List, Optional
from ve_client import AsyncVeClient

app = FastAPI(title="VE FastAPI Example")

# Global client (reuse connection)
ve_client = AsyncVeClient("http://localhost:12000")


@app.on_event("shutdown")
async def shutdown():
    """Close VE client on shutdown."""
    await ve_client.close()


@app.get("/ve/{path:path}")
async def get_node(path: str) -> Any:
    """Get node value at path."""
    value = await ve_client.get(path)
    if value is None:
        raise HTTPException(status_code=404, detail="Node not found")
    return {"path": path, "value": value}


@app.put("/ve/{path:path}")
async def set_node(path: str, value: Any) -> Dict:
    """Set node value at path."""
    success = await ve_client.set(path, value)
    if not success:
        raise HTTPException(status_code=500, detail="Failed to set node")
    return {"path": path, "value": value, "success": True}


@app.get("/ve-list/{path:path}")
async def list_children(path: str) -> List[Dict]:
    """List children at path."""
    return await ve_client.list(path)


@app.get("/ve-tree/{path:path}")
async def get_tree(path: str) -> Dict:
    """Get subtree as dict."""
    return await ve_client.tree(path)


@app.post("/ve-command/{name}")
async def run_command(name: str, args: Optional[Dict] = None) -> Any:
    """Run a VE command."""
    try:
        result = await ve_client.command(name, args)
        return {"command": name, "result": result}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))


@app.get("/ve/search")
async def search_nodes(pattern: str, root: str = "/", key: bool = True, top: int = 10) -> List[str]:
    """Search nodes by pattern."""
    args = {
        "args": [pattern, root, "--key" if key else "--value", "--top", str(top)]
    }
    try:
        result = await ve_client.command("search", args)
        return result if isinstance(result, list) else []
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))


@app.get("/health")
async def health_check() -> Dict:
    """Health check endpoint."""
    ve_ok = await ve_client.ping()
    return {
        "status": "ok" if ve_ok else "degraded",
        "ve_connected": ve_ok
    }


if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=8000)
