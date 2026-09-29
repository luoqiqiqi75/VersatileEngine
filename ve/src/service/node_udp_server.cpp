// node_udp_server.cpp - per-peer JSON datagram execution, no subscriptions
#include "ve/service/node_service.h"
#include "node_server_util.h"

namespace ve {
namespace service {

struct NodeUdpServer::Private
{
    Node* root = nullptr;
    uint16_t port = 12300;
    asio2::udp_server server{serverRuntime().pool()};
    ConnectionLoops<NodeConnection> connections{"node.udp"};
};

NodeUdpServer::NodeUdpServer(const Node* config_n) : _p(std::make_unique<Private>())
{
    _p->root = ve::n(config_n->get("root").toString("/"));
    _p->port = config_n->get("port").toInt(0);
}

NodeUdpServer::~NodeUdpServer() { stop(); }

bool NodeUdpServer::start()
{
    bindNodeConnections(_p->server, _p->connections, _p->root, false);
    _p->server.bind_recv([this](auto& socket, std::string_view data) {
        if (data.empty()) return;
        auto state = _p->connections.get(connectionKey(socket));
        if (!state) return;
        std::weak_ptr<NodeConnection> weak_state = state;
        std::weak_ptr<typename std::decay_t<decltype(socket)>::element_type> weak_socket = socket;
        state->loop->post([weak_state, weak_socket, message = std::string(data)] {
            auto state = weak_state.lock();
            if (state && state->connected.load(std::memory_order_acquire))
                processNodeJson(state, weak_socket, message);
        });
    });
    disableWindowsPortReuse(_p->server);
    return _p->server.start("0.0.0.0", _p->port);
}

void NodeUdpServer::stop(bool wait)
{
    _p->server.stop();
    _p->connections.stop(wait);
}

bool NodeUdpServer::isRunning() const { return _p->server.is_started(); }

} // namespace service
} // namespace ve
