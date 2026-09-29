// tcp_bin_server.cpp - per-connection native binary protocol execution
#include "ve/service/bin_service.h"
#include "ve/core/log.h"
#include "node_server_util.h"

namespace ve {
namespace service {

struct BinTcpServer::Private
{
    Node* root = nullptr;
    uint16_t port = 11000;
    asio2::tcp_server server{serverRuntime().pool()};
    ConnectionLoops<NodeConnection> connections{"bin.tcp"};

    template<typename Socket>
    static void sendFrame(const std::weak_ptr<NodeConnection>& state,
                          const std::weak_ptr<Socket>& socket, uint8_t flag, const Node& node)
    {
        auto frame = bin::encodeFrame(flag, schema::fromNode<schema::VarS>(&node));
        sendConnection(state, socket, std::string(frame.begin(), frame.end()));
    }

    template<typename Socket>
    static void processFrames(const std::shared_ptr<NodeConnection>& state,
                              const std::weak_ptr<Socket>& socket)
    {
        Var message;
        uint8_t flag;
        std::weak_ptr<NodeConnection> weak_state = state;
        auto reply = [weak_state, socket](const Node& node) {
            sendFrame(weak_state, socket,
                node.get("code").toInt() < 0 ? bin::FLAG_ERROR : bin::FLAG_RESPONSE, node);
        };
        while (state->connected.load(std::memory_order_acquire)
               && bin::tryPopFrame(state->binBuf, flag, message)) {
            if ((flag & bin::FLAG_TYPE_MASK) != bin::FLAG_REQUEST) continue;
            Pipeline pipe;
            if (!schema::toNode<schema::VarS>(pipe.contextNode(), message)) {
                Node error;
                error.set("code", int64_t(ERR_INVALID));
                error.set("message", "invalid binary request");
                reply(error);
                continue;
            }
            executeNodeRequest(std::move(pipe), state->session, reply);
        }
    }
};

BinTcpServer::BinTcpServer(const Node* config_n) : _p(std::make_unique<Private>())
{
    _p->root = ve::n(config_n->get("root").toString("/"));
    _p->port = config_n->get("port").toInt(0);
}

BinTcpServer::~BinTcpServer() { stop(); }

bool BinTcpServer::start()
{
    _p->server.bind_connect([this](auto& socket) {
        socket->set_disconnect_timeout(std::chrono::seconds(2));
        auto state = std::make_shared<NodeConnection>();
        std::weak_ptr<NodeConnection> weak_state = state;
        std::weak_ptr<typename std::decay_t<decltype(socket)>::element_type> weak_socket = socket;
        state->session = std::make_shared<Session>(_p->root, _p->root,
            [weak_state, weak_socket](std::string message) {
                Node event;
                if (schema::toNode<schema::JsonS>(&event, message))
                    Private::sendFrame(weak_state, weak_socket, bin::FLAG_NOTIFY, event);
            });
        if (!_p->connections.add(connectionKey(socket), std::move(state))) socket->stop();
    });
    _p->server.bind_recv([this](auto& socket, std::string_view data) {
        auto state = _p->connections.get(connectionKey(socket));
        if (!state) return;
        std::weak_ptr<NodeConnection> weak_state = state;
        std::weak_ptr<typename std::decay_t<decltype(socket)>::element_type> weak_socket = socket;
        state->loop->post([weak_state, weak_socket, chunk = std::string(data)] {
            auto state = weak_state.lock();
            if (!state || !state->connected.load(std::memory_order_acquire)) return;
            state->binBuf.insert(state->binBuf.end(), chunk.begin(), chunk.end());
            Private::processFrames(state, weak_socket);
        });
    });
    _p->server.bind_disconnect([this](auto& socket) {
        const auto error = asio2::get_last_error();
        veLogDs("[bin/tcp] client disconnected:", socket->remote_address(), ":", socket->remote_port(),
                "reason:", error.value(), error.message());
        _p->connections.remove(connectionKey(socket));
    });
    disableWindowsPortReuse(_p->server);
    return _p->server.start("0.0.0.0", _p->port);
}

void BinTcpServer::stop(bool wait)
{
    _p->server.stop();
    _p->connections.stop(wait);
}

bool BinTcpServer::isRunning() const { return _p->server.is_started(); }
int BinTcpServer::connectionCount() const { return _p->connections.count(); }
uint16_t BinTcpServer::port() const { return _p->port; }

} // namespace service
} // namespace ve
