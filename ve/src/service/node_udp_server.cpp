// node_udp_server.cpp — ve::service::NodeUdpServer (single session, no watch)
#include "ve/service/node_service.h"
#include "ve/core/node.h"
#include "ve/core/schema.h"
#include "ve/core/command.h"
#include "ve/core/pipeline.h"
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

static std::string toJson(Node& n)
{
    return schema::fromNode<schema::JsonS>(&n, schema::JsonS::compact());
}

struct NodeUdpServer::Private
{
    Node*    root = nullptr;
    uint16_t port = 12300;

    asio2::udp_server server{serverRuntime().pool()};
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
    _p->session = std::make_unique<Session>(_p->root, _p->root);

    _p->server.bind_recv([this](auto& session_ptr, std::string_view data) {
        std::string msg(data);
        if (msg.empty()) return;

        Pipeline pipe;
        if (!schema::toNode<schema::JsonS>(pipe.contextNode(), msg)) {
            Node err;
            err.set("code", int64_t(ERR_INVALID));
            err.set("message", std::string("invalid JSON"));
            session_ptr->async_send(toJson(err));
            return;
        }

        pipe.contextNode()->set("_session", Var::ptr(_p->session.get()));

        Node* batch_n = pipe.contextNode()->find("batch");
        if (batch_n) {
            Node* out = pipe.contextNode()->at("data");
            for (auto* item : batch_n->children()) {
                auto ref = resolveCmd(item);
                if (!ref.factory) {
                    pipe.contextNode()->erase("batch");
                    pipe.contextNode()->set("code", int64_t(ERR_NOT_FOUND));
                    pipe.contextNode()->set("message", "unknown: " + ref.key);
                    session_ptr->async_send(toJson(*pipe.contextNode()));
                    return;
                }
                Command* c = pipe.add(command::create(*ref.factory, ref.key));
                c->setContextNodes(pipe.contextNode(), item->at("params"), out->append());
            }
        } else {
            auto ref = resolveCmd(pipe.contextNode());
            if (!ref.factory) {
                pipe.contextNode()->set("code", int64_t(ref.key.empty() ? ERR_INVALID : ERR_NOT_FOUND));
                pipe.contextNode()->set("message", ref.key.empty() ? std::string("op or cmd required") : "unknown: " + ref.key);
                session_ptr->async_send(toJson(*pipe.contextNode()));
                return;
            }
            Command* c = pipe.add(command::create(*ref.factory, ref.key));
            c->setContextNodes(pipe.contextNode(), pipe.contextNode()->at("params"), pipe.contextNode()->at("data"));
        }

        pipe.sync();
        if (finalizeReply(pipe))
            session_ptr->async_send(toJson(*pipe.contextNode()));
    });

    ve::service::disableWindowsPortReuse(_p->server);
    return _p->server.start("0.0.0.0", _p->port);
}

void NodeUdpServer::stop(bool wait)
{
    if (!wait) {
        _p->server.stop();
        return;
    }
    _p->server.stop();
    _p->session.reset();
}

bool NodeUdpServer::isRunning() const
{
    return _p->server.is_started();
}

} // namespace service
} // namespace ve
