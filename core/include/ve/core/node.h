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

class VE_API Node
{
public:
    explicit Node(const std::string& name = "");
    ~Node();

    // --- static ---
    static Node* root();
    static bool  isValidName(const std::string& name);

    // --- identity ---
    const std::string&   name()  const;
    std::recursive_mutex& mutex() const;

    // --- tree navigation ---
    Node* parent() const;
    Node* parent(int level) const;
    Node* sibling(int offset) const;
    Node* prev() const;
    Node* next() const;
    Node* first() const;
    Node* last() const;
    int   indexInParent() const;
    bool  isAncestorOf(const Node* node) const;

    // --- child (by name, no # no /) ---
    Node*         child(const std::string& name) const;
    Node*         child(const std::string& name, int index) const;
    Node*         childAt(int index) const;
    int           childCount() const;
    int           childCount(const std::string& name) const;
    bool          hasChild(const std::string& name) const;
    int           indexOf(const Node* child) const;
    Strings       childNames() const;
    Vector<Node*> children() const;
    Vector<Node*> children(const std::string& name) const;

    // --- child management (by name, no # no /) ---
    bool  insert(const std::string& name, Node* child);
    bool  insert(const std::string& name, int index, Node* child);
    bool  insertAt(int index, Node* child, bool auto_fill = true);
    Node* append(const std::string& name, Node* child);
    Node* append(Node* child);
    bool  remove(Node* child, bool auto_delete = true);
    bool  remove(const std::string& name, int index = 0, bool auto_delete = true);
    void  clearChildren(bool auto_delete = true);

    // --- path (key = name | name#N | #N,  path = key/key/...) ---
    Node*       resolve(const std::string& path) const;
    std::string path(Node* ancestor = nullptr) const;
    Node*       ensure(const std::string& path);
    bool        erase(const std::string& path, bool auto_delete = true);

    // --- shadow (prototype chain) ---
    Node* shadow() const;
    void  setShadow(Node* shadow);

    // --- debug ---
    std::string dump(int depth = 0) const;

private:
    VE_DECLARE_UNIQUE_PRIVATE
};

} // namespace ve
