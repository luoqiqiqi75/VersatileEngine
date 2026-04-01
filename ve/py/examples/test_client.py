"""Test VeClient with both transports."""

import sys
sys.stdout.reconfigure(encoding='utf-8')

from ve_client import VeClient


def test_transport(name, client):
    print(f"\n--- Transport: {name} ---")

    print("1. Ping...")
    if not client.ping():
        print("   FAIL: Cannot connect")
        return False
    print("   OK")

    print("2. Get root...")
    try:
        val = client.get("/")
        print(f"   OK: {val}")
    except Exception as e:
        print(f"   FAIL: {e}")
        return False

    print("3. Set /test/py = 42...")
    try:
        ok = client.set("/test/py", 42)
        print(f"   OK: {ok}")
    except Exception as e:
        print(f"   FAIL: {e}")

    print("4. Get /test/py...")
    try:
        val = client.get("/test/py")
        print(f"   OK: {val}")
    except Exception as e:
        print(f"   FAIL: {e}")

    print("5. List /test...")
    try:
        children = client.list("/test")
        print(f"   OK: {children}")
    except Exception as e:
        print(f"   FAIL: {e}")

    return True


def main():
    print("VeClient Test")
    print("=" * 40)

    base = "http://localhost:8080"

    # Test HTTP REST transport
    ok1 = test_transport("http", VeClient(base, transport="http"))

    # Test JSON-RPC transport
    ok2 = test_transport("jsonrpc", VeClient(base, transport="jsonrpc"))

    print("\n" + "=" * 40)
    if ok1 and ok2:
        print("All tests passed!")
    else:
        print("Some tests failed!")
        sys.exit(1)


if __name__ == "__main__":
    main()
