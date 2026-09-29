// node_tcp_server.cpp - per-connection line-delimited JSON execution
#include "ve/service/node_service.h"
#include "ve/core/log.h"
#include "node_server_util.h"

namespace ve {
namespace service {

struct NodeTcpServer::Private
{
    Node* root = nullptr;
    uint16_t port = 12200;
    asio2::tcp_server server{serverRuntime().pool()};
    ConnectionLoops<NodeConnection> connections{"node.tcp"};
};

NodeTcpServer::NodeTcpServer(const Node* config_n) : _p(std::make_unique<Private>())
{
    _p->root = ve::n(config_n->get("root").toString("/"));
    _p->port = config_n->get("port").toInt(0);
}

NodeTcpServer::~NodeTcpServer() { stop(); }

bool NodeTcpServer::start()
{
    bindNodeConnections(_p->server, _p->connections, _p->root, true, "\n");
    _p->server.bind_recv([this](auto& socket, std::string_view data) {
        auto state = _p->connections.get(connectionKey(socket));
        if (!state) return;
        std::weak_ptr<NodeConnection> weak_state = state;
        std::weak_ptr<typename std::decay_t<decltype(socket)>::element_type> weak_socket = socket;
        state->loop->post([weak_state, weak_socket, chunk = std::string(data)] {
            auto state = weak_state.lock();
            if (!state || !state->connected.load(std::memory_order_acquire)) return;
            auto& buffer = state->recvBuf;
            buffer.append(chunk);
            std::string::size_type pos;
            while (state->connected.load(std::memory_order_acquire)
                   && (pos = buffer.find('\n')) != std::string::npos) {
                std::string line = buffer.substr(0, pos);
                buffer.erase(0, pos + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (!line.empty()) processNodeJson(state, weak_socket, line, "\n");
            }
        });
    });
    _p->server.bind_disconnect([this](auto& socket) {
        const auto error = asio2::get_last_error();
        veLogDs("[node/tcp] client disconnected:", socket->remote_address(), ":", socket->remote_port(),
                "reason:", error.value(), error.message());
        _p->connections.remove(connectionKey(socket));
    });
    disableWindowsPortReuse(_p->server);
    return _p->server.start("0.0.0.0", _p->port);
}

void NodeTcpServer::stop(bool wait)
{
    _p->server.stop();
    _p->connections.stop(wait);
}

bool NodeTcpServer::isRunning() const { return _p->server.is_started(); }
int NodeTcpServer::connectionCount() const { return _p->connections.count(); }

} // namespace service
} // namespace ve
