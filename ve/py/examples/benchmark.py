"""VE Client Benchmark Suite

Compares performance of different transports:
- HTTP /ve (12000)
- JSON-RPC (12000/jsonrpc)
- TCP JSON (12200) - default
- MessagePack (11000) - fastest

Usage:
    python benchmark.py
    python benchmark.py --quick  # Skip throughput test
"""

import sys
import time
import argparse
sys.stdout.reconfigure(encoding='utf-8')

from ve_client import VeClient


def bench_latency(client, n=1000):
    """Measure average latency for get operations."""
    # Warmup
    for _ in range(10):
        try:
            client.get("/test")
        except:
            pass

    start = time.time()
    success = 0
    for i in range(n):
        try:
            client.get("/test")
            success += 1
        except Exception as e:
            pass
    elapsed = time.time() - start

    if success == 0:
        return None
    return elapsed / success * 1000  # ms/op


def bench_throughput(client, duration=10):
    """Measure operations per second."""
    count = 0
    end_time = time.time() + duration
    while time.time() < end_time:
        try:
            client.get("/test")
            count += 1
        except:
            pass
    return count / duration  # ops/s


def bench_batch(client, n=100):
    """Measure time for sequential set operations (different nodes)."""
    # Warmup: ensure connection pool is ready
    for i in range(5):
        try:
            client.set("/warmup", i)
        except:
            pass

    start = time.time()
    success = 0
    for i in range(n):
        try:
            client.set(f"/bench/{i}", i)
            success += 1
        except:
            pass
    elapsed = (time.time() - start) * 1000  # ms

    if success == 0:
        return None
    return elapsed


def main():
    parser = argparse.ArgumentParser(description='VE Client Benchmark')
    parser.add_argument('--quick', action='store_true', help='Skip throughput test')
    parser.add_argument('--latency-n', type=int, default=1000, help='Latency test iterations')
    parser.add_argument('--batch-n', type=int, default=100, help='Batch test iterations')
    args = parser.parse_args()

    transports = [
        ("HTTP /ve", "http://localhost:12000", "http"),
        ("JSON-RPC", "http://localhost:12000", "jsonrpc"),
        ("TCP JSON", "tcp://localhost:12200", "tcp"),
        ("MessagePack", "tcp://localhost:11000", "msgpack"),
    ]

    print("VE Client Benchmark")
    print("=" * 60)
    print()

    # Latency test
    print(f"Latency ({args.latency_n} get operations):")
    print("-" * 60)
    latencies = {}
    for name, url, transport in transports:
        try:
            client = VeClient(url, transport=transport)
            lat = bench_latency(client, args.latency_n)
            if lat:
                latencies[name] = lat
                marker = "  <- default" if transport == "tcp" else ("  <- fastest" if transport == "msgpack" else "")
                print(f"  {name:15} {lat:7.2f} ms/op{marker}")
            else:
                print(f"  {name:15} FAILED")
            client.close()
        except Exception as e:
            print(f"  {name:15} ERROR: {e}")
    print()

    # Throughput test
    if not args.quick:
        print("Throughput (10 seconds):")
        print("-" * 60)
        for name, url, transport in transports:
            try:
                client = VeClient(url, transport=transport)
                tput = bench_throughput(client, 10)
                print(f"  {name:15} {tput:7.0f} ops/s")
                client.close()
            except Exception as e:
                print(f"  {name:15} ERROR: {e}")
        print()

    # Batch test
    print(f"Batch ({args.batch_n} sequential sets):")
    print("-" * 60)
    for name, url, transport in transports:
        try:
            client = VeClient(url, transport=transport)
            batch_time = bench_batch(client, args.batch_n)
            if batch_time:
                print(f"  {name:15} {batch_time:7.0f} ms")
            else:
                print(f"  {name:15} FAILED")
            client.close()
        except Exception as e:
            print(f"  {name:15} ERROR: {e}")
    print()

    # Summary
    if latencies:
        fastest = min(latencies, key=latencies.get)
        slowest = max(latencies, key=latencies.get)
        speedup = latencies[slowest] / latencies[fastest]
        print("Summary:")
        print("-" * 60)
        print(f"  Fastest: {fastest} ({latencies[fastest]:.2f} ms/op)")
        print(f"  Slowest: {slowest} ({latencies[slowest]:.2f} ms/op)")
        print(f"  Speedup: {speedup:.1f}x")


if __name__ == "__main__":
    main()
