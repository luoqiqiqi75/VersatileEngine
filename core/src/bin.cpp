// ----------------------------------------------------------------------------
// bin.cpp — Binary serialization for Var and Node (hand-rolled, little-endian)
// ----------------------------------------------------------------------------
#include "ve/core/impl/bin.h"
#include "ve/core/node.h"

#include <cstring>

namespace ve::bin {

// ============================================================================
// Little-endian read/write primitives
// ============================================================================

static inline void writeU8(Bytes& buf, uint8_t v)
{
    buf.push_back(v);
}

static inline void writeU16(Bytes& buf, uint16_t v)
{
    buf.push_back(static_cast<uint8_t>(v));
    buf.push_back(static_cast<uint8_t>(v >> 8));
}

static inline void writeU32(Bytes& buf, uint32_t v)
{
    buf.push_back(static_cast<uint8_t>(v));
    buf.push_back(static_cast<uint8_t>(v >> 8));
    buf.push_back(static_cast<uint8_t>(v >> 16));
    buf.push_back(static_cast<uint8_t>(v >> 24));
}

static inline void writeI64(Bytes& buf, int64_t v)
{
    uint64_t u;
    std::memcpy(&u, &v, 8);
    for (int i = 0; i < 8; ++i)
        buf.push_back(static_cast<uint8_t>(u >> (i * 8)));
}

static inline void writeF64(Bytes& buf, double v)
{
    uint64_t u;
    std::memcpy(&u, &v, 8);
    for (int i = 0; i < 8; ++i)
        buf.push_back(static_cast<uint8_t>(u >> (i * 8)));
}

static inline void writeStr(Bytes& buf, const std::string& s)
{
    writeU32(buf, static_cast<uint32_t>(s.size()));
    buf.insert(buf.end(), s.begin(), s.end());
}

static inline void writeBin(Bytes& buf, const Bytes& b)
{
    writeU32(buf, static_cast<uint32_t>(b.size()));
    buf.insert(buf.end(), b.begin(), b.end());
}

// --- read ---

static inline bool canRead(const uint8_t* ptr, const uint8_t* end, size_t n)
{
    return (end - ptr) >= static_cast<ptrdiff_t>(n);
}

static inline uint8_t readU8(const uint8_t*& ptr, const uint8_t* end)
{
    if (!canRead(ptr, end, 1)) return 0;
    return *ptr++;
}

static inline uint16_t readU16(const uint8_t*& ptr, const uint8_t* end)
{
    if (!canRead(ptr, end, 2)) return 0;
    uint16_t v = static_cast<uint16_t>(ptr[0]) | (static_cast<uint16_t>(ptr[1]) << 8);
    ptr += 2;
    return v;
}

static inline uint32_t readU32(const uint8_t*& ptr, const uint8_t* end)
{
    if (!canRead(ptr, end, 4)) return 0;
    uint32_t v = static_cast<uint32_t>(ptr[0])
               | (static_cast<uint32_t>(ptr[1]) << 8)
               | (static_cast<uint32_t>(ptr[2]) << 16)
               | (static_cast<uint32_t>(ptr[3]) << 24);
    ptr += 4;
    return v;
}

static inline int64_t readI64(const uint8_t*& ptr, const uint8_t* end)
{
    if (!canRead(ptr, end, 8)) return 0;
    uint64_t u = 0;
    for (int i = 0; i < 8; ++i)
        u |= static_cast<uint64_t>(ptr[i]) << (i * 8);
    ptr += 8;
    int64_t v;
    std::memcpy(&v, &u, 8);
    return v;
}

static inline double readF64(const uint8_t*& ptr, const uint8_t* end)
{
    if (!canRead(ptr, end, 8)) return 0.0;
    uint64_t u = 0;
    for (int i = 0; i < 8; ++i)
        u |= static_cast<uint64_t>(ptr[i]) << (i * 8);
    ptr += 8;
    double v;
    std::memcpy(&v, &u, 8);
    return v;
}

static inline std::string readStr(const uint8_t*& ptr, const uint8_t* end)
{
    uint32_t len = readU32(ptr, end);
    if (!canRead(ptr, end, len)) { ptr = end; return ""; }
    std::string s(reinterpret_cast<const char*>(ptr), len);
    ptr += len;
    return s;
}

static inline Bytes readBytesRaw(const uint8_t*& ptr, const uint8_t* end)
{
    uint32_t len = readU32(ptr, end);
    if (!canRead(ptr, end, len)) { ptr = end; return {}; }
    Bytes b(ptr, ptr + len);
    ptr += len;
    return b;
}

// ============================================================================
// Var serialization
// ============================================================================

// Type tags aligned with Var::Type enum values
static constexpr uint8_t TAG_NULL    = 0;
static constexpr uint8_t TAG_BOOL    = 1;
static constexpr uint8_t TAG_INT     = 2;
static constexpr uint8_t TAG_DOUBLE  = 3;
static constexpr uint8_t TAG_STRING  = 4;
static constexpr uint8_t TAG_BIN     = 5;
static constexpr uint8_t TAG_LIST    = 6;
static constexpr uint8_t TAG_DICT    = 7;

void writeVar(const Var& v, Bytes& buf)
{
    switch (v.type()) {
        case Var::NONE:
            writeU8(buf, TAG_NULL);
            break;
        case Var::BOOL:
            writeU8(buf, TAG_BOOL);
            writeU8(buf, v.toBool() ? 1 : 0);
            break;
        case Var::INT:
            writeU8(buf, TAG_INT);
            writeI64(buf, v.toInt64());
            break;
        case Var::DOUBLE:
            writeU8(buf, TAG_DOUBLE);
            writeF64(buf, v.toDouble());
            break;
        case Var::STRING:
            writeU8(buf, TAG_STRING);
            writeStr(buf, v.toString());
            break;
        case Var::BIN:
            writeU8(buf, TAG_BIN);
            writeBin(buf, v.toBin());
            break;
        case Var::LIST: {
            writeU8(buf, TAG_LIST);
            auto& list = v.toList();
            writeU32(buf, static_cast<uint32_t>(list.size()));
            for (auto& item : list)
                writeVar(item, buf);
            break;
        }
        case Var::DICT: {
            writeU8(buf, TAG_DICT);
            auto& dict = v.toDict();
            writeU32(buf, static_cast<uint32_t>(dict.size()));
            for (auto& kv : dict) {
                writeStr(buf, kv.key);
                writeVar(kv.value, buf);
            }
            break;
        }
        default:
            writeU8(buf, TAG_STRING);
            writeStr(buf, v.toString());
            break;
    }
}

Var readVar(const uint8_t*& ptr, const uint8_t* end)
{
    if (ptr >= end) return Var();
    uint8_t tag = readU8(ptr, end);

    switch (tag) {
        case TAG_NULL:
            return Var();
        case TAG_BOOL:
            return Var(readU8(ptr, end) != 0);
        case TAG_INT:
            return Var(readI64(ptr, end));
        case TAG_DOUBLE:
            return Var(readF64(ptr, end));
        case TAG_STRING:
            return Var(readStr(ptr, end));
        case TAG_BIN:
            return Var(readBytesRaw(ptr, end));
        case TAG_LIST: {
            uint32_t count = readU32(ptr, end);
            Var::ListV list;
            list.reserve(count);
            for (uint32_t i = 0; i < count; ++i)
                list.push_back(readVar(ptr, end));
            return Var(std::move(list));
        }
        case TAG_DICT: {
            uint32_t count = readU32(ptr, end);
            Var::DictV dict;
            for (uint32_t i = 0; i < count; ++i) {
                std::string key = readStr(ptr, end);
                dict[key] = readVar(ptr, end);
            }
            return Var(std::move(dict));
        }
        default:
            return Var();
    }
}

// ============================================================================
// Node tree serialization
// ============================================================================

static void writeNode(const Node* node, Bytes& buf)
{
    // value
    if (node->hasValue() && !node->value().isNull())
        writeVar(node->value(), buf);
    else
        writeU8(buf, TAG_NULL);

    // children
    writeU32(buf, static_cast<uint32_t>(node->count()));
    for (auto* c : *node) {
        writeU16(buf, static_cast<uint16_t>(c->name().size()));
        buf.insert(buf.end(), c->name().begin(), c->name().end());
        writeNode(c, buf);
    }
}

static void readNode(const uint8_t*& ptr, const uint8_t* end, Node* node)
{
    // value
    Var v = readVar(ptr, end);
    if (!v.isNull())
        node->set(std::move(v));

    // children
    uint32_t count = readU32(ptr, end);
    for (uint32_t i = 0; i < count; ++i) {
        uint16_t nameLen = readU16(ptr, end);
        std::string name;
        if (canRead(ptr, end, nameLen)) {
            name.assign(reinterpret_cast<const char*>(ptr), nameLen);
            ptr += nameLen;
        }
        auto* c = node->append(name, name.empty() ? 0 : node->count(name));
        readNode(ptr, end, c);
    }
}

Bytes exportTree(const Node* node)
{
    if (!node) return {};
    Bytes buf;
    buf.reserve(1024);
    writeNode(node, buf);
    return buf;
}

bool importTree(Node* node, const uint8_t* data, size_t len)
{
    if (!node || !data || len == 0) return false;
    const uint8_t* ptr = data;
    const uint8_t* end = data + len;
    readNode(ptr, end, node);
    return ptr <= end;
}

} // namespace ve::bin
