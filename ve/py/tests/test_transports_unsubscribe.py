import json
import socket
import struct
import threading
import unittest
from unittest.mock import patch

from ve_client import VeClient
from ve_client.transports import msgpack


class _SubscriptionServer:
    """Send an in-flight push immediately before each unsubscribe reply."""

    def __init__(self, binary=False):
        self.binary = binary
        self.requests = []
        self.error = None
        self._listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._listener.bind(("127.0.0.1", 0))
        self._listener.listen(1)
        self._listener.settimeout(3)
        self.port = self._listener.getsockname()[1]
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def _read(self, reader):
        if not self.binary:
            line = reader.readline()
            return json.loads(line) if line else None
        header = reader.read(5)
        if not header:
            return None
        flag, length = struct.unpack("<BI", header)
        if flag != 0:
            raise AssertionError(f"expected request frame, got {flag}")
        return msgpack.unpackb(reader.read(length), raw=False)

    def _encode(self, message, notify=False):
        if not self.binary:
            return (json.dumps(message) + "\n").encode("utf-8")
        payload = msgpack.packb(message)
        return struct.pack("<BI", 0x80 if notify else 0x40, len(payload)) + payload

    def _push(self, path):
        return self._encode({
            "event": "node.changed", "path": path, "data": 42,
        }, notify=True)

    def _run(self):
        try:
            with self._listener:
                conn, _ = self._listener.accept()
                conn.settimeout(3)
                with conn, conn.makefile("rb") as reader:
                    while True:
                        request = self._read(reader)
                        if request is None:
                            break
                        self.requests.append(request)
                        payload = b""
                        if request["op"] in ("get", "unsubscribe"):
                            payload += self._push(request["params"]["path"])
                        if request["op"] == "unsubscribe":
                            payload += self._push("test/other")
                        payload += self._encode({"id": request["id"], "code": 0})
                        conn.sendall(payload)
        except Exception as exc:
            self.error = exc

    def wait(self):
        self._thread.join(timeout=4)
        if self._thread.is_alive():
            raise AssertionError("test server did not finish")
        if self.error is not None:
            raise self.error


class _UnsubscribeTests:
    transport_name = "tcp"
    send_method = "_send"

    def setUp(self):
        self.server = _SubscriptionServer(binary=self.transport_name == "msgpack")
        self.addCleanup(self.server.wait)
        self.client = VeClient(
            f"tcp://127.0.0.1:{self.server.port}",
            transport=self.transport_name, timeout=0.5,
        )
        self.addCleanup(self.client.close)

    def _unsubscribe_with_reply(self, unsubscribe):
        # unsubscribe swallows exceptions, so verify that the real send returned
        # an acknowledgement rather than timing out and silently discarding it.
        replies = []
        send = getattr(self.client._transport, self.send_method)

        def record_reply(message):
            reply = send(message)
            replies.append(reply)
            return reply

        with patch.object(self.client._transport, self.send_method, record_reply):
            unsubscribe()
        self.assertEqual(len(replies), 1, "unsubscribe did not receive its reply")
        reply = replies[0][1] if self.transport_name == "msgpack" else replies[0]
        self.assertEqual(reply["code"], 0)
        self.assertFalse(self.client._transport._pending)

    def test_callback_unsubscribe_receives_reply_after_in_flight_push(self):
        cancelled = []
        active = []
        unsubscribe = self.client.subscribe(
            "/test/watch", lambda path, data: cancelled.append((path, data)),
        )
        self.client.subscribe(
            "test/other", lambda path, data: active.append((path, data)),
        )

        self._unsubscribe_with_reply(unsubscribe)

        self.assertEqual(cancelled, [])
        self.assertEqual(active, [("test/other", 42)])
        self.assertEqual(self.server.requests[-1]["params"], {"path": "test/watch"})
        unsubscribe()
        self.assertEqual([r["op"] for r in self.server.requests],
                         ["subscribe", "subscribe", "unsubscribe"])

    def test_only_last_callback_unsubscribes_from_server(self):
        cancelled = []
        active = []
        unsubscribe_first = self.client.subscribe(
            "test/watch", lambda path, data: cancelled.append((path, data)),
        )
        unsubscribe_last = self.client.subscribe(
            "test/watch", lambda path, data: active.append((path, data)),
        )

        unsubscribe_first()
        self.client.val("test/watch")
        self.assertEqual(cancelled, [])
        self.assertEqual(active, [("test/watch", 42)])
        self.assertEqual([r["op"] for r in self.server.requests], ["subscribe", "get"])

        self._unsubscribe_with_reply(unsubscribe_last)
        self.assertEqual(active, [("test/watch", 42)])
        unsubscribe_first()
        unsubscribe_last()
        self.assertEqual([r["op"] for r in self.server.requests],
                         ["subscribe", "get", "unsubscribe"])

    def test_path_unsubscribe_receives_reply_after_in_flight_push(self):
        cancelled = []
        self.client.subscribe(
            "test/watch", lambda path, data: cancelled.append((path, data)),
        )

        self._unsubscribe_with_reply(lambda: self.client.unsubscribe("/test/watch"))

        self.assertEqual(cancelled, [])
        self.assertEqual(self.server.requests[-1]["params"], {"path": "test/watch"})


class TcpJsonUnsubscribeTests(_UnsubscribeTests, unittest.TestCase):
    pass


@unittest.skipIf(msgpack is None, "msgpack is not installed")
class MsgPackUnsubscribeTests(_UnsubscribeTests, unittest.TestCase):
    transport_name = "msgpack"
    send_method = "_send_message"


if __name__ == "__main__":
    unittest.main()
