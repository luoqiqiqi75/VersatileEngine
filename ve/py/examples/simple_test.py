"""Simple test - just verify connection works."""

import sys
sys.stdout.reconfigure(encoding='utf-8')

from ve_client import VeClient


def main():
    # TCP JSON (default)
    client = VeClient()

    if not client.ping():
        print("Cannot connect to VE server at localhost:12200")
        print("Make sure ve.exe is running")
        sys.exit(1)

    print("Connected!")
    print(f"Root value: {client.get('/')}")
    print(f"Children: {client.list('/')}")

    client.close()


if __name__ == "__main__":
    main()
