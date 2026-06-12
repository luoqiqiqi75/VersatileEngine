// node_http_service.cpp — ve::service::NodeHttpServer
#include "ve/service/node_service.h"
#include "ve/core/command.h"
#include "ve/core/node.h"
#include "ve/core/pipeline.h"
#include "ve/core/schema.h"
#include "server_util.h"

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <asio2/http/http_server.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <chrono>
#include <algorithm>
#include <memory>
#include <string>
#include <string_view>

namespace ve {

// defines
namespace service {

static const schema::ExportOptions<schema::JsonS> compactJson{0};

// HTTP-specific key separator: ':' instead of '#' so that
// /at/leo/mu:1 is reachable from a browser without URL-encoding
// (URL '#' is the fragment delimiter, truncated by browsers).
static constexpr char HTTP_KEY_SEP = ':';

enum JsonRpcError
{
    JRpcParseError     = -32700,
    JRpcInvalidRequest = -32600,
    JRpcMethodNotFound = -32601,
    JRpcInvalidParams  = -32602,
    JRpcInternalError  = -32603,
    JRpcServerError    = -32000
};

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
    return parse(service::HttpRep { status, schema::exportAs<schema::JsonS>(&proto_n, service::compactJson) }, rep);
}

}

namespace service {

static std::string toJson(Node& n)
{
    return schema::exportAs<schema::JsonS>(&n, compactJson);
}

static void fillError(Node* rep, const std::string& code, const std::string& error)
{
    if (!rep) return;
    rep->clear();
    rep->set(Var());
    rep->set("ok", false);
    rep->set("code", code);
    rep->set("error", error);
}

static std::string getQueryParam(std::string_view query, std::string_view key)
{
    size_t pos = 0;
    while (pos < query.size()) {
        size_t eq = query.find('=', pos);
        size_t amp = query.find('&', pos);
        if (amp == std::string_view::npos) {
            amp = query.size();
        }
        if (eq == std::string_view::npos || eq > amp) {
            if (query.substr(pos, amp - pos) == key) {
                return "1";
            }
        } else if (query.substr(pos, eq - pos) == key) {
            return std::string(query.substr(eq + 1, amp - eq - 1));
        }
        pos = amp + 1;
    }
    return {};
}

static bool queryBool(std::string_view query, std::string_view key, bool def = false)
{
    std::string value = getQueryParam(query, key);
    if (value.empty()) {
        return def;
    }
    if (value == "0" || value == "false" || value == "False" || value == "FALSE"
        || value == "no" || value == "No" || value == "NO") {
        return false;
    }
    return true;
}

static int queryInt(std::string_view query, std::string_view key, int def)
{
    std::string value = getQueryParam(query, key);
    if (value.empty()) {
        return def;
    }
    try {
        return std::stoi(value);
    } catch (...) {
        return def;
    }
}

struct NodeHttpServer::Private
{
    Object   obj;
    Node*    root = nullptr;
    uint16_t port = 12000;

    asio2::http_server server;
    std::chrono::steady_clock::time_point startTime;
};

NodeHttpServer::NodeHttpServer(const Node* config_n) : _p(std::make_unique<Private>())
{
    _p->root = ve::n(config_n->get("root").toString("/"));
    _p->port = config_n->get("port").toInt(0); // default stop
}

NodeHttpServer::~NodeHttpServer()
{
    stop();
}

bool NodeHttpServer::start()
{
    auto& http_f = factory::at("standard/http");

    { // health protocol
        _p->startTime = std::chrono::steady_clock::now();

        auto* http_timestamp_fn = ve::command::reg(http_f, "timestamp", [=] {
            auto elapsed = std::chrono::steady_clock::now() - _p->startTime;
            auto seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
            return seconds;
        });

        _p->server.bind<http::verb::get>("/health", [=] (http::web_request&, http::web_response& rep) {
            int s = Command(http_timestamp_fn).run().outputNode()->getInt64();
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
                    convert::parse(HttpResultRep(Result::fail(JRpcParseError, "invalid json")), rep); // todo error code control
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
                    convert::parse(HttpResultRep(Result::fail(JRpcParseError, "invalid json")), rep); // todo error code control
                }
            }
        });

        _p->server.bind<http::verb::delete_>("/at/*", [tar_n_f] (http::web_request& req, http::web_response& rep) {
            if (auto const tar_n = tar_n_f(req, rep)) {
                if (tar_n->parent()->remove(tar_n)) {
                    convert::parse(HttpRep(), rep);
                } else {
                    convert::parse(HttpResultRep{Result::fail(JRpcInternalError, "failed")}, rep);
                }
            }
        });
    }

    { // cmd protocol
        _p->server.bind<http::verb::post>("/cmd/*", [o = &_p->obj] (http::web_request& req, http::web_response& rep) {
            auto cmd_sv = req.path();
            cmd_sv.remove_prefix(5); // /cmd/
            std::string cmd_key(cmd_sv);
            Command cmd = command::create(cmd_key);
            if (!cmd.valid()) {
                convert::parse(HttpResultRep(Result::fail(JRpcMethodNotFound, "unknown command")), rep);
                return;
            }

            Pipeline pipe;
            if (!schema::importAs<schema::JsonS>(pipe.inputNode(), req.body())) {
                convert::parse(HttpResultRep(Result::fail(JRpcInvalidRequest, "bad request")), rep);
                return;
            }
            pipe.add(cmd);

            if (queryBool(req.query(), "async", false)) {
               pipe.onFinished(o, [guard = rep.defer(), rep_ptr = &rep] (Pipeline& p) {
                   convert::parse(HttpResultRep(p.result(), p.outputNode()), *rep_ptr);
               });
               pipe.async();
           } else {
               pipe.sync();
               convert::parse(HttpResultRep(pipe.result(), pipe.outputNode()), rep);
           }
        });
    }

    _p->server.bind_not_found([] (http::web_request&, http::web_response& rep) {
        Node reply("rep");
        fillError(&reply, "not_found", "not found");
        rep.fill_json(toJson(reply), http::status::not_found);
    });

    ve::service::disableWindowsPortReuse(_p->server);
    return _p->server.start("0.0.0.0", _p->port);
}

void NodeHttpServer::stop()
{
    _p->server.stop();
}

bool NodeHttpServer::isRunning() const
{
    return _p->server.is_started();
}

} // namespace service
} // namespace ve
