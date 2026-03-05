#include <ve/rtt/sdk_service.h>
#include <ve/rtt/global_data.h>

namespace imol {
namespace xservice {

SdkService::SdkService() = default;
SdkService::~SdkService() = default;

void SdkService::setServer(ServerNetObject* server)
{
    m_server = server;
}

int SdkService::handleMessage(const std::string& addr, const std::string& msg)
{
    // Parse JSON message
    // NOTE: Using SimpleJson for now — in a real build you would integrate
    // rapidjson/nlohmann here. This is a structural placeholder.
    (void)msg;

    // Expected format:
    // { "id": "...", "g": { key: null, ... }, "s": { key: value, ... }, "c": { key: args, ... } }
    //
    // The actual JSON parsing would produce a SimpleJson tree.
    // For the 1:1 port we show the structure; full parsing requires a JSON library.

    SimpleJson root;
    // TODO: parse msg into root

    JsonRef jr(root);

    SimpleJson response;
    response.setObject();

    // g: read data
    if (auto g = jr.at("g")) {
        SimpleJson g_result;
        g_result.setObject();
        for (const auto& data_key : g.objectKeys()) {
            g_result.set(data_key, gCmd(data_key));
        }
        response.set("g", g_result);
    }

    // s: write data
    if (auto s = jr.at("s")) {
        SimpleJson s_result;
        s_result.setObject();
        for (const auto& data_key : s.objectKeys()) {
            s_result.set(data_key, sCmd(data_key, s[data_key].value()));
        }
        response.set("s", s_result);
    }

    // c: execute commands
    if (auto c = jr.at("c")) {
        SimpleJson c_result;
        c_result.setObject();
        auto request_id = jr["id"].toString();
        for (const auto& cmd_key : c.objectKeys()) {
            SimpleJson cmd_result;
            bool sync = cCmd(addr, request_id, cmd_key, c[cmd_key].value(), cmd_result);
            if (sync) {
                c_result.set(cmd_key, cmd_result);
            }
            // async commands: result delivered via notify()
        }
        response.set("c", c_result);
    }

    // TODO: serialize response to JSON string and send
    // send(addr, serialize(response));
    return 0;
}

void SdkService::send(const std::string& addr, const std::string& content) const
{
    if (m_server) m_server->send(addr, content + "\r");
}

void SdkService::notify(const std::string& addr, const std::string& request_id,
                         const std::string& cmd_key, const SimpleJson& result) const
{
    SimpleJson msg;
    msg.setObject();
    if (!request_id.empty()) msg.set("id", SimpleJson(request_id));

    SimpleJson c;
    c.setObject();
    c.set(cmd_key, result);
    msg.set("c", c);

    // TODO: serialize msg and send
    // send(addr, serialize(msg));
}

SimpleJson SdkService::gCmd(const std::string& data_key) const
{
    auto* ji = data::ji(data_key);
    if (!ji) {
        return Result(ERR_G_NO_KEY, "no data key: " + data_key).toJson();
    }
    // TODO: call ji->serializeToJsonString() and parse to SimpleJson
    SimpleJson placeholder;
    placeholder.set("value", SimpleJson(ji->serializeToJsonString()));
    return placeholder;
}

SimpleJson SdkService::sCmd(const std::string& data_key, const SimpleJson& input) const
{
    auto* ji = data::ji(data_key);
    if (!ji) {
        return Result(ERR_S_DEFAULT, "no data key: " + data_key).toJson();
    }
    // TODO: serialize input to string and call ji->deserializeFromJsonString()
    bool ok = ji->deserializeFromJsonString(input.asString());
    return Result(ok, ERR_S_DEFAULT).toJson();
}

bool SdkService::cCmd(const std::string& addr, const std::string& request_id,
                       const std::string& cmd_key, const SimpleJson& input,
                       SimpleJson& out_result)
{
    // 1. Copy command template
    CommandObject* cobj = command::copy(cmd_key);
    if (!cobj) {
        out_result = Result(ERR_NO_CMD_KEY, "no command: " + cmd_key).toJson();
        return true;
    }

    // 2. Save session for async reply
    SimpleJson session;
    session.setObject();
    session.set("addr", SimpleJson(addr));
    session.set("id", SimpleJson(request_id));
    session.set("cmd", SimpleJson(cmd_key));
    cobj->setData("_session", session);

    // 3. Async result handler
    auto self = this;
    cobj->setResultHandler([self, addr, request_id, cmd_key](const Result& result) {
        self->notify(addr, request_id, cmd_key, result.toJson());
    });

    // 4. CIP input preprocessing
    if (m_cips.has(cmd_key)) {
        cobj->prependProc(m_cips.makeCipProc(cmd_key, input));
    } else {
        cobj->setInputData(input);
    }

    // 5. Start execution
    Result cmd_res = cobj->start();

    // 6. Sync: result immediately available
    if (!cmd_res.isAccepted()) {
        out_result = cmd_res.toJson();
        return true;
    }

    // 7. Async: result delivered via notify()
    return false;
}

} // namespace xservice
} // namespace imol
