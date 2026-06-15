// node_http_service.cpp — ve::service::NodeHttpServer (single session)
#include "ve/service/node_service.h"

#include "ve/core/command.h"
#include "ve/core/schema.h"
#include "ve/core/pipeline.h"

#include "node_commands.h"
#include "server_util.h"

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <asio2/http/http_server.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace ve {

namespace service {

static constexpr char HTTP_KEY_SEP = ':';

struct HttpRep : std::pair<http::status, std::string>
{
    using std::pair<http::status, std::string>::pair;
    explicit HttpRep() { first = http::status::ok; }
};
struct HttpResultRep : std::pair<Result, Node*>
{
    using std::pair<Result, Node*>::pair;
    explicit HttpResultRep(Result&& r) { first = std::move(r); second = nullptr; }
    explicit HttpResultRep(Node* n) { first = Result::ok(); second = n; }
};

}

namespace convert {

static bool parse(const service::HttpRep& r, http::web_response& rep)
{
    rep.fill_json(r.second, r.first);
    return true;
}

static bool parse(const service::HttpResultRep& r, http::web_response& rep)
{
    Node proto_n;
    proto_n.set("code", r.first.code());
    proto_n.set("message", r.first.message());
    proto_n.at("data")->copy(r.second);
    http::status status = r.first.isAccepted() ? http::status::accepted : http::status::ok; // always ok
    return parse(service::HttpRep { status, schema::exportAs<schema::JsonS>(&proto_n, schema::JsonS::compact()) }, rep);
}

}

namespace service {

struct NodeHttpServer::Private
{
    Node*    root = nullptr;
    uint16_t port = 12000;

    asio2::http_server server;

    std::chrono::steady_clock::time_point startTime;
    std::unique_ptr<Session> session;
};

NodeHttpServer::NodeHttpServer(const Node* config_n) : _p(std::make_unique<Private>())
{
    _p->root = ve::n(config_n->get("root").toString("/"));
    _p->port = config_n->get("port").toInt(0);
}

NodeHttpServer::~NodeHttpServer()
{
    stop();
}

bool NodeHttpServer::start()
{
    _p->session = std::make_unique<Session>(_p->root, _p->root);

    { // health protocol
        _p->startTime = std::chrono::steady_clock::now();

        auto health_f = [=] {
            auto elapsed = std::chrono::steady_clock::now() - _p->startTime;
            auto seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
            return seconds;
        };

        _p->server.bind<http::verb::get>("/health", [=] (http::web_request&, http::web_response& rep) {
            Pipeline pipeline;
            pipeline.addProc(convert::to<Proc>(health_f));
            pipeline.sync();
            int s = pipeline.outputNode()->getInt64();
            convert::parse(HttpRep(http::status::ok, "\"status\":\"ok\",\"uptime_s\":" + std::to_string(s) + "}"), rep);
        });
    }

    { // at protocol
        auto tar_n_f = [root_n = _p->root] (http::web_request& req, http::web_response& rep) {
            auto sv = req.path();
            sv.remove_prefix(4); // /at/
            Node* tar_n = root_n->atPath(sv, VE_NODE_PATH_SEP, HTTP_KEY_SEP);
            if (!tar_n) convert::parse(HttpRep{http::status::not_found, "node not found"}, rep);
            return tar_n;
        };

        // export tree
        _p->server.bind<http::verb::get>("/at", [root_n = _p->root] (http::web_request&, http::web_response& rep) {
            convert::parse(HttpResultRep(root_n), rep);
        });
        _p->server.bind<http::verb::get>("/at/*", [=] (http::web_request& req, http::web_response& rep) {
            if (const auto tar_n = tar_n_f(req, rep)) convert::parse(HttpResultRep(tar_n), rep);
        });

        // import tree
        // _p->server.bind<http::verb::put>("/at", [] (http::web_request& req, http::web_response& rep) {
        //     convert::parse(HttpResult(http::status::forbidden, "forbid to import whole node tree"), rep);
        // });
        _p->server.bind<http::verb::put>("/at/*", [tar_n_f] (http::web_request& req, http::web_response& rep) {
            if (const auto tar_n = tar_n_f(req, rep)) {
                if (schema::importAs<schema::JsonS>(tar_n, req.body())) { // without deletion
                    convert::parse(HttpRep(), rep);
                } else {
                    convert::parse(HttpResultRep(Result::fail(ERR_INVALID, "invalid json")), rep);
                }
            }
        });

        // _p->server.bind<http::verb::post>("/at", [] (http::web_request&, http::web_response& rep) {
        //     convert::parse(HttpResult(http::status::forbidden, "forbid to import whole node tree"), rep);
        // });
        _p->server.bind<http::verb::post>("/at/*", [tar_n_f] (http::web_request& req, http::web_response& rep) {
            if (auto const tar_n = tar_n_f(req, rep)) {
                if (schema::importAs<schema::JsonS>(tar_n, req.body(), Node::COPY_STRICT)) { // with deletion
                    convert::parse(HttpRep(), rep);
                } else {
                    convert::parse(HttpResultRep(Result::fail(ERR_INVALID, "invalid json")), rep);
                }
            }
        });

        _p->server.bind<http::verb::delete_>("/at/*", [tar_n_f] (http::web_request& req, http::web_response& rep) {
            if (auto const tar_n = tar_n_f(req, rep)) {
                if (tar_n->parent()->remove(tar_n)) {
                    convert::parse(HttpRep(), rep);
                } else {
                    convert::parse(HttpResultRep{Result::fail("failed")}, rep);
                }
            }
        });
    }

    { // cmd protocol
        _p->server.bind<http::verb::post>("/cmd/*", [o = _p->session.get()] (http::web_request& req, http::web_response& rep) {
            auto cmd_sv = req.path();
            cmd_sv.remove_prefix(5); // /cmd/
            std::string cmd_key(cmd_sv);
            Command cmd = command::create(cmd_key);
            if (!cmd.valid()) {
                convert::parse(HttpResultRep(Result::fail(ERR_NOT_FOUND, "unknown command")), rep);
                return;
            }


            if (!cmd.input(req.body())) {
                convert::parse(HttpResultRep(Result::fail(ERR_INVALID, "bad request")), rep);
                return;
            }

           //  if (queryBool(req.query(), "async", false)) {
           //      cmd.call([guard = rep.defer(), rep_ptr = &rep] (Command& c) {
           //          convert::parse(HttpResultRep(c.result(), c.outputNode()), *rep_ptr);
           //      });
           // } else {
                cmd.run();
                convert::parse(HttpResultRep(cmd.result(), cmd.outputNode()), rep);
           // }
        });
    }

    { // standard protocol with envelop
        _p->server.bind<http::verb::post>("/ve", [this] (http::web_request& req, http::web_response& rep) {
            // Node ctx;

            //
            // if (dispatch(_p->session.get(), &ctx)) {
            //     int code = ctx.get("code").toInt(0);
            //     rep.fill_json(toJson(ctx), mapHttpStatus(code));
            // } else {
            //     rep.fill_json("{\"code\":1}", http::status::accepted);
            // }
            //
            // std::string cmd = ctx->get("cmd").toString();
            // if (cmd.empty()) {
            //     setError(ctx, ERR_INVALID, "cmd required");
            //     return true;
            // }

            Pipeline pipe;
            if (!schema::importAs<schema::JsonS>(pipe.contextNode(), std::string(req.body()))) {
                convert::parse(HttpResultRep(Result::fail(ERR_INVALID, "invalid JSON")), rep);
                return;
            }

            Command* cmd = nullptr;
            std::string cmd_key = pipe.contextNode()->get("cmd").toString();
            static Factory& node_f = factory::at("standard/node");
            static Factory& cmd_f = command::factory();
            if (node_f.has(cmd_key)) {
                cmd = pipe.add(command::create(node_f, cmd_key));
            } else if (cmd_f.has(cmd_key)) {
                cmd = pipe.add(command::create(cmd_f, cmd_key));
            }
            if (!cmd || !cmd->valid()) {
                convert::parse(HttpResultRep(Result::fail(ERR_NOT_FOUND, "unknown: " + cmd_key)), rep);
                return;
            }

            pipe.contextNode()->set("_session", Var::ptr(_p->session.get()));

            cmd->setContextNodes(pipe.contextNode(), pipe.contextNode()->at("params"), pipe.contextNode()->at("data")); // todo: single / batch control

            // sync
            pipe.sync();

            // result
            if (pipe.result().isAccepted()) {
                convert::parse(HttpRep(http::status::accepted, "{code:" + std::to_string(pipe.result().code()) + ",cmd:\"" + cmd_key + "\"}"), rep);
            } else {
                convert::parse(HttpResultRep(pipe.result(), pipe.contextNode()->at("data")), rep);
            }
        });
    }

    { // default
        _p->server.bind_not_found([] (http::web_request&, http::web_response& rep) {
            convert::parse(HttpRep(http::status::not_found, "not found"), rep);
        });
    }

    disableWindowsPortReuse(_p->server);
    return _p->server.start("0.0.0.0", _p->port);
}

void NodeHttpServer::stop()
{
    _p->session.reset();
    _p->server.stop();
}

bool NodeHttpServer::isRunning() const
{
    return _p->server.is_started();
}

} // namespace service
} // namespace ve
