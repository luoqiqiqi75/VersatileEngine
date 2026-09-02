import json
import threading
import unittest

from ve_client.transports import TcpJsonTransport, _command_params


class _AliveThread:
    @staticmethod
    def is_alive():
        return True


class _ReplyingSocket:
    def __init__(self, transport):
        self.transport = transport
        self.request = None

    def sendall(self, payload):
        self.request = json.loads(payload)
        event, result_holder = self.transport._pending[self.request["id"]]
        result_holder[0] = {
            "id": self.request["id"],
            "code": 0,
            "data": {"registered": self.request["params"]["id"]},
        }
        event.set()

    def shutdown(self, _how):
        pass

    def close(self):
        pass


class CommandParamsTests(unittest.TestCase):
    def test_named_params_are_copied_without_filtering(self):
        args = {"id": "leo_teleop", "wait": False, "label": "leo-teleop"}

        params = _command_params(args)

        self.assertEqual(params, args)
        self.assertIsNot(params, args)

    def test_positional_params_remain_wrapped_under_args(self):
        self.assertEqual(_command_params(["leo_teleop"]),
                         {"args": ["leo_teleop"]})

    def test_tcp_command_keeps_business_id_separate_from_request_id(self):
        transport = object.__new__(TcpJsonTransport)
        transport.timeout = 1
        transport.sock = _ReplyingSocket(transport)
        transport._id = 0
        transport._lock = threading.Lock()
        transport._pending = {}
        transport._recv_thread = _AliveThread()
        transport._running = True
        transport._disconnect_error = None

        try:
            result = transport.command(
                "leo.service.register",
                {"id": "leo_teleop", "wait": False},
            )

            request = transport.sock.request
            self.assertEqual(result, {"registered": "leo_teleop"})
            self.assertEqual(request["id"], 1)
            self.assertEqual(request["params"]["id"], "leo_teleop")
            self.assertFalse(request["params"]["wait"])
        finally:
            transport.close()


if __name__ == "__main__":
    unittest.main()
