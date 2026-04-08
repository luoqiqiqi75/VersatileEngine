#include "ve/ros/yaml_schema.h"

namespace ve::ros::yaml {

YAML::Node varToYaml(const Var& v)
{
    switch (v.type()) {
    case Var::NONE:   return YAML::Node(YAML::NodeType::Null);
    case Var::BOOL:   return YAML::Node(v.toBool());
    case Var::INT:    return YAML::Node(v.toInt64());
    case Var::DOUBLE: return YAML::Node(v.toDouble());
    case Var::STRING: return YAML::Node(v.toString());
    case Var::LIST: {
        YAML::Node node(YAML::NodeType::Sequence);
        for (const auto& item : v.toList())
            node.push_back(varToYaml(item));
        return node;
    }
    case Var::DICT: {
        YAML::Node node(YAML::NodeType::Map);
        for (const auto& [key, value] : v.toDict())
            node[key] = varToYaml(value);
        return node;
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
        const std::string scalar = yn.Scalar();
        if (scalar == "true" || scalar == "false")
            return Var(yn.as<bool>());
        try {
            std::size_t pos = 0;
            const int64_t int_v = std::stoll(scalar, &pos);
            if (pos == scalar.size())
                return Var(int_v);
        } catch (...) {
        }
        try {
            std::size_t pos = 0;
            const double double_v = std::stod(scalar, &pos);
            if (pos == scalar.size())
                return Var(double_v);
        } catch (...) {
        }
        return Var(scalar);
    }

    if (yn.IsSequence()) {
        Var::ListV list;
        for (auto it = yn.begin(); it != yn.end(); ++it)
            list.push_back(yamlToVar(*it));
        return Var(std::move(list));
    }

    Var::DictV dict;
    for (auto it = yn.begin(); it != yn.end(); ++it)
        dict[it->first.as<std::string>()] = yamlToVar(it->second);
    return Var(std::move(dict));
}

YAML::Node nodeToYaml(Node* n)
{
    const std::string encoded = schema::exportAs<schema::YamlS>(n);
    return YAML::Load(encoded);
}

void yamlToNode(const YAML::Node& yn, Node* n)
{
    if (!n)
        return;
    YAML::Emitter emitter;
    emitter << yn;
    schema::importAs<schema::YamlS>(n, std::string(emitter.c_str()));
}

std::string encode(const Var& v)
{
    YAML::Emitter emitter;
    emitter << varToYaml(v);
    return emitter.c_str();
}

std::string encode(Node* n)
{
    return schema::exportAs<schema::YamlS>(n);
}

Var decode(const std::string& yaml_str)
{
    try {
        return yamlToVar(YAML::Load(yaml_str));
    } catch (...) {
        return Var();
    }
}

} // namespace ve::ros::yaml

namespace ve::schema {

std::string SchemaTraits<YamlS>::exportNode(const Node* node, int indent)
{
    return exportNode(node, ExportOptions{indent, false});
}

std::string SchemaTraits<YamlS>::exportNode(const Node* node, const ExportOptions&)
{
    YAML::Emitter emitter;
    emitter << ve::ros::yaml::varToYaml(schema::exportAs<schema::VarS>(node));
    return emitter.c_str();
}

bool SchemaTraits<YamlS>::importNode(Node* node, const std::string& data)
{
    return importNode(node, data, ImportOptions{});
}

bool SchemaTraits<YamlS>::importNode(Node* node,
                                     const std::string& data,
                                     const ImportOptions& options)
{
    if (!node)
        return false;

    try {
        const Var decoded = ve::ros::yaml::yamlToVar(YAML::Load(data));
        return schema::importAs<schema::VarS>(node, decoded, options);
    } catch (...) {
        return false;
    }
}

namespace {

const bool yaml_schema_registered = []() {
    registerSchemaFormat("yaml", {
        [](const Node* node) -> std::string {
            return exportAs<YamlS>(node);
        },
        [](Node* node, const std::string& data) -> bool {
            return importAs<YamlS>(node, data);
        }
    });
    return true;
}();

} // namespace

} // namespace ve::schema
