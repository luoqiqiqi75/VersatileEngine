// ----------------------------------------------------------------------------
// bin.cpp — Binary serialization for Var and Node (MessagePack-based)
// ----------------------------------------------------------------------------
#include "ve/core/impl/bin.h"
#include "ve/core/node.h"

#define MSGPACK_NO_BOOST
#include <msgpack.hpp>
#include <cstring>

namespace ve::impl::bin {

// ============================================================================
// Var → MessagePack
// ============================================================================

static void packVar(const Var& v, msgpack::packer<msgpack::sbuffer>& pk)
{
    switch (v.type()) {
        case Var::NONE:
            pk.pack_nil();
            break;
        case Var::BOOL:
            pk.pack(v.toBool());
            break;
        case Var::INT:
            pk.pack(v.toInt64());
            break;
        case Var::DOUBLE:
            pk.pack(v.toDouble());
            break;
        case Var::STRING: {
            const auto& s = v.toString();
            pk.pack_str(static_cast<uint32_t>(s.size()));
            pk.pack_str_body(s.data(), s.size());
            break;
        }
        case Var::BIN: {
            const auto& b = v.toBin();
            pk.pack_bin(static_cast<uint32_t>(b.size()));
            pk.pack_bin_body(reinterpret_cast<const char*>(b.data()), b.size());
            break;
        }
        case Var::LIST: {
            const auto& list = v.toList();
            pk.pack_array(static_cast<uint32_t>(list.size()));
            for (const auto& item : list) {
                packVar(item, pk);
            }
            break;
        }
        case Var::DICT: {
            const auto& dict = v.toDict();
            pk.pack_map(static_cast<uint32_t>(dict.size()));
            for (const auto& kv : dict) {
                pk.pack_str(static_cast<uint32_t>(kv.first.size()));
                pk.pack_str_body(kv.first.data(), kv.first.size());
                packVar(kv.second, pk);
            }
            break;
        }
        case Var::POINTER:
        case Var::CUSTOM:
        default: {
            const auto& s = v.toString();
            pk.pack_str(static_cast<uint32_t>(s.size()));
            pk.pack_str_body(s.data(), s.size());
            break;
        }
    }
}

// ============================================================================
// MessagePack → Var
// ============================================================================

static Var unpackVar(const msgpack::object& obj)
{
    switch (obj.type) {
        case msgpack::type::NIL:
            return Var();
        case msgpack::type::BOOLEAN:
            return Var(obj.as<bool>());
        case msgpack::type::POSITIVE_INTEGER:
            return Var(static_cast<int64_t>(obj.as<uint64_t>()));
        case msgpack::type::NEGATIVE_INTEGER:
            return Var(obj.as<int64_t>());
        case msgpack::type::FLOAT32:
        case msgpack::type::FLOAT64:
            return Var(obj.as<double>());
        case msgpack::type::STR: {
            msgpack::object_str str = obj.via.str;
            return Var(std::string(str.ptr, str.size));
        }
        case msgpack::type::BIN: {
            msgpack::object_bin bin = obj.via.bin;
            Bytes bytes(reinterpret_cast<const uint8_t*>(bin.ptr),
                        reinterpret_cast<const uint8_t*>(bin.ptr) + bin.size);
            return Var(std::move(bytes));
        }
        case msgpack::type::ARRAY: {
            msgpack::object_array arr = obj.via.array;
            Var::ListV list;
            list.reserve(arr.size);
            for (uint32_t i = 0; i < arr.size; ++i) {
                list.push_back(unpackVar(arr.ptr[i]));
            }
            return Var(std::move(list));
        }
        case msgpack::type::MAP: {
            msgpack::object_map map = obj.via.map;
            Var::DictV dict;
            for (uint32_t i = 0; i < map.size; ++i) {
                const msgpack::object_kv& kv = map.ptr[i];
                if (kv.key.type == msgpack::type::STR) {
                    msgpack::object_str key_str = kv.key.via.str;
                    std::string key(key_str.ptr, key_str.size);
                    dict[key] = unpackVar(kv.val);
                }
            }
            return Var(std::move(dict));
        }
        default:
            return Var();
    }
}

// ============================================================================
// Node tree → MessagePack (format: {_v: value, _c: [[name, node], ...]})
// ============================================================================

static bool isIgnoredChild(const Node* child, const schema::ExportOptions& options)
{
    return options.auto_ignore
        && child
        && !child->name().empty()
        && child->name()[0] == '_';
}

static void packNode(const Node* node, msgpack::packer<msgpack::sbuffer>& pk, const schema::ExportOptions& options)
{
    int fieldCount = 0;
    const Var& nodeValue = node->get();
    bool hasValue = !nodeValue.isNull();

    auto all_children = node->children();
    Vector<const Node*> visible_children;
    visible_children.reserve(all_children.sizeAsInt());
    for (auto* child : all_children) {
        if (!isIgnoredChild(child, options)) {
            visible_children.push_back(child);
        }
    }
    bool hasChildren = !visible_children.empty();

    if (hasValue) fieldCount++;
    if (hasChildren) fieldCount++;

    pk.pack_map(fieldCount);

    if (hasValue) {
        pk.pack_str(2);
        pk.pack_str_body("_v", 2);
        packVar(nodeValue, pk);
    }

    if (hasChildren) {
        pk.pack_str(2);
        pk.pack_str_body("_c", 2);
        pk.pack_array(static_cast<uint32_t>(visible_children.size()));
        for (const Node* child : visible_children) {
            pk.pack_array(2);
            const std::string& name = child->name();
            pk.pack_str(static_cast<uint32_t>(name.size()));
            pk.pack_str_body(name.data(), name.size());
            packNode(child, pk, options);
        }
    }
}

// ============================================================================
// MessagePack → Node tree
// ============================================================================

static void unpackNode(const msgpack::object& obj, Node* node)
{
    if (obj.type != msgpack::type::MAP) {
        return;
    }

    msgpack::object_map map = obj.via.map;
    for (uint32_t i = 0; i < map.size; ++i) {
        const msgpack::object_kv& kv = map.ptr[i];
        if (kv.key.type != msgpack::type::STR) {
            continue;
        }

        msgpack::object_str key_str = kv.key.via.str;
        std::string key(key_str.ptr, key_str.size);

        if (key == "_v") {
            Var v = unpackVar(kv.val);
            if (!v.isNull()) {
                node->set(std::move(v));
            }
        }
        else if (key == "_c") {
            if (kv.val.type == msgpack::type::ARRAY) {
                msgpack::object_array children = kv.val.via.array;
                for (uint32_t j = 0; j < children.size; ++j) {
                    const msgpack::object& child_pair = children.ptr[j];
                    if (child_pair.type == msgpack::type::ARRAY && child_pair.via.array.size == 2) {
                        const msgpack::object& name_obj = child_pair.via.array.ptr[0];
                        const msgpack::object& node_obj = child_pair.via.array.ptr[1];

                        if (name_obj.type == msgpack::type::STR) {
                            msgpack::object_str name_str = name_obj.via.str;
                            std::string name(name_str.ptr, name_str.size);
                            Node* child = node->append(name);
                            unpackNode(node_obj, child);
                        }
                    }
                }
            }
        }
    }
}

// ============================================================================
// Public API
// ============================================================================

void writeVar(const Var& v, Bytes& buf)
{
    msgpack::sbuffer sbuf;
    msgpack::packer<msgpack::sbuffer> pk(&sbuf);
    packVar(v, pk);
    buf.insert(buf.end(), sbuf.data(), sbuf.data() + sbuf.size());
}

Var readVar(const uint8_t*& ptr, const uint8_t* end)
{
    if (!ptr || ptr >= end) {
        return Var();
    }

    try {
        size_t offset = 0;
        msgpack::object_handle oh = msgpack::unpack(reinterpret_cast<const char*>(ptr), end - ptr, offset);
        const msgpack::object& obj = oh.get();

        ptr += offset;

        return unpackVar(obj);
    }
    catch (...) {
        ptr = nullptr;
        return Var();
    }
}

Bytes exportTree(const Node* node)
{
    return exportTree(node, schema::ExportOptions{});
}

Bytes exportTree(const Node* node, const schema::ExportOptions& options)
{
    if (!node) {
        return {};
    }

    msgpack::sbuffer sbuf;
    msgpack::packer<msgpack::sbuffer> pk(&sbuf);
    packNode(node, pk, options);

    return Bytes(sbuf.data(), sbuf.data() + sbuf.size());
}

bool importTree(Node* node, const uint8_t* data, size_t len)
{
    return importTree(node, data, len, schema::ImportOptions{});
}

bool importTree(Node* node, const uint8_t* data, size_t len, const schema::ImportOptions& options)
{
    if (!node || !data || len == 0) {
        return false;
    }

    try {
        msgpack::object_handle oh = msgpack::unpack(reinterpret_cast<const char*>(data), len);
        const msgpack::object& obj = oh.get();

        Node parsed("bin_import");
        unpackNode(obj, &parsed);

        node->copy(&parsed, options.auto_insert, options.auto_remove, options.auto_update);
        return true;
    }
    catch (...) {
        return false;
    }
}

} // namespace ve::impl::bin
