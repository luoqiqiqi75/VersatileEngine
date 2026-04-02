"""Complete VE Client Examples - All transports, all operations.

Requires: ve.exe running with all services enabled.
"""

import sys
import time
sys.stdout.reconfigure(encoding='utf-8')

from ve_client import VeClient


def test_transport(name, client):
    """Test all operations on a transport."""
    print(f"\n{'=' * 50}")
    print(f"  {name}")
    print(f"{'=' * 50}")

    # Ping
    ok = client.ping()
    print(f"  ping:     {'OK' if ok else 'FAIL'}")
    if not ok:
        return False

    # Set
    ok = client.set("/test/x", 42)
    print(f"  set:      {'OK' if ok else 'FAIL'}")

    # Get
    val = client.get("/test/x")
    print(f"  get:      {val}")

    # Set various types
    client.set("/test/list", [1, 2, 3])
    client.set("/test/dict", {"a": 1, "b": 2})

    print(f"  list:     {client.get('/test/list')}")
    print(f"  dict:     {client.get('/test/dict')}")

    # List
    children = client.list("/test")
    print(f"  list:     {children}")

    # Tree
    tree = client.tree("/test")
    print(f"  tree:     {tree}")

    # Get non-existent node
    val = client.get("/does/not/exist")
    print(f"  missing:  {val}")

    return True


def test_subscribe_tcp():
    """Test subscribe via TCP JSON (push notifications)."""
    print(f"\n{'=' * 50}")
    print(f"  Subscribe (TCP JSON)")
    print(f"{'=' * 50}")

    import socket, json

    # Subscriber connection
    sub = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sub.connect(("localhost", 5082))
    sub.settimeout(3)

    # Writer connection (separate)
    wr = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    wr.connect(("localhost", 5082))
    wr.settimeout(3)

    # Subscribe
    sub.sendall(b'{"cmd":"subscribe","path":"/test/watch","id":1}\n')
    time.sleep(0.1)
    sub.recv(4096)  # consume subscribe response

    # Set value from writer
    events = []
    for v in [100, 200, 300]:
        wr.sendall(json.dumps({"cmd": "set", "path": "/test/watch", "value": v, "id": v}).encode() + b"\n")
        time.sleep(0.1)
        wr.recv(4096)  # consume set response

        # Read event from subscriber
        try:
            data = sub.recv(4096).decode().strip()
            if data:
                for line in data.split("\n"):
                    line = line.strip()
                    if line:
                        msg = json.loads(line)
                        if msg.get("type") == "event":
                            events.append(msg)
                            print(f"  [event] path={msg.get('path')} value={msg.get('value')}")
        except socket.timeout:
            pass

    # Unsubscribe
    sub.sendall(b'{"cmd":"unsubscribe","path":"/test/watch","id":99}\n')

    sub.close()
    wr.close()

    print(f"  Received {len(events)} events")
    return len(events) > 0


def main():
    print("VE Client - Complete Example")
    print("All transports, all operations")

    results = {}

    # 1. TCP JSON (default, fastest, zero deps)
    try:
        client = VeClient()
        results["TCP JSON"] = test_transport("TCP JSON (tcp://localhost:5082)", client)
        client.close()
    except Exception as e:
        print(f"  TCP JSON: ERROR - {e}")
        results["TCP JSON"] = False

    # 2. HTTP REST
    try:
        client = VeClient("http://localhost:5080", transport="http")
        results["HTTP REST"] = test_transport("HTTP REST (http://localhost:5080)", client)
        client.close()
    except Exception as e:
        print(f"  HTTP REST: ERROR - {e}")
        results["HTTP REST"] = False

    # 3. JSON-RPC
    try:
        client = VeClient("http://localhost:5080", transport="jsonrpc")
        results["JSON-RPC"] = test_transport("JSON-RPC (http://localhost:5080/jsonrpc)", client)
        client.close()
    except Exception as e:
        print(f"  JSON-RPC: ERROR - {e}")
        results["JSON-RPC"] = False

    # 4. MessagePack
    try:
        client = VeClient("tcp://localhost:5065", transport="msgpack")
        results["MessagePack"] = test_transport("MessagePack (tcp://localhost:5065)", client)
        client.close()
    except Exception as e:
        print(f"  MessagePack: ERROR - {e}")
        results["MessagePack"] = False

    # 5. Subscribe test
    try:
        results["Subscribe"] = test_subscribe_tcp()
    except Exception as e:
        print(f"  Subscribe: ERROR - {e}")
        results["Subscribe"] = False

    # Summary
    print(f"\n{'=' * 50}")
    print("  Summary")
    print(f"{'=' * 50}")
    for name, ok in results.items():
        status = "OK" if ok else "FAIL"
        print(f"  {name:15} {status}")

    failed = [k for k, v in results.items() if not v]
    if failed:
        print(f"\nFailed: {', '.join(failed)}")
        sys.exit(1)
    else:
        print("\nAll tests passed!")


if __name__ == "__main__":
    main()
