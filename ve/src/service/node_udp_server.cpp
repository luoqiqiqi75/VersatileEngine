// node_udp_server.cpp — ve::service::NodeUdpServer (single session, no watch)
#include "ve/service/node_service.h"
#include "ve/core/node.h"
#include "ve/core/schema.h"
#include "node_session.h"
#include "node_commands.h"
#include "server_util.h"

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <asio2/udp/udp_server.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <memory>
#include <string>

namespace ve {
namespace service {

static const schema::ExportOptions<schema::JsonS> compactJson{0};

static std::string toJson(Node& n)
{
    return schema::exportAs<schema::JsonS>(&n, compactJson);
}

struct NodeUdpServer::Private
{
    Node*    root = nullptr;
    uint16_t port = 12300;

    asio2::udp_server server;
    std::unique_ptr<Session> session;
};

NodeUdpServer::NodeUdpServer(const Node* config_n) : _p(std::make_unique<Private>())
{
    _p->root = ve::n(config_n->get("root").toString("/"));
    _p->port = config_n->get("port").toInt(0);
}

NodeUdpServer::~NodeUdpServer()
{
    stop();
}

bool NodeUdpServer::start()
{
    registerNodeCommands();
    _p->session = std::make_unique<Session>(_p->root);

    _p->server.bind_recv([this](auto& session_ptr, std::string_view data) {
        std::string msg(data);
        if (msg.empty()) return;

        Node ctx;
        if (!schema::importAs<schema::JsonS>(&ctx, msg)) {
            Node err;
            err.set("code", int64_t(-2));
            err.set("message", std::string("invalid JSON"));
            session_ptr->async_send(toJson(err));
            return;
        }

        if (dispatch(_p->session.get(), &ctx))
            session_ptr->async_send(toJson(ctx));
    });

    ve::service::disableWindowsPortReuse(_p->server);
    return _p->server.start("0.0.0.0", _p->port);
}

void NodeUdpServer::stop()
{
    _p->server.stop();
    _p->session.reset();
}

bool NodeUdpServer::isRunning() const
{
    return _p->server.is_started();
}

} // namespace service
} // namespace ve
