// node.h — ve::Node
#pragma once

#include "object.h"
#include "schema.h"

namespace ve {

// --- Node ------------------------------------------------------------------
//
// children : Vector<Node*>            flat array, true insertion order
//            Hash<SmallVector<int>>   name → [indices into vector]
// shadow   : Node*                    prototype-chain fallback
//
// name  = real child name (no # no /)
// key   = name | name#N | #N
// path  = key/key/...  (separator /)

class VE_API Node : public Object
{
public:
    using Nodes = Vector<Node*>;

public:
    explicit Node(const std::string& name = "");
    ~Node();

    // --- signals ---
    // Slot parameter count can be ≤ trigger parameter count (Qt-like).
    //   connect<S>(obs, [](string key, int count) { ... })  — full args
    //   connect<S>(obs, [](string key) { ... })             — partial args OK
    //   connect<S>(obs, []() { ... })                       — just notification
    enum NodeSignal : SignalT {
        NODE_CHANGED    = 0xFFFF'0010,  // (Var new_val, Var old_val) — value changed via set()/update()
        NODE_ADDED      = 0xFFFF'0011,  // (string key, int overlap) — key of first added, overlap count (0 = single)
        NODE_REMOVED    = 0xFFFF'0012,  // (string key, int overlap) — key of removed, overlap count; clear: ("#0", count-1)
        NODE_ACTIVATED  = 0xFFFF'001F,  // (SignalT signal, Node* source) — bubbles up the ancestor chain
    };

    // --- value ---
    Var get() const { return value(); }

    Node* set(const Var& v);
    Node* set(Var&& v);

    bool update(const Var& v);

    // --- tree navigation ---
    Node* parent() const;
    Node* parent(int level) const;

    bool isAncestorOf(const Node* descendant_node) const;

    // --- child ---
    Node* child(int index) const;
    Node* child(const std::string& name, int overlap = 0) const;

    int indexOf(const Node* child_node, int guess = -1) const;

    bool has(int index) const { return child(index) != nullptr; }
    bool has(const std::string& name, int overlap = 0) const { return child(name, overlap) != nullptr; }
    bool has(const Node* child_node) const { return indexOf(child_node) >= 0; }

    int count() const;
    int count(const std::string& name) const;

    bool empty() const { return count() == 0; }
    std::size_t size() const { return count(); }

    Nodes children() const;
    Nodes children(const std::string& name) const;

    Strings childNames() const; // unique non-empty names, insertion order

    // --- child management (by name, no # no /) ---
    // index: 0 = prepend, -1 = append, negative wraps via (index + count + 1)
    bool  insert(Node* child, int index = -1);
    bool  insert(const Nodes& children, int index = -1);

    Node* append(const std::string& name, int overlap = 0);
    Node* append(int overlap = 0) { return append("", overlap); }

    Node* take(Node* child);
    Node* take(int index) { return take(child(index)); }
    Node* take(const std::string& name, int overlap = 0) { return take(child(name, overlap)); }

    bool  remove(Node* child);
    bool  remove(int index) { return remove(child(index)); }
    bool  remove(const std::string& name, int overlap) { return remove(child(name, overlap)); }
    bool  remove(const std::string& name);

    void  clear(bool auto_delete = true);
    // Sync this node from another node using key/path semantics.
    // - Named-sibling insertion order is not part of copy semantics; same-name overlap
    //   order and anonymous occurrence (#N) are matched when reusing children.
    // - For Dict attributes in Var (XML/JSON attrs), CRUD only emits NODE_CHANGED
    //   (no NODE_ADDED/NODE_REMOVED to avoid polluting parent signals).
    // - Default value sync uses set() (always emits NODE_CHANGED); auto_update=true uses update().
    void  copy(const Node* other, bool auto_insert = true, bool auto_remove = false, bool auto_update = false);

    // -- key (key = name | name#N | #N) ---
    //  parseKey: "name" → (name,-1)  "name#N" → (name,N)  "#N" → ("",N)
    //  toKey:    inverse of parseKey
    static bool parseKey(std::string_view key, std::string_view& name, int& index);
    static std::string toKey(std::string_view name, int index);
    static bool isKey(const std::string& key);
    static int  keyIndex(const std::string& key);

    std::string keyOf(const Node* child, int guess = -1) const;

    const Node* shadow() const;
    void  setShadow(Node* shadow);

    // Single-level key access: "name" | "name#N" | "#N"
    Node* atKey(int index, bool use_shadow) const;
    Node* atKey(const std::string& name, int overlap, bool use_shadow) const;
    Node* atKey(std::string_view key, bool use_shadow) const;

    Node* atKey(int index, bool use_shadow);
    Node* atKey(const std::string& name, int overlap, bool use_shadow);
    Node* atKey(std::string_view key, bool use_shadow);        // ensure exists, creates if not found


    // --- path (path = key/key/...) ---
    std::string path(Node* ancestor = nullptr) const;

    // Multi-level path access: "a/b/c"
    Node*       atPath(std::string_view path, bool use_shadow) const;  // find only
    Node*       atPath(std::string_view path, bool use_shadow);        // ensure exists

    bool        erase(const std::string& path, bool auto_delete = true);

    // --- container interface ---
    Node* operator[](int index) const { return child(index); }
    Node* operator[](const std::string& key) const { return atKey(key, false); }

    class ChildIterator {
        Node* const* _p = nullptr;
        friend class Node;
        ChildIterator(Node* const* p) : _p(p) {}
    public:
        ChildIterator() = default;
        Node*          operator*() const { return *_p; }
        ChildIterator& operator++() { ++_p; return *this; }
        ChildIterator  operator++(int) { auto t = *this; ++_p; return t; }
        bool operator==(const ChildIterator& o) const { return _p == o._p; }
        bool operator!=(const ChildIterator& o) const { return _p != o._p; }
    };

    class ReverseChildIterator {
        Node* const* _p = nullptr;
        friend class Node;
        ReverseChildIterator(Node* const* p) : _p(p) {}
    public:
        ReverseChildIterator() = default;
        Node*                 operator*() const { return *(_p - 1); }
        ReverseChildIterator& operator++() { --_p; return *this; }
        ReverseChildIterator  operator++(int) { auto t = *this; --_p; return t; }
        bool operator==(const ReverseChildIterator& o) const { return _p == o._p; }
        bool operator!=(const ReverseChildIterator& o) const { return _p != o._p; }
    };

    ChildIterator        begin()  const;
    ChildIterator        end()    const;
    ReverseChildIterator rbegin() const;
    ReverseChildIterator rend()   const;

    // --- flags (reuses Object::_flags, higher bits) ---
    enum NodeFlag : int {
        WATCHING = 0x02,  // participate in signal bubbling (receive NODE_ACTIVATED from descendants)
    };

    bool isWatching() const { return flags::get(_flags, WATCHING); }
    void watch(bool on) { flags::set(_flags, WATCHING, on); }

    void watchAll(bool on);
    void silentAll(bool on);

    // --- activate (signal bubbling) ---
    // Emits NODE_ACTIVATED on this node, then bubbles to parent while parent->isWatching().
    // Insert/remove/clear/set call activate only when this node isWatching()
    // Value change bubbling to ancestors therefore requires watch on the node that changed unless
    // you call activate() explicitly.
    // SILENT stops emission + bubbling at that node.
    void activate(SignalT signal, Node* source = nullptr);

    // --- debug ---
    std::string dump(int depth = 0) const;

public:
    // get usage
    template<typename T> T getAs(const T& def = T{}) const { return value().to<T>(def); }

    bool        getBool(bool def = false) const { return value().toBool(def); }
    int         getInt(int def = -1) const { return value().toInt(def); }
    int64_t     getInt64(int64_t def = -1) const { return value().toInt64(def); }
    double      getDouble(double def = 0.0) const { return value().toDouble(def); }
    std::string getString(const std::string& def = "") const { return value().toString(def); }

    // sibling usage
    Node* sibling(int offset) const { auto* p = parent(); if (!p) return nullptr; int i = p->indexOf(this) + offset; return (i >= 0) ? p->child(i) : nullptr; }

    Node* first() const { return child(0); }
    Node* last() const { return child(-1); }

    Node* prev() const { return sibling(-1); }
    Node* next() const { return sibling(1); }

    // path usage
    Node* find(std::string_view path, bool use_shadow = true) const { return atPath(path, use_shadow); }

    Node* at(int index, bool use_shadow = true) { return atKey(index, use_shadow); }
    Node* at(const std::string& name, int overlap, bool use_shadow = true) { return atKey(name, overlap, use_shadow); }
    Node* at(const std::string& path, bool use_shadow = true) { return atPath(path, use_shadow); }

    // subpath usage
    Var get(int index) const { if (auto n = child(index)) return n->value(); return Var {}; }
    Var get(const std::string& name, int overlap) const { if (auto n = child(name, overlap)) return n->value(); return Var {}; }
    Var get(const std::string& path) const { if (auto n = find(path)) return n->value(); return Var {}; }

    template<typename T> Node* set(int index, T&& t) { at(index)->set(std::forward<T>(t)); return this; }
    template<typename T> Node* set(const std::string& name, int overlap, T&& t) { at(name, overlap)->set(std::forward<T>(t)); return this; }
    template<typename T> Node* set(const std::string& path, T&& t) { at(path)->set(std::forward<T>(t)); return this; }

    template<typename T> bool update(int index, T&& t)
    {
        if (get(index) == t) return false;
        set(index, std::forward<T>(t));
        return true;
    }
    template<typename T> bool update(const std::string& name, int overlap, T&& t)
    {
        if (get(name, overlap) == t) return false;
        set(name, overlap, std::forward<T>(t));
        return true;
    }
    template<typename T> bool update(const std::string& path, T&& t)
    {
        if (get(path) == t) return false;
        set(path, std::forward<T>(t));
        return true;
    }

    // container usage
    Var::ListV toList() const
    {
        Var::ListV v;
        v.reserve(count());
        for (auto* c : *this) {
            v.push_back(c->get());
        }
        return v;
    }

    template<typename ListT> void toList(ListT& l) const
    {
        Var::ListV v = toList();
        l.reserve(v.size());
        for (const auto& item : v) {
            l.push_back(item.to<typename ListT::value_type>());
        }
    }

    Ints toInts() const { Ints is; toList(is); return is; }
    Doubles toDoubles() const { Doubles ds; toList(ds); return ds; }
    Strings toStrings() const { Strings ss; toList(ss); return ss; }
    Values toValues() const { Values vs; toList(vs); return vs; }

    Var::DictV toDict() const
    {
        Var::DictV v;
        for (auto* c : *this) {
            v[c->name()] = c->get();
        }
        return v;
    }

    template<typename DictT> void toDict(DictT& d)
    {
        Var::DictV v = toDict();
        d.clear();
        for (const auto& kv : v) {
            d[kv.first] = kv.second.to<typename DictT::mapped_type>();
        }
    }

    template<typename ListT> Node* fromList(const ListT& l)
    {
        for (std::size_t i = 0; i < l.size(); i++) {
            set(l.size() - i - 1, l[i]);
        }
        return set(get());
    }
    template<typename ListT> Node* fromList(const std::string& path, const ListT& l) { return at(path)->fromList(l); }

    template<typename DictT> Node* fromDict(const DictT& d)
    {
        for (const auto& kv : d) {
            set(kv.first, kv.second);
        }
        return set(get());
    }
    template<typename DictT> Node* fromDict(const std::string& path, const DictT& d) { return at(path)->fromDict(d); }

    // signal usage
    template<typename... Args> void onChanged(Args&&... args) { connect<NODE_CHANGED>(std::forward<Args>(args)...); }

    template<typename... Args> void then(Args&&... args) { once<NODE_CHANGED>(std::forward<Args>(args)...); }

protected:
    [[nodiscard]] const Var& value() const;

private:
    VE_DECLARE_UNIQUE_PRIVATE
};

namespace node {

VE_API Node* root(); // --- global data tree accessor ---

using KeyPair = std::pair<std::string, int>;

inline std::string key(int index) { return Node::toKey("", index); }
inline std::string key(const std::string& name, int overlap = 0) { return Node::toKey(name, overlap); }
inline std::string key(const KeyPair& k) { return Node::toKey(k.first, k.second); }

template<typename T> std::string path(const T& t) { return key(t); }
template<typename T1, typename T2, typename... Ts> std::string path(const T1& t1, const T2& t2, const Ts&... ts) { return key(t1) + "/" + path(t2, ts...); }

}

VE_API Node* n(const std::string& path);

} // namespace ve
