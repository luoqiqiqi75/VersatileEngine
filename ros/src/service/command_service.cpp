// command_service.cpp — ve::dds::CommandService + YAML utilities

#include "ve/ros/service/command_service.h"

#include <fastrtps/types/DynamicTypeBuilderFactory.h>
#include <fastrtps/types/DynamicTypeBuilder.h>
#include <fastrtps/types/DynamicType.h>
#include <fastrtps/types/DynamicPubSubType.h>
#include <fastrtps/types/DynamicData.h>
#include <fastrtps/types/DynamicDataFactory.h>

namespace ve::dds {

namespace ftypes = eprosima::fastrtps::types;
namespace fdds   = eprosima::fastdds::dds;

namespace {

inline bool hasNodeValue(const Node* node)
{
    return node && !node->get().isNull();
}

} // namespace

// ============================================================================
// YAML ↔ Var conversion
// ============================================================================

namespace yaml {

YAML::Node varToYaml(const Var& v)
{
    switch (v.type()) {
    case Var::NONE:   return YAML::Node(YAML::NodeType::Null);
    case Var::BOOL:   return YAML::Node(v.toBool());
    case Var::INT:    return YAML::Node(v.toInt());
    case Var::DOUBLE: return YAML::Node(v.toDouble());
    case Var::STRING: return YAML::Node(v.toString());
    case Var::LIST: {
        YAML::Node yn(YAML::NodeType::Sequence);
        for (auto& item : v.toList())
            yn.push_back(varToYaml(item));
        return yn;
    }
    case Var::DICT: {
        YAML::Node yn(YAML::NodeType::Map);
        for (auto& kv : v.toDict())
            yn[kv.first] = varToYaml(kv.second);
        return yn;
    }
    default:
        return YAML::Node(v.toString());
    }
}

Var yamlToVar(const YAML::Node& yn)
{
    if (!yn.IsDefined() || yn.IsNull())
        return Var();

    if (yn.IsScalar()) {
        auto s = yn.Scalar();
        if (s == "true" || s == "false")
            return Var(yn.as<bool>());
        try {
            auto i = yn.as<int64_t>();
            auto d = yn.as<double>();
            if (static_cast<double>(i) == d)
                return Var(i);
            return Var(d);
        } catch (...) {}
        return Var(s);
    }

    if (yn.IsSequence()) {
        Var::ListV list;
        for (auto it = yn.begin(); it != yn.end(); ++it)
            list.push_back(yamlToVar(*it));
        return Var(std::move(list));
    }

    if (yn.IsMap()) {
        Var::DictV dict;
        for (auto it = yn.begin(); it != yn.end(); ++it)
            dict[it->first.as<std::string>()] = yamlToVar(it->second);
        return Var(std::move(dict));
    }

    return Var();
}

YAML::Node nodeToYaml(Node* n)
{
    if (!n) return YAML::Node(YAML::NodeType::Null);

    if (n->count() == 0) {
        return varToYaml(n->get());
    }

    YAML::Node yn(YAML::NodeType::Map);
    if (hasNodeValue(n))
        yn["_value"] = varToYaml(n->get());

    for (auto* child : *n) {
        auto name = child->name();
        if (name.empty()) continue;
        yn[name] = nodeToYaml(child);
    }
    return yn;
}

void yamlToNode(const YAML::Node& yn, Node* n)
{
    if (!n || !yn.IsDefined()) return;

    if (yn.IsScalar() || yn.IsNull()) {
        n->set(yamlToVar(yn));
        return;
    }

    if (yn.IsMap()) {
        for (auto it = yn.begin(); it != yn.end(); ++it) {
            auto key = it->first.as<std::string>();
            if (key == "_value") {
                n->set(yamlToVar(it->second));
            } else {
                auto* child = n->at(key);
                yamlToNode(it->second, child);
            }
        }
    }
}

std::string encode(const Var& v)
{
    YAML::Emitter out;
    out << varToYaml(v);
    return out.c_str();
}

std::string encode(Node* n)
{
    YAML::Emitter out;
    out << nodeToYaml(n);
    return out.c_str();
}

Var decode(const std::string& yamlStr)
{
    try {
        auto yn = YAML::Load(yamlStr);
        return yamlToVar(yn);
    } catch (...) {
        return Var();
    }
}

} // namespace yaml

// ============================================================================
// CommandService — Private
// ============================================================================

struct CommandService::Private
{
    Participant* participant = nullptr;
    std::string  prefix;
    bool         running = false;

    ftypes::DynamicType_ptr cmdReqType;
    ftypes::DynamicType_ptr cmdRepType;
    ftypes::DynamicData*    cmdReqData = nullptr;
    ftypes::DynamicData*    cmdRepData = nullptr;
};

// ============================================================================
// Helper: create a DDS service from Dynamic Types
// ============================================================================

static void createStringService(
    Participant& p,
    const std::string& srvName,
    ftypes::DynamicType_ptr reqType,
    ftypes::DynamicType_ptr repType,
    ftypes::DynamicData* reqData,
    ftypes::DynamicData* repData,
    std::function<void(const std::string& command,
                       const std::string& path,
                       const std::string& args,
                       int& outCode,
                       std::string& outResult)> handler)
{
    fdds::TypeSupport reqTs(new ftypes::DynamicPubSubType(reqType));
    fdds::TypeSupport repTs(new ftypes::DynamicPubSubType(repType));

    p.createTopicGroup(Participant::TopicGroupInfo{
        srvName,
        {"rq/" + srvName + "Request", std::move(reqTs)},
        {"rr/" + srvName + "Reply",   std::move(repTs)},
        [reqData, repData, handler](Participant::DataReaderT* r,
                                     Participant::DataWriterT* w) -> int {
            fdds::SampleInfo si;
            while (r->get_unread_count() > 0) {
                auto rc = r->take_next_sample(reqData, &si);
                if (rc != eprosima::fastrtps::types::ReturnCode_t::RETCODE_OK)
                    continue;
                if (si.instance_state != fdds::ALIVE_INSTANCE_STATE)
                    continue;

                std::string command, path, args;
                reqData->get_string_value(command, 0);
                reqData->get_string_value(path, 1);
                reqData->get_string_value(args, 2);

                int code = 0;
                std::string result;
                handler(command, path, args, code, result);

                repData->set_int32_value(code, 0);
                repData->set_string_value(result, 1);

                eprosima::fastrtps::rtps::WriteParams wp;
                wp.related_sample_identity().writer_guid(
                    si.sample_identity.writer_guid());
                wp.related_sample_identity().sequence_number(
                    si.sample_identity.sequence_number());
                w->write(reinterpret_cast<void*>(repData), wp);
            }
            return 0;
        }
    });
}

// ============================================================================
// Lifecycle
// ============================================================================

CommandService::CommandService(Participant& p, const std::string& prefix)
    : _p(new Private)
{
    _p->participant = &p;
    _p->prefix = prefix;
}

CommandService::~CommandService()
{
    if (_p->cmdReqData)
        ftypes::DynamicDataFactory::get_instance()->delete_data(_p->cmdReqData);
    if (_p->cmdRepData)
        ftypes::DynamicDataFactory::get_instance()->delete_data(_p->cmdRepData);
    delete _p;
}

void CommandService::start()
{
    if (_p->running) return;
    _p->running = true;

    auto* factory = ftypes::DynamicTypeBuilderFactory::get_instance();

    // Request: { string command; string path; string args; }
    auto reqBuilder = factory->create_struct_builder();
    reqBuilder->set_name(_p->prefix + "_CommandRequest");
    reqBuilder->add_member(0, "command", factory->create_string_type());
    reqBuilder->add_member(1, "path",    factory->create_string_type());
    reqBuilder->add_member(2, "args",    factory->create_string_type());
    _p->cmdReqType = reqBuilder->build();

    // Reply: { int32 code; string result; }
    auto repBuilder = factory->create_struct_builder();
    repBuilder->set_name(_p->prefix + "_CommandReply");
    repBuilder->add_member(0, "code",   factory->create_int32_type());
    repBuilder->add_member(1, "result", factory->create_string_type());
    _p->cmdRepType = repBuilder->build();

    _p->cmdReqData = ftypes::DynamicDataFactory::get_instance()
        ->create_data(_p->cmdReqType);
    _p->cmdRepData = ftypes::DynamicDataFactory::get_instance()
        ->create_data(_p->cmdRepType);

    auto pre = _p->prefix;

    // === Generic command service ===
    // { command: "ls", path: "/robot", args: "--tree" }
    createStringService(*_p->participant,
        pre + "/command",
        _p->cmdReqType, _p->cmdRepType,
        _p->cmdReqData, _p->cmdRepData,
        [](const std::string& command, const std::string& path,
           const std::string& args, int& outCode, std::string& outResult)
        {
            Var::ListV input;
            if (!path.empty())
                input.push_back(Var(path));
            if (!args.empty()) {
                std::istringstream iss(args);
                std::string token;
                while (iss >> token)
                    input.push_back(Var(token));
            }
            Var inputVar = input.empty() ? Var() : Var(std::move(input));
            Result r = command::call(command, inputVar);
            outCode = r.code();
            outResult = r.content().toString();
        });

    // === data_list (ls) ===
    auto* listReq = ftypes::DynamicDataFactory::get_instance()
        ->create_data(_p->cmdReqType);
    auto* listRep = ftypes::DynamicDataFactory::get_instance()
        ->create_data(_p->cmdRepType);

    createStringService(*_p->participant,
        pre + "/data_list",
        _p->cmdReqType, _p->cmdRepType,
        listReq, listRep,
        [](const std::string&, const std::string& path,
           const std::string&, int& outCode, std::string& outResult)
        {
            auto* root = node::root();
            auto* target = path.empty() || path == "/"
                ? root : root->find(path, false);
            if (!target) {
                outCode = Result::FAIL;
                outResult = "not found: " + path;
                return;
            }
            YAML::Node yn(YAML::NodeType::Map);
            for (auto* c : *target) {
                auto name = c->name();
                if (name.empty()) continue;
                yn[name] = yaml::nodeToYaml(c);
            }
            YAML::Emitter out;
            out << yn;
            outCode = Result::SUCCESS;
            outResult = out.c_str();
        });

    // === data_get ===
    auto* getReq = ftypes::DynamicDataFactory::get_instance()
        ->create_data(_p->cmdReqType);
    auto* getRep = ftypes::DynamicDataFactory::get_instance()
        ->create_data(_p->cmdRepType);

    createStringService(*_p->participant,
        pre + "/data_get",
        _p->cmdReqType, _p->cmdRepType,
        getReq, getRep,
        [](const std::string&, const std::string& path,
           const std::string&, int& outCode, std::string& outResult)
        {
            auto* root = node::root();
            auto* target = path.empty() || path == "/"
                ? root : root->find(path, false);
            if (!target) {
                outCode = Result::FAIL;
                outResult = "not found: " + path;
                return;
            }
            outCode = Result::SUCCESS;
            outResult = yaml::encode(target);
        });

    // === data_set ===
    auto* setReq = ftypes::DynamicDataFactory::get_instance()
        ->create_data(_p->cmdReqType);
    auto* setRep = ftypes::DynamicDataFactory::get_instance()
        ->create_data(_p->cmdRepType);

    createStringService(*_p->participant,
        pre + "/data_set",
        _p->cmdReqType, _p->cmdRepType,
        setReq, setRep,
        [](const std::string&, const std::string& path,
           const std::string& args, int& outCode, std::string& outResult)
        {
            auto* root = node::root();
            auto* target = (path.empty() || path == "/") ? root : root->at(path);
            if (!target) {
                outCode = Result::FAIL;
                outResult = "cannot ensure: " + path;
                return;
            }
            try {
                auto yn = YAML::Load(args);
                if (yn.IsMap()) {
                    yaml::yamlToNode(yn, target);
                } else {
                    target->set(yaml::yamlToVar(yn));
                }
                outCode = Result::SUCCESS;
                outResult = "ok";
            } catch (const std::exception& e) {
                outCode = Result::FAIL;
                outResult = std::string("YAML parse error: ") + e.what();
            }
        });

    veLogIs("ve::dds::CommandService", "started with prefix:", pre,
            "services:", pre + "/command,", pre + "/data_list,",
            pre + "/data_get,", pre + "/data_set");
}

void CommandService::stop()
{
    _p->running = false;
}

bool CommandService::isRunning() const
{
    return _p->running;
}

} // namespace ve::dds
