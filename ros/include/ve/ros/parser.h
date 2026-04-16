#pragma once

#include "ve/core/node.h"

namespace ve::ros {

struct ParseRequest {
    std::string payload;
    Node* target = nullptr;
    Node* schema = nullptr;
};

using ParseFn = std::function<bool(const ParseRequest&, std::string&)>;

struct ParserDescriptor {
    std::string key;
    Strings aliases;
    std::string summary;
    ParseFn parse;
};

VE_API void registerParser(ParserDescriptor parser);
VE_API bool hasParser(const std::string& key);
VE_API bool parsePayload(const std::string& key,
                         const std::string& payload,
                         Node* target,
                         Node* schema,
                         std::string& error);
VE_API Var::ListV parserInfoList();
VE_API Strings parserKeys();

} // namespace ve::ros
