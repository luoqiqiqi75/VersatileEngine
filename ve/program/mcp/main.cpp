#include <ve/entry.h>
#include <ve/core/command.h>
#include <ve/core/impl/json.h>
#include <ve/core/log.h>

#include <algorithm>
#include <cctype>
#include <iostream>
#include <string>
#include <vector>

namespace {

using ve::Var;
using ve::impl::json::parse;
using ve::impl::json::stringify;

static std::string trimCrlf(std::string s)
{
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) {
        s.pop_back();
    }
    return s;
}

static bool readMessage(std::istream& in, std::string& out)
{
    out.clear();
    std::string line;
    std::size_t contentLength = 0;
    bool haveLength = false;

    while (std::getline(in, line)) {
        line = trimCrlf(line);
        if (line.empty()) {
            break;
        }
        const std::string prefix = "Content-Length:";
        if (line.rfind(prefix, 0) == 0) {
            std::string len = line.substr(prefix.size());
            len.erase(0, len.find_first_not_of(" \t"));
            try {
                contentLength = static_cast<std::size_t>(std::stoul(len));
                haveLength = true;
            } catch (...) {
                return false;
            }
        }
    }

    if (!haveLength) {
        return false;
    }
    out.resize(contentLength);
    in.read(out.data(), static_cast<std::streamsize>(contentLength));
    return static_cast<std::size_t>(in.gcount()) == contentLength;
}

static void writeMessage(std::ostream& out, const std::string& json)
{
    out << "Content-Length: " << json.size() << "\r\n\r\n";
    out << json;
    out.flush();
}

static Var makeErrorObj(int code, const std::string& msg)
{
    Var::DictV err;
    err["code"] = Var(code);
    err["message"] = Var(msg);
    return Var(std::move(err));
}

static Var responseWithResult(const Var& id, const Var& result)
{
    Var::DictV rep;
    rep["jsonrpc"] = Var("2.0");
    rep["id"] = id;
    rep["result"] = result;
    return Var(std::move(rep));
}

static Var responseWithError(const Var& id, int code, const std::string& msg)
{
    Var::DictV rep;
    rep["jsonrpc"] = Var("2.0");
    rep["id"] = id;
    rep["error"] = makeErrorObj(code, msg);
    return Var(std::move(rep));
}

static Var makeInitializeResult()
{
    Var::DictV serverInfo;
    serverInfo["name"] = Var("ve_mcp");
    serverInfo["version"] = Var("0.1.0");

    Var::DictV toolsCaps;
    toolsCaps["listChanged"] = Var(false);

    Var::DictV capabilities;
    capabilities["tools"] = Var(std::move(toolsCaps));

    Var::DictV result;
    result["protocolVersion"] = Var("2024-11-05");
    result["capabilities"] = Var(std::move(capabilities));
    result["serverInfo"] = Var(std::move(serverInfo));
    return Var(std::move(result));
}

static Var makeToolsListResult()
{
    auto keys = ve::command::keys();
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());

    Var::ListV tools;
    tools.reserve(keys.size());

    for (const auto& key : keys) {
        Var::DictV schema;
        schema["type"] = Var("object");
        schema["additionalProperties"] = Var(true);

        Var::DictV tool;
        tool["name"] = Var(key);
        std::string help = ve::command::help(key);
        tool["description"] = Var(help.empty() ? ("VE command: " + key) : help);
        tool["inputSchema"] = Var(std::move(schema));
        tools.push_back(Var(std::move(tool)));
    }

    Var::DictV result;
    result["tools"] = Var(std::move(tools));
    return Var(std::move(result));
}

static Var makeToolCallResult(const std::string& key, const Var& args)
{
    auto r = ve::command::call(key, args);
    bool ok = r.isSuccess() || r.isAccepted();

    Var::ListV content;
    Var::DictV textItem;
    textItem["type"] = Var("text");
    if (ok) {
        textItem["text"] = Var(stringify(r.content()));
    } else {
        textItem["text"] = Var(Var(r).toString());
    }
    content.push_back(Var(std::move(textItem)));

    Var::DictV result;
    result["content"] = Var(std::move(content));
    result["isError"] = Var(!ok);
    return Var(std::move(result));
}

static std::string parseConfigFromArgs(int argc, char** argv)
{
    std::string cfg = "ve.json";
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--config" || arg == "-c") && i + 1 < argc) {
            cfg = argv[++i];
        } else if (!arg.empty() && arg[0] != '-') {
            cfg = arg;
        }
    }
    return cfg;
}

} // namespace

int main(int argc, char** argv)
{
    const std::string configPath = parseConfigFromArgs(argc, argv);

    ve::entry::setup(configPath);
    ve::entry::init();

    std::string message;
    while (readMessage(std::cin, message)) {
        Var req = parse(message);
        if (!req.isDict()) {
            continue;
        }
        auto& reqObj = req.toDict();

        Var id;
        if (reqObj.has("id")) {
            id = reqObj["id"];
        } else {
            // Notification: no response required.
            continue;
        }

        std::string method = reqObj.has("method") ? reqObj["method"].toString() : "";
        Var reply;

        if (method == "initialize") {
            reply = responseWithResult(id, makeInitializeResult());
        } else if (method == "tools/list") {
            reply = responseWithResult(id, makeToolsListResult());
        } else if (method == "tools/call") {
            if (!reqObj.has("params") || !reqObj["params"].isDict()) {
                reply = responseWithError(id, -32602, "invalid params");
            } else {
                auto& params = reqObj["params"].toDict();
                std::string toolName = params.has("name") ? params["name"].toString() : "";
                if (toolName.empty()) {
                    reply = responseWithError(id, -32602, "tool name required");
                } else {
                    Var args = params.has("arguments") ? params["arguments"] : Var(Var::DictV{});
                    reply = responseWithResult(id, makeToolCallResult(toolName, args));
                }
            }
        } else {
            reply = responseWithError(id, -32601, "method not found: " + method);
        }

        writeMessage(std::cout, stringify(reply));
    }

    ve::entry::deinit();
    return 0;
}
