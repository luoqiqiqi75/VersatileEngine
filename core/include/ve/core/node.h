// node.h — ve::Node + ve::Schema
#pragma once

#include "base.h"

namespace ve {

class Node;

// --- Schema ----------------------------------------------------------------

struct SchemaField
{
    std::string name;
    std::shared_ptr<struct Schema> sub;

    SchemaField(const std::string& n) : name(n) {}
    SchemaField(std::string n, std::shared_ptr<struct Schema> s)
        : name(std::move(n)), sub(std::move(s)) {}
};

struct VE_API Schema
{
    Vector<SchemaField> fields;

    Schema() = default;
    Schema(std::initializer_list<SchemaField> f) : fields(f.begin(), f.end()) {}

    static std::shared_ptr<Schema> create(std::initializer_list<SchemaField> f)
    { return std::make_shared<Schema>(f); }

    int fieldCount() const { return (int)fields.size(); }
    void build(Node* node) const;
};

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
    explicit Node(const std::string& name = "");
    ~Node();

    // --- signals ---
    enum NodeSignal : SignalT {
        NODE_CHILD_ADDED   = 0x0010,
        NODE_CHILD_REMOVED = 0x0011,
    };

    // --- static ---
    static Node* root();

    // --- tree navigation ---
    Node* parent() const;
    Node* parent(int level) const;

    bool isAncestorOf(const Node* descendant_node) const;

    // --- child ---
    Node* child(int index) const;
    Node* child(const std::string& name, int overlap = 0) const;

    int indexOf(const Node* child_node) const;

    bool has(int index) const { return child(index) != nullptr; }
    bool has(const std::string& name, int overlap = 0) const { return child(name, overlap) != nullptr; }
    bool has(const Node* child_node) const { return indexOf(child_node) >= 0; }

    int count() const;
    int count(const std::string& name) const;

    bool empty() const { return count() == 0; }
    std::size_t size() const { return count(); }

    Vector<Node*> children() const;
    Vector<Node*> children(const std::string& name) const;

    Strings childNames() const; // unique non-empty names, insertion order

    Node* sibling(int offset) const { auto* p = parent(); return p ? p->child(p->indexOf(this) + offset) : nullptr; }

    Node* first() const { return child(0); }
    Node* last() const { return child(-1); }

    Node* prev() const { return sibling(-1); }
    Node* next() const { return sibling(1); }

    // --- child management (by name, no # no /) ---
    bool  insert(Node* child);
    bool  insert(Node* child, int index);

    Node* append(const std::string& name, int overlap = 0);
    Node* append(int index = 0) { return append("", index); }

    Node* take(Node* child);
    Node* take(int index) { return take(child(index)); }
    Node* take(const std::string& name, int overlap = 0) { return take(child(name, overlap)); }

    bool  remove(Node* child);
    bool  remove(int index) { return remove(child(index)); }
    bool  remove(const std::string& name, int overlap) { return remove(child(name, overlap)); }
    bool  remove(const std::string& name);

    void  clear(bool auto_delete = true);

    // -- key (key = name | name#N | #N) ---
    static bool isKey(const std::string& key);
    static int keyIndex(const std::string& key);

    std::string keyOf(const Node* child) const;

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

    // --- shadow ---
    Node* shadow() const;
    void  setShadow(Node* shadow);

    // --- path (path = key/key/...) ---
    Node*       resolve(const std::string& path) const;
    std::string path(Node* ancestor = nullptr) const;
    Node*       ensure(const std::string& path);
    bool        erase(const std::string& path, bool auto_delete = true);

    // --- debug ---
    std::string dump(int depth = 0) const;

private:
    VE_DECLARE_UNIQUE_PRIVATE
};

} // namespace ve
