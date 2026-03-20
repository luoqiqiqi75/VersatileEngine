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
    // Parse incoming JSON — discard on parse error.
    Json root = Json::parse(msg, nullptr, false);
    if (root.is_discarded()) return -1;

    JsonRef jr(root);

    Json response = Json::object();

    // ---- g: read data ---------------------------------------------------
    if (root.contains("g") && root["g"].is_object()) {
        Json g_result = Json::object();
        for (auto& [data_key, _] : root["g"].items()) {
            g_result[data_key] = gCmd(data_key);
        }
        response["g"] = g_result;
    }

    // ---- s: write data --------------------------------------------------
    if (root.contains("s") && root["s"].is_object()) {
        Json s_result = Json::object();
        for (auto& [data_key, val] : root["s"].items()) {
            s_result[data_key] = sCmd(data_key, val);
        }
        response["s"] = s_result;
    }

    // ---- c: execute commands --------------------------------------------
    if (root.contains("c") && root["c"].is_object()) {
        Json c_result = Json::object();
        auto request_id = jr["id"].toString();
        for (auto& [cmd_key, input] : root["c"].items()) {
            Json cmd_result;
            bool sync = cCmd(addr, request_id, cmd_key, input, cmd_result);
            if (sync) {
                c_result[cmd_key] = cmd_result;
            }
            // async commands: result delivered via notify()
        }
        response["c"] = c_result;
    }

    // Send response
    send(addr, response.dump());
    return 0;
}

void SdkService::send(const std::string& addr, const std::string& content) const
{
    if (m_server) m_server->send(addr, content + "\r");
}

void SdkService::notify(const std::string& addr, const std::string& request_id,
                         const std::string& cmd_key, const Json& result) const
{
    Json msg = Json::object();
    if (!request_id.empty()) msg["id"] = request_id;

    Json c = Json::object();
    c[cmd_key] = result;
    msg["c"] = c;

    send(addr, msg.dump());
}

Json SdkService::gCmd(const std::string& data_key) const
{
    auto* ji = data::ji(data_key);
    if (!ji) {
        return Result(ERR_G_NO_KEY, "no data key: " + data_key).toJson();
    }
    // Serialize the data object → JSON string → parse back to Json object.
    std::string json_str = ji->serializeToJsonString();
    Json parsed = Json::parse(json_str, nullptr, false);
    if (parsed.is_discarded()) {
        // Fallback: wrap the raw string.
        return Json({{"value", json_str}});
    }
    return parsed;
}

Json SdkService::sCmd(const std::string& data_key, const Json& input) const
{
    auto* ji = data::ji(data_key);
    if (!ji) {
        return Result(ERR_S_DEFAULT, "no data key: " + data_key).toJson();
    }
    // Dump the input Json to a string and feed it to the data object.
    bool ok = ji->deserializeFromJsonString(input.dump());
    return Result(ok, ERR_S_DEFAULT).toJson();
}

bool SdkService::cCmd(const std::string& addr, const std::string& request_id,
                       const std::string& cmd_key, const Json& input,
                       Json& out_result)
{
    // 1. Copy command template
    CommandObject* cobj = command::copy(cmd_key);
    if (!cobj) {
        out_result = Result(ERR_NO_CMD_KEY, "no command: " + cmd_key).toJson();
        return true;
    }

    // 2. Save session for async reply
    Json session = Json::object();
    session["addr"] = addr;
    session["id"]   = request_id;
    session["cmd"]  = cmd_key;
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
