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
// children : Dict<SmallVector<Node*, 1>>  name → [Node*], insertion order
// shadow   : Node*                        prototype-chain fallback
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
    static bool  isValidName(const std::string& name);

    // --- tree navigation ---
    Node* parent() const;
    Node* parent(int level) const;

    bool  isAncestorOf(const Node* node) const;

    // --- child ---
    Node*         child(const std::string& name, int index = 0) const;
    Node*         child(int global_index) const;

    bool          has(const std::string& name, int index = 0) const;
    bool          has(int global_index) const;
    bool          has(const Node* child) const;

    int           count() const;
    int           count(const std::string& name) const;

    Vector<Node*> children() const;
    Vector<Node*> children(const std::string& name) const;

    Strings       childNames() const; // skip empty

    // --- relation ---
    Node*         first() const;
    Node*         last() const;

    template<bool IsGlobal = true> int indexOf(const Node* child) const;

    template<bool IsGlobal = true> Node* sibling(int offset) const;

    template<bool IsGlobal = true> VE_FORCE_INLINE Node* prev() const { return sibling<IsGlobal>(-1); }
    template<bool IsGlobal = true> VE_FORCE_INLINE Node* next() const { return sibling<IsGlobal>(1); }

    // --- child management (by name, no # no /) ---
    bool  insert(Node* child);
    bool  insert(Node* child, int index, bool auto_fill = true);
    Node* append(const std::string& name = "");
    Node* append(const std::string& name, int index, bool auto_fill = true);
    Node* append(int index, bool auto_fill = true);

    Node* take(Node* child);
    Node* take(const std::string& name, int index = 0);
    bool  remove(Node* child);
    bool  remove(const std::string& name, int index);
    bool  remove(const std::string& name);

    void  clear(bool auto_delete = true);

    // -- key (key = name | name#N | #N) ---
    std::string keyOf(const Node* child) const;

    Node*       childAt(const std::string& key) const;

    // --- container interface ---
    VE_FORCE_INLINE Node* operator[](int global_index) const { return child(global_index); }
    VE_FORCE_INLINE Node* operator[](const std::string& key) const { return childAt(key); }

    class VE_API ChildIterator {
        const void* _e = nullptr;
        uint32_t _i = 0;
        friend class Node;
        ChildIterator(const void* e, uint32_t i) : _e(e), _i(i) {}
    public:
        ChildIterator() = default;
        Node*          operator*() const;
        ChildIterator& operator++();
        ChildIterator  operator++(int) { auto t = *this; ++(*this); return t; }
        bool operator==(const ChildIterator& o) const { return _e == o._e && _i == o._i; }
        bool operator!=(const ChildIterator& o) const { return !(*this == o); }
    };

    class VE_API ReverseChildIterator {
        const void* _e = nullptr;
        uint32_t _i = 0;
        friend class Node;
        ReverseChildIterator(const void* e, uint32_t i) : _e(e), _i(i) {}
    public:
        ReverseChildIterator() = default;
        Node*                 operator*() const;
        ReverseChildIterator& operator++();
        ReverseChildIterator  operator++(int) { auto t = *this; ++(*this); return t; }
        bool operator==(const ReverseChildIterator& o) const { return _e == o._e && _i == o._i; }
        bool operator!=(const ReverseChildIterator& o) const { return !(*this == o); }
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
