import json
import socket
import threading
import time
import unittest

from ve_client import VeClient


class _ClosingSubscriptionServer:
    def __init__(self):
        self._listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._listener.bind(("127.0.0.1", 0))
        self._listener.listen(1)
        self.port = self._listener.getsockname()[1]
        self.error = None
        self.done = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def _run(self):
        try:
            with self._listener:
                conn, _ = self._listener.accept()
                with conn, conn.makefile("rb") as reader:
                    request = json.loads(reader.readline())
                    if request.get("op") != "subscribe":
                        raise AssertionError(f"expected subscribe, got {request!r}")
                    reply = {"id": request["id"], "code": 0}
                    conn.sendall((json.dumps(reply) + "\n").encode("utf-8"))

                    # Close while the subscribed client has a request in flight.
                    if not reader.readline():
                        raise AssertionError("client closed before sending the test request")
        except Exception as exc:
            self.error = exc
        finally:
            self.done.set()

    def wait(self):
        if not self.done.wait(2):
            raise AssertionError("test server did not finish")
        self._thread.join(timeout=1)
        if self.error is not None:
            raise self.error


class VeClientDisconnectTests(unittest.TestCase):
    def test_server_close_wakes_subscribed_client_without_request_timeout(self):
        server = _ClosingSubscriptionServer()
        client = VeClient(f"tcp://127.0.0.1:{server.port}", timeout=5)
        transport = client._transport

        try:
            client.subscribe("test/watch", lambda _path, _data: None)

            started = time.monotonic()
            with self.assertRaises(ConnectionError):
                client.get("test/watch")
            self.assertLess(time.monotonic() - started, 2)

            server.wait()
            self.assertFalse(transport._running)
            self.assertIsNone(transport.sock)
            self.assertFalse(transport._pending)

            started = time.monotonic()
            self.assertFalse(client.ping())
            self.assertLess(time.monotonic() - started, 0.5)
        finally:
            client.close()


if __name__ == "__main__":
    unittest.main()
