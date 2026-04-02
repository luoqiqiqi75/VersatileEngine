# VersatileEngine Python Client

Python client library for VersatileEngine with multiple transport options.

## Installation

```bash
# Default (TCP JSON, pure stdlib, no dependencies)
pip install ve-client

# With HTTP support
pip install ve-client[http]

# With MessagePack support (high-performance)
pip install ve-client[msgpack]

# All transports
pip install ve-client[all]
```

## Quick Start

```python
from ve_client import VeClient

# TCP JSON (default, pure stdlib, persistent connection)
client = VeClient()

# Get node value
value = client.get("/config/port")

# Set node value
client.set("/test", 42)

# List children
children = client.list("/")
```

## Transports

### TCP JSON (Default)
- **Port**: 5082
- **Protocol**: Newline-delimited JSON over TCP
- **Dependencies**: None (pure stdlib)
- **Features**: Persistent connection, subscribe support
- **Use case**: Default choice, embedded systems, scripts

```python
client = VeClient()  # tcp://localhost:5082
client = VeClient("tcp://localhost:5082")
```

### HTTP REST
- **Port**: 5080
- **Protocol**: REST API
- **Dependencies**: `requests`
- **Features**: Stateless, curl-compatible
- **Use case**: One-off queries, debugging

```python
client = VeClient("http://localhost:5080", transport="http")
```

### JSON-RPC 2.0
- **Port**: 5080
- **Protocol**: JSON-RPC 2.0 over HTTP
- **Dependencies**: `requests`
- **Features**: Standard protocol, library support
- **Use case**: Standard JSON-RPC clients

```python
client = VeClient("http://localhost:5080", transport="jsonrpc")
```

### MessagePack Binary
- **Port**: 5065
- **Protocol**: Frame-based MessagePack over TCP
- **Dependencies**: `msgpack`
- **Features**: Highest performance, smallest payload
- **Use case**: High-frequency data, performance-critical

```python
client = VeClient("tcp://localhost:5065", transport="msgpack")
```

## API

```python
client.get(path: str) -> Any
client.set(path: str, value: Any) -> bool
client.list(path: str) -> List[Dict]
client.tree(path: str) -> Dict
client.ping() -> bool
client.close()
```

## Benchmarks

Run benchmarks to compare transports:

```bash
python examples/benchmark.py
```

Typical results (localhost):
```
Latency (1000 get operations):
  HTTP REST        45.2 ms/op
  JSON-RPC         42.1 ms/op
  TCP JSON         12.3 ms/op  <- default
  MessagePack       8.7 ms/op  <- fastest

Speedup: 5.2x (MessagePack vs HTTP)
```

## Examples

See `examples/` directory:
- `simple_test.py` - Basic usage
- `test_client.py` - All transports
- `benchmark.py` - Performance comparison
