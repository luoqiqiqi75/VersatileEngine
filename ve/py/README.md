# VersatileEngine Python Client

Python client library for VE with multiple transports.

## Installation

```bash
# Default (TCP JSON, pure stdlib)
pip install ve-client

# HTTP / JSON-RPC (sync)
pip install ve-client[http]

# Async support (for FastAPI, asyncio apps)
pip install ve-client[async]

# MessagePack
pip install ve-client[msgpack]

# All transports
pip install ve-client[all]
```

## Quick Start

### Sync Client

```python
from ve_client import VeClient

client = VeClient()  # tcp://localhost:12200

value = client.get("ve/server/node/http/runtime/port")
client.set("test/value", 42)
children = client.list("ve/server")
tree = client.tree("ve/server")
```

### Async Client (FastAPI, asyncio)

```python
from ve_client import AsyncVeClient

# Context manager (recommended)
async with AsyncVeClient("http://localhost:12000") as client:
    value = await client.get("/config")
    await client.set("/test", 42)
    children = await client.list("/")

# Manual close
client = AsyncVeClient("http://localhost:12000")
value = await client.get("/config")
await client.close()
```

## Transport Overview

### TCP JSON (default)

- Port: `12200`
- Protocol: newline-delimited JSON envelope
- Features: persistent connection, subscribe support

```python
client = VeClient()
client = VeClient("tcp://localhost:12200")
```

### HTTP

- Port: `12000`
- Protocol: `POST /ve` + convenience `GET/PUT /at/*`
- Features: stateless, curl-friendly

```python
client = VeClient("http://localhost:12000", transport="http")
```

### JSON-RPC 2.0

- Port: `12000`
- Protocol: `POST /jsonrpc`
- Features: standard JSON-RPC clients

```python
client = VeClient("http://localhost:12000", transport="jsonrpc")
```

### MessagePack Binary

- Port: `11000`
- Protocol: frame-based MessagePack with the same VE envelope semantics
- Features: lowest overhead, subscribe support

```python
client = VeClient("tcp://localhost:11000", transport="msgpack")
```

## Core API

```python
client.get(path: str) -> Any
client.set(path: str, value: Any) -> bool
client.trigger(path: str) -> bool
client.list(path: str) -> List[Dict]
client.tree(path: str) -> Dict
client.command(name: str, args: Optional[Dict]) -> Any
client.ping() -> bool
client.close()
```

## HTTP / `/ve`

HTTP transport internally maps these calls to the VE native protocol:

- `get()` -> `node.get`
- `set()` -> `node.set`
- `trigger()` -> `node.trigger`
- `list()` -> `node.list`
- `tree()` -> `GET /at/<path>`
- `command()` -> `command.run`

## Subscribe

`subscribe()` is available on:

- TCP JSON
- MessagePack

Example:

```python
def on_change(path, value):
    print(path, value)

client = VeClient("tcp://localhost:12200")
unsub = client.subscribe("ve/server/node/http/runtime/port", on_change)
unsub()
```

## FastAPI Integration

```python
from fastapi import FastAPI
from ve_client import AsyncVeClient

app = FastAPI()
ve_client = AsyncVeClient("http://localhost:12000")

@app.get("/ve/{path:path}")
async def get_node(path: str):
    return await ve_client.get(path)

@app.on_event("shutdown")
async def shutdown():
    await ve_client.close()
```

See `examples/fastapi_example.py` for a complete FastAPI integration example.

## Examples

See `examples/`:

- `simple_test.py` - Basic sync client usage
- `test_client.py` - All transport types
- `benchmark.py` - Performance testing
- `fastapi_example.py` - FastAPI integration with async client
