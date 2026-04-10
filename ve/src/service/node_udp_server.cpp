// node_udp_server.cpp — ve::service::NodeUdpServer
//
// JSON text protocol over UDP, one JSON object per datagram.
// Same command format as NodeTcpServer:
//   {"cmd":"get","path":"...","id":1}
//   {"cmd":"set","path":"...","value":...,"id":1}
//
// Stateless - no subscribe support (UDP has no persistent connection).

#include "ve/service/node_service.h"
#include "ve/core/node.h"
#include "ve/core/var.h"
#include "ve/core/command.h"
#include "ve/core/schema.h"
#include "ve/core/log.h"
#include "server_util.h"

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <asio2/udp/udp_server.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace ve {
namespace service {

// Reuse from node_tcp_server.cpp
extern std::string handleNodeJsonCmd(Node* root, Node* reqNode);

// ============================================================================
// Private
// ============================================================================

struct NodeUdpServer::Private
{
    Node*    root = nullptr;
    uint16_t port = 12300;
    asio2::udp_server server;
};

// ============================================================================
// NodeUdpServer
// ============================================================================

NodeUdpServer::NodeUdpServer(Node* root, uint16_t port)
    : _p(std::make_unique<Private>())
{
    _p->root = root;
    _p->port = port;
}

NodeUdpServer::~NodeUdpServer()
{
    stop();
}

bool NodeUdpServer::start()
{
    _p->server.bind_recv([this](auto& session_ptr, std::string_view data) {
        std::string msg(data);
        if (msg.empty()) return;

        Node req("req");
        if (!schema::importAs<schema::JsonS>(&req, msg)) {
            session_ptr->async_send("{\"type\":\"error\",\"msg\":\"invalid json\"}");
            return;
        }
        std::string response = handleNodeJsonCmd(_p->root, &req);
        session_ptr->async_send(response);
    });

    ve::service::disableWindowsPortReuse(_p->server);
    return _p->server.start("0.0.0.0", _p->port);
}

void NodeUdpServer::stop()
{
    _p->server.stop();
}

bool NodeUdpServer::isRunning() const
{
    return _p->server.is_started();
}

} // namespace service
} // namespace ve
