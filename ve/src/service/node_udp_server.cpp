// node_udp_server.cpp — ve::service::NodeUdpServer
#include "ve/service/node_service.h"
#include "ve/core/node.h"
#include "ve/core/schema.h"
#include "node_protocol.h"
#include "node_task_service.h"
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

static const schema::ExportOptions compactJson{0};

static std::string toJson(Node& n)
{
    return schema::exportAs<schema::JsonS>(&n, compactJson);
}

static void fillError(Node* rep, const std::string& code, const std::string& error)
{
    rep->clear();
    rep->set(Var());
    rep->set("ok", false);
    rep->set("code", code);
    rep->set("error", error);
}

struct NodeUdpServer::Private
{
    Node*    root = nullptr;
    uint16_t port = 12300;
    asio2::udp_server server;
    std::unique_ptr<NodeTaskService> taskSvc;
};

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
    _p->taskSvc = std::make_unique<NodeTaskService>(_p->root);

    _p->server.bind_recv([this](auto& session_ptr, std::string_view data) {
        std::string msg(data);
        if (msg.empty()) {
            return;
        }

        Node req("req");
        if (!schema::importAs<schema::JsonS>(&req, msg)) {
            Node reply("rep");
            fillError(&reply, "invalid_request", "invalid JSON request");
            session_ptr->async_send(toJson(reply));
            return;
        }

        Node reply("rep");
        dispatchNodeProtocol(_p->root, &req, &reply, nullptr, _p->taskSvc.get());
        session_ptr->async_send(toJson(reply));
    });

    ve::service::disableWindowsPortReuse(_p->server);
    return _p->server.start("0.0.0.0", _p->port);
}

void NodeUdpServer::stop()
{
    _p->server.stop();
    _p->taskSvc.reset();
}

bool NodeUdpServer::isRunning() const
{
    return _p->server.is_started();
}

} // namespace service
} // namespace ve
