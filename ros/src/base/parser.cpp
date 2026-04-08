#include "ve/ros/parser.h"

#include "ve/core/schema.h"

#include <yaml-cpp/yaml.h>

#include <cctype>
#include <mutex>

namespace ve::ros {

namespace {

struct Registry {
    std::mutex mu;
    Dict<ParserDescriptor> canonical;
    Dict<std::string> alias_to_key;
    Strings order;
};

Registry& registry()
{
    static Registry r;
    return r;
}

std::string lowerCopy(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

std::string normalizeKey(const std::string& key)
{
    return lowerCopy(key);
}

bool looksLikeJson(const std::string& text)
{
    const auto first = std::find_if(text.begin(), text.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    });
    return first != text.end() && (*first == '{' || *first == '[');
}

Var yamlNodeToVar(const YAML::Node& node)
{
    if (!node || node.IsNull())
        return Var();

    if (node.IsScalar()) {
        const std::string text = node.as<std::string>("");
        if (text == "true")
            return Var(true);
        if (text == "false")
            return Var(false);
        try {
            std::size_t pos = 0;
            const long long int_v = std::stoll(text, &pos);
            if (pos == text.size())
                return Var(static_cast<int64_t>(int_v));
        } catch (...) {
        }
        try {
            std::size_t pos = 0;
            const double double_v = std::stod(text, &pos);
            if (pos == text.size())
                return Var(double_v);
        } catch (...) {
        }
        return Var(text);
    }

    if (node.IsSequence()) {
        Var::ListV list;
        for (const auto& item : node)
            list.push_back(yamlNodeToVar(item));
        return Var(std::move(list));
    }

    Var::DictV dict;
    for (const auto& kv : node)
        dict[kv.first.as<std::string>("")] = yamlNodeToVar(kv.second);
    return Var(std::move(dict));
}

void yamlToTree(const YAML::Node& yaml, Node* node)
{
    if (!node)
        return;

    node->clear();
    node->set(Var());

    if (!yaml || yaml.IsNull()) {
        node->set(Var());
        return;
    }

    if (yaml.IsScalar()) {
        node->set(yamlNodeToVar(yaml));
        return;
    }

    if (yaml.IsSequence()) {
        for (std::size_t i = 0; i < yaml.size(); ++i)
            yamlToTree(yaml[i], node->at(static_cast<int>(i)));
        return;
    }

    for (const auto& kv : yaml) {
        const std::string key = kv.first.as<std::string>("");
        if (!key.empty())
            yamlToTree(kv.second, node->at(key));
    }
}

bool applySchemaJson(const std::string& json_text, Node* target, Node* schema_node)
{
    if (!target)
        return false;

    if (schema_node)
        target->copy(schema_node, true, true, false);

    schema::ImportOptions options;
    options.auto_insert = schema_node == nullptr;
    options.auto_remove = false;
    options.auto_update = true;
    return schema::importAs<schema::JsonS>(target, json_text, options);
}

void insertParserLocked(const ParserDescriptor& parser)
{
    if (parser.key.empty() || !parser.parse)
        return;
    if (!registry().canonical.has(parser.key))
        registry().order.push_back(parser.key);
    registry().canonical.insertOne(parser.key, parser);
    registry().alias_to_key.insertOne(parser.key, parser.key);
    for (const auto& alias : parser.aliases) {
        if (!alias.empty())
            registry().alias_to_key.insertOne(alias, parser.key);
    }
}

std::string resolveKeyLocked(const std::string& key)
{
    const std::string normalized = normalizeKey(key);
    if (registry().canonical.has(normalized))
        return normalized;
    return registry().alias_to_key.value(normalized);
}

void registerBuiltins()
{
    static const bool once = []() {
        std::lock_guard<std::mutex> lock(registry().mu);

        insertParserLocked(ParserDescriptor{
            "string",
            {"raw_string"},
            "Store the raw payload string directly into the target node.",
            [](const ParseRequest& request, std::string& error) -> bool {
                if (!request.target) {
                    error = "target node is null";
                    return false;
                }
                request.target->clear();
                request.target->set(Var(request.payload));
                return true;
            }
        });

        insertParserLocked(ParserDescriptor{
            "yaml",
            {},
            "Parse YAML text into a VE node tree.",
            [](const ParseRequest& request, std::string& error) -> bool {
                if (!request.target) {
                    error = "target node is null";
                    return false;
                }
                try {
                    yamlToTree(YAML::Load(request.payload), request.target);
                    return true;
                } catch (const std::exception& e) {
                    error = e.what();
                    return false;
                }
            }
        });

        insertParserLocked(ParserDescriptor{
            "json",
            {},
            "Parse JSON text into a VE node tree, optionally using a schema node.",
            [](const ParseRequest& request, std::string& error) -> bool {
                if (!request.target) {
                    error = "target node is null";
                    return false;
                }
                if (!applySchemaJson(request.payload, request.target, request.schema)) {
                    error = "json import failed";
                    return false;
                }
                return true;
            }
        });

        insertParserLocked(ParserDescriptor{
            "json_string",
            {},
            "Extract a scalar JSON string from YAML/string payload and import it as JSON.",
            [](const ParseRequest& request, std::string& error) -> bool {
                if (!request.target) {
                    error = "target node is null";
                    return false;
                }

                std::string json_text;
                try {
                    const YAML::Node yaml = YAML::Load(request.payload);
                    if (yaml.IsScalar()) {
                        json_text = yaml.as<std::string>("");
                    } else if (yaml.IsMap() && yaml["data"] && yaml["data"].IsScalar()) {
                        json_text = yaml["data"].as<std::string>("");
                    } else {
                        error = "json_string parser expects scalar or data scalar";
                        return false;
                    }
                } catch (...) {
                    json_text = request.payload;
                }

                if (!looksLikeJson(json_text)) {
                    error = "json_string payload is not json";
                    return false;
                }
                if (!applySchemaJson(json_text, request.target, request.schema)) {
                    error = "json_string import failed";
                    return false;
                }
                return true;
            }
        });

        return true;
    }();
    (void)once;
}

} // namespace

void registerParser(ParserDescriptor parser)
{
    registerBuiltins();
    if (parser.key.empty() || !parser.parse)
        return;

    parser.key = normalizeKey(parser.key);
    for (auto& alias : parser.aliases)
        alias = normalizeKey(alias);

    std::lock_guard<std::mutex> lock(registry().mu);
    insertParserLocked(parser);
}

bool hasParser(const std::string& key)
{
    registerBuiltins();
    std::lock_guard<std::mutex> lock(registry().mu);
    return !resolveKeyLocked(key).empty();
}

bool parsePayload(const std::string& key,
                  const std::string& payload,
                  Node* target,
                  Node* schema,
                  std::string& error)
{
    registerBuiltins();

    ParseFn fn;
    {
        std::lock_guard<std::mutex> lock(registry().mu);
        const std::string resolved = resolveKeyLocked(key.empty() ? "yaml" : key);
        if (resolved.empty()) {
            error = "unknown parser: " + (key.empty() ? std::string("<default>") : key);
            return false;
        }
        fn = registry().canonical.value(resolved).parse;
    }

    return fn(ParseRequest{payload, target, schema}, error);
}

Var::ListV parserInfoList()
{
    registerBuiltins();

    Var::ListV list;
    std::lock_guard<std::mutex> lock(registry().mu);
    for (const auto& key : registry().order) {
        const auto& parser = registry().canonical.value(key);
        Var::DictV item;
        item["key"] = Var(parser.key);
        item["summary"] = Var(parser.summary);
        Var::ListV aliases;
        for (const auto& alias : parser.aliases)
            aliases.push_back(Var(alias));
        item["aliases"] = Var(std::move(aliases));
        list.push_back(Var(std::move(item)));
    }
    return list;
}

Strings parserKeys()
{
    registerBuiltins();
    std::lock_guard<std::mutex> lock(registry().mu);
    return registry().order;
}

} // namespace ve::ros
