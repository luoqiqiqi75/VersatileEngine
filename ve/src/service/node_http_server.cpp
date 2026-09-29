// node_http_server.cpp - ve::service::NodeHttpServer (per-connection execution)
#include "ve/service/node_service.h"

#include "ve/core/command.h"
#include "ve/core/schema.h"
#include "ve/core/pipeline.h"

#include "node_commands.h"
#include "node_server_util.h"

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
    parse(r.first, proto_n);
    proto_n.at("data")->copy(r.second);
    http::status status = r.first.isAccepted() ? http::status::accepted : http::status::ok; // always ok
    return parse(service::HttpRep { status, schema::fromNode<schema::JsonS>(&proto_n, schema::JsonS::compact()) }, rep);
}

}

namespace service {

struct NodeHttpServer::Private
{
    Node*    root = nullptr;
    uint16_t port = 12000;

    asio2::http_server server{serverRuntime().pool()};

    std::chrono::steady_clock::time_point startTime;
    ConnectionLoops<NodeConnection> connections{"node.http"};

    template<typename Handler>
    auto route(Handler handler)
    {
        return [this, handler](std::shared_ptr<asio2::http_session>& socket, http::web_request& req, http::web_response& rep) {
            postHttpRequest(connections.get(connectionKey(socket)), socket, req, rep,
                [handler](auto& req, auto& rep, const auto& state) {
                    if constexpr (std::is_invocable_v<Handler, http::web_request&,
                                  http::web_response&, const std::shared_ptr<NodeConnection>&>)
                        handler(req, rep, state);
                    else
                        handler(req, rep);
                });
        };
    }

    template<http::verb Method, typename Handler>
    void bind(const std::string& path, Handler handler)
    {
        server.bind<Method>(path, route(std::move(handler)));
    }

    static void execute(Pipeline pipe, const std::shared_ptr<Session>& session, http::web_response& rep)
    {
        const bool background = pipe.contextNode()->get("async").toBool(false);
        Result result = executeNodeRequest(pipe, session, [&rep](const Node& node) {
            rep.fill_json(schema::fromNode<schema::JsonS>(&node, schema::JsonS::compact()),
                node.get("accepted").toBool(false) ? http::status::accepted : http::status::ok);
        }, false);
        if (!background && result.isAccepted())
            convert::parse(HttpRep(http::status::accepted,
                "{\"code\":" + std::to_string(result.code()) + "}"), rep);
    }
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
    bindNodeConnections(_p->server, _p->connections, _p->root, false);

    { // health protocol
        _p->startTime = std::chrono::steady_clock::now();

        _p->bind<http::verb::get>("/health", [this] (http::web_request&, http::web_response& rep) {
            auto elapsed = std::chrono::steady_clock::now() - _p->startTime;
            auto seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
            convert::parse(HttpRep(http::status::ok,
                "{\"status\":\"ok\",\"uptime_s\":" + std::to_string(seconds) + "}"), rep);
        });
    }

    { // at protocol
        auto tar_n_f = [root_n = _p->root] (http::web_request& req, http::web_response& rep) {
            auto sv = req.path();
            sv.remove_prefix(4); // /at/
            Node* tar_n = const_cast<const Node*>(root_n)->atPath(sv, VE_NODE_PATH_SEP, HTTP_KEY_SEP);
            if (!tar_n) convert::parse(HttpRep{http::status::not_found, "node not found"}, rep);
            return tar_n;
        };

        // export tree
        _p->bind<http::verb::get>("/at", [root_n = _p->root] (http::web_request&, http::web_response& rep) {
            convert::parse(HttpRep(http::status::ok, schema::fromNode<schema::JsonS>(root_n, schema::JsonS::compact())), rep);
        });
        _p->bind<http::verb::get>("/at/*", [=] (http::web_request& req, http::web_response& rep) {
            if (const auto tar_n = tar_n_f(req, rep)) {
                convert::parse(HttpRep(http::status::ok, schema::fromNode<schema::JsonS>(tar_n, schema::JsonS::compact())), rep);
            }
        });

        // import tree
        // _p->bind<http::verb::put>("/at", [] (http::web_request& req, http::web_response& rep) {
        //     convert::parse(HttpResult(http::status::forbidden, "forbid to import whole node tree"), rep);
        // });
        _p->bind<http::verb::put>("/at/*", [tar_n_f] (http::web_request& req, http::web_response& rep) {
            if (const auto tar_n = tar_n_f(req, rep)) {
                if (schema::toNode<schema::JsonS>(tar_n, req.body())) { // without deletion
                    convert::parse(HttpRep(), rep);
                } else {
                    convert::parse(HttpRep(http::status::bad_request, "invalid json"), rep);
                }
            }
        });

        // _p->bind<http::verb::post>("/at", [] (http::web_request&, http::web_response& rep) {
        //     convert::parse(HttpResult(http::status::forbidden, "forbid to import whole node tree"), rep);
        // });
        _p->bind<http::verb::post>("/at/*", [tar_n_f] (http::web_request& req, http::web_response& rep) {
            if (auto const tar_n = tar_n_f(req, rep)) {
                if (schema::toNode<schema::JsonS>(tar_n, req.body(), Node::COPY_STRICT)) { // with deletion
                    convert::parse(HttpRep(), rep);
                } else {
                    convert::parse(HttpRep(http::status::bad_request, "invalid json"), rep);
                }
            }
        });

        _p->bind<http::verb::delete_>("/at/*", [tar_n_f] (http::web_request& req, http::web_response& rep) {
            if (auto const tar_n = tar_n_f(req, rep)) {
                if (tar_n->parent()->remove(tar_n)) {
                    convert::parse(HttpRep(), rep);
                } else {
                    convert::parse(HttpRep(http::status::forbidden, "remove failed"), rep);
                }
            }
        });
    }

    { // cmd protocol
        _p->bind<http::verb::post>("/cmd/*", [](http::web_request& req, http::web_response& rep,
                                             const std::shared_ptr<NodeConnection>& state) {
            Pipeline pipe;
            auto cmd = req.path();
            cmd.remove_prefix(5);
            pipe.contextNode()->set("cmd", std::string(cmd));
            if (!schema::toNode<schema::JsonS>(pipe.contextNode()->at("params"), std::string(req.body()))) {
                convert::parse(HttpResultRep(Result::fail(ERR_INVALID, "bad request")), rep);
                return;
            }
            // Query options belong to the transport, never to command params.
            std::string query(req.query());
            bool background = false;
            std::istringstream parts(query);
            std::string part;
            while (std::getline(parts, part, '&')) {
                if (part == "async=1" || part == "async=true") background = true;
            }
            pipe.contextNode()->set("async", background);
            Private::execute(pipe, state->session, rep);
        });
    }

    { // standard protocol with envelope
        _p->bind<http::verb::post>("/ve", [](http::web_request& req, http::web_response& rep,
                                          const std::shared_ptr<NodeConnection>& state) {
            Pipeline pipe;
            if (!schema::toNode<schema::JsonS>(pipe.contextNode(), std::string(req.body()))) {
                convert::parse(HttpResultRep(Result::fail(ERR_INVALID, "invalid JSON")), rep);
                return;
            }
            Private::execute(pipe, state->session, rep);
        });
    }

    { // default
        _p->server.bind_not_found(_p->route([] (http::web_request&, http::web_response& rep) {
            convert::parse(HttpRep(http::status::not_found, "not found"), rep);
        }));
    }

    disableWindowsPortReuse(_p->server);
    return _p->server.start("0.0.0.0", _p->port);
}

void NodeHttpServer::stop(bool wait)
{
    _p->server.stop();
    _p->connections.stop(wait);
}

bool NodeHttpServer::isRunning() const
{
    return _p->server.is_started();
}

} // namespace service
} // namespace ve
