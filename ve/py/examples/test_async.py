"""Test async client basic functionality.

Usage:
    pip install ve-client[async]
    python test_async.py
"""

import asyncio
from ve_client import AsyncVeClient


async def test_basic_operations():
    """Test basic async operations."""
    print("Testing AsyncVeClient...")

    async with AsyncVeClient("http://localhost:12000") as client:
        # Test ping
        print("\n1. Testing ping...")
        ok = await client.ping()
        print(f"   Ping: {'OK' if ok else 'FAILED'}")

        # Test set
        print("\n2. Testing set...")
        success = await client.set("/test/async/value", 42)
        print(f"   Set /test/async/value = 42: {'OK' if success else 'FAILED'}")

        # Test get
        print("\n3. Testing get...")
        value = await client.get("/test/async/value")
        print(f"   Get /test/async/value: {value}")

        # Test list
        print("\n4. Testing list...")
        children = await client.list("/test")
        print(f"   List /test: {len(children)} children")
        for child in children[:3]:
            print(f"      - {child.get('name', 'N/A')}")

        # Test tree
        print("\n5. Testing tree...")
        tree = await client.tree("/test/async")
        print(f"   Tree /test/async: {tree}")

        # Test command
        print("\n6. Testing command (search)...")
        try:
            result = await client.command("search", {
                "args": ["async", "/test", "--key", "--top", "5"]
            })
            print(f"   Search result: {result}")
        except Exception as e:
            print(f"   Search failed (command may not exist): {e}")

    print("\n✅ All tests completed!")


async def test_multiple_clients():
    """Test multiple concurrent clients."""
    print("\n\nTesting concurrent clients...")

    async def fetch_value(client_id: int):
        async with AsyncVeClient("http://localhost:12000") as client:
            value = await client.get("/test/async/value")
            print(f"   Client {client_id}: got value {value}")
            return value

    # Run 5 clients concurrently
    results = await asyncio.gather(*[fetch_value(i) for i in range(5)])
    print(f"   All clients got: {results}")
    print("✅ Concurrent test completed!")


if __name__ == "__main__":
    asyncio.run(test_basic_operations())
    asyncio.run(test_multiple_clients())
