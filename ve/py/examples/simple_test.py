"""Simple test - just verify connection works."""

import sys
sys.stdout.reconfigure(encoding='utf-8')

from ve_client import VeClient


def main():
    client = VeClient("http://localhost:8080")

    if not client.ping():
        print("Cannot connect to VE server at localhost:8080")
        sys.exit(1)

    print("Connected!")
    print(f"Root value: {client.get('/')}")
    print(f"Children: {client.list('/')}")


if __name__ == "__main__":
    main()
