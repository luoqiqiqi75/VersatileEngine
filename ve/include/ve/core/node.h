// node.h — ve::Node
#pragma once

#include "factory.h"
#include "schema.h"
#include <string_view>

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
    const Var& get() const { return value(); }
    const Var& get(const std::string& path, const Var& def = Var()) const { if (auto n = find(path)) return n->value(); return def; }

    template<typename T> T getAs(const T& def = T{}) const { return value().to<T>(def); }

    bool        getBool(bool def = false) const { return value().toBool(def); }
    int         getInt(int def = -1) const { return value().toInt(def); }
    int64_t     getInt64(int64_t def = -1) const { return value().toInt64(def); }
    double      getDouble(double def = 0.0) const { return value().toDouble(def); }
    std::string getString(const std::string& def = "") const { return value().toString(def); }

    Node* set(const Var& v);
    Node* set(Var&& v);

    template<typename T> Node* set(const std::string& path, T&& t) { at(path)->set(std::forward<T>(t)); return this; }
    template<typename T> Node* set(int index, T&& t) { at(index)->set(std::forward<T>(t)); return this; }
    template<typename T> Node* set(const std::string& name, int overlap, T&& t) { at(name, overlap)->set(std::forward<T>(t)); return this; }

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

    Node* sibling(int offset) const { auto* p = parent(); if (!p) return nullptr; int i = p->indexOf(this) + offset; return (i >= 0) ? p->child(i) : nullptr; }

    Node* first() const { return child(0); }
    Node* last() const { return child(-1); }

    Node* prev() const { return sibling(-1); }
    Node* next() const { return sibling(1); }

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
    // Sync this node from another node. Existing children are reused when possible.
    // Default value sync uses set() so copy always emits NODE_CHANGED.
    // auto_update=true switches to update() to suppress unchanged value signals.
    void  copy(const Node* other, bool auto_insert = true, bool auto_remove = false, bool auto_update = false);

    // -- key (key = name | name#N | #N) ---
    //  parseKey: "name" → (name,-1)  "name#N" → (name,N)  "#N" → ("",N)
    //  toKey:    inverse of parseKey
    static bool parseKey(std::string_view key, std::string_view& name, int& index);
    static std::string toKey(std::string_view name, int index);
    static bool isKey(const std::string& key);
    static int  keyIndex(const std::string& key);

    std::string keyOf(const Node* child, int guess = -1) const;

    Node* childAt(const std::string& key) const;

    // --- container interface ---
    Node* operator[](int index) const { return child(index); }
    Node* operator[](const std::string& key) const { return childAt(key); }

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

    // --- path (path = key/key/...) ---
    Node* shadow() const;
    void  setShadow(Node* shadow);

    Node*       find(const std::string& path, bool use_shadow = true) const;
    std::string path(Node* ancestor = nullptr) const;
    Node*       at(const std::string& path);
    Node*       at(int index);
    Node*       at(const std::string& name, int overlap);
    bool        erase(const std::string& path, bool auto_delete = true);

    // --- flags (reuses Object::_flags, higher bits) ---
    enum NodeFlag : int {
        WATCHING = 0x02,  // participate in signal bubbling (receive NODE_ACTIVATED from descendants)
    };

    bool isWatching() const { return flags::get(_flags, WATCHING); }
    void watch(bool on) { flags::set(_flags, WATCHING, on); }

    void watchAll(bool on);
    void silentAll(bool on);

    // --- activate (signal bubbling) ---
    // Triggers NODE_ACTIVATED on this node, then bubbles up to each watching ancestor.
    // SILENT on any node in the chain stops emission + bubbling at that node.
    // WATCHING on parent controls whether the bubble continues upward.
    void activate(SignalT signal, Node* source = nullptr);

    // --- debug ---
    std::string dump(int depth = 0) const;

protected:
    const Var& value() const;

private:
    VE_DECLARE_POOL_PRIVATE
};

namespace node {

VE_API Node* root(); // --- global data tree accessor ---

}

// ve::n("robot/arm/joint1") — slash-separated path accessor.
// Delegates to ve::node::root()->at(path).
VE_API Node* n(const std::string& path);

} // namespace ve
