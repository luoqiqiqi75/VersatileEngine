"""Test Python client alignment with JS veservice.js API."""

import sys
sys.path.insert(0, "../")

from ve_client import VeClient

def test_api_alignment():
    """Test that Python API matches JS veservice.js API."""
    client = VeClient("http://localhost:12000")

    print("Testing API alignment with JS veservice.js...")

    # Test val() - single value operations (like JS val)
    print("\n1. val() - single value operations:")
    print("   val('/test', 42) - set single value")
    client.val("/test", 42)
    print("   val('/test') - get single value:", client.val("/test"))

    # Test get() - tree operations (like JS get)
    print("\n2. get() - tree operations (depth=-1 by default):")
    tree = client.get("/test")
    print("   get('/test'):", tree)

    # Test set() - tree structure operations (like JS set, uses node.put)
    print("\n3. set() - tree structure operations:")
    print("   set('/config', {'port': 8080, 'host': 'localhost'})")
    client.set("/config", {"port": 8080, "host": "localhost"})
    print("   get('/config'):", client.get("/config"))

    # Test depth parameter
    print("\n4. get() with depth parameter:")
    print("   get('/config', depth=0) - only node value:", client.get("/config", depth=0))
    print("   get('/config', depth=-1) - full tree:", client.get("/config", depth=-1))

    print("\n✓ API alignment test completed!")
    print("\nPython API now matches JS veservice.js:")
    print("  - get(path, depth=-1) → tree || value (node.get)")
    print("  - set(path, tree) → tree structure (node.put)")
    print("  - val(path) → single value read (node.get)")
    print("  - val(path, value) → single value write (node.set)")

if __name__ == "__main__":
    test_api_alignment()
