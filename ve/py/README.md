# VersatileEngine Python Client

Python client library for VE with multiple transports.

## Installation

```bash
# Default (TCP JSON, pure stdlib)
pip install ve-client

# HTTP / JSON-RPC
pip install ve-client[http]

# MessagePack
pip install ve-client[msgpack]

# All transports
pip install ve-client[all]
```

## Quick Start

```python
from ve_client import VeClient

client = VeClient()  # tcp://localhost:12200

value = client.get("ve/server/node/http/runtime/port")
client.set("test/value", 42)
children = client.list("ve/server")
tree = client.tree("ve/server")
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

## Examples

See `examples/`:

- `simple_test.py`
- `test_client.py`
- `benchmark.py`
