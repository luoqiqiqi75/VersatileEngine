// node.cpp — ve::Node + ve::Schema
#include "ve/core/node.h"
#include <string_view>

#include "ve/core/log.h"

namespace ve {

// ============================================================================
// Schema
// ============================================================================

void Schema::build(Node* node) const
{
    if (!node) return;
    for (auto& f : fields) {
        if (f.sub) f.sub->build(node->append(f.name));
    }
}

// ============================================================================
// Node::Private
// ============================================================================

struct Node::Private
{
    Node* parent  = nullptr;
    Node* shadow  = nullptr;

    struct Children {
        Hash<SmallVector<int>> indices;   // name → [global indices in nodes]
        Vector<Node*>          nodes;     // all children, true insertion order

        // shift every recorded index that is >= 'from' by 'delta'
        void shift(int from, int delta)
        {
            for (auto& [_, ivec] : indices)
                for (auto& idx : ivec)
                    if (idx >= from) idx += delta;
        }
    };

    Children* children = nullptr;

    Children* ensure() { return children ? children : (children = new Children()); }
};

// ============================================================================
// Node — construction / static
// ============================================================================

Node::Node(const std::string& name) : Object(name), _p(std::make_unique<Private>()) {}
Node::~Node() { clear(); }

Node* Node::root() { static Node r; return &r; }

// ============================================================================
// Node — tree navigation
// ============================================================================

Node* Node::parent() const { return _p->parent; }
Node* Node::parent(const int level) const
{
    auto* n = _p->parent;
    for (int i = 0; i < level && n; ++i, n = n->_p->parent) {}
    return n;
}

bool Node::isAncestorOf(const Node* descendant_node) const
{
    if (!descendant_node) return false;
    for (auto* p = descendant_node->_p->parent; p; p = p->_p->parent)
        if (p == this) return true;
    return false;
}

// ============================================================================
// Node — child access
// ============================================================================

Node* Node::child(int index) const
{
    if (!_p->children) return nullptr;
    LockT lk(mutex());
    if (index < 0) index += _p->children->nodes.sizeAsInt();
    if (index >= _p->children->nodes.sizeAsInt()) return nullptr;
    return _p->children->nodes.at(index);
}

Node* Node::child(const std::string& name, int overlap) const
{
    if (!_p->children) return nullptr;
    if (name.empty()) return child(overlap);
    LockT lk(mutex());
    if (const auto* iv = _p->children->indices.ptr(name); iv) {
        if (overlap >= 0 && static_cast<std::size_t>(overlap) < iv->size())
            return _p->children->nodes.value((*iv)[static_cast<std::size_t>(overlap)], nullptr);
    }
    return nullptr;
}

int Node::indexOf(const Node* child_node) const
{
    if (!child_node || !_p->children) return -1;
    LockT lk(mutex());
    if (child_node->name().empty()) { // global search, slow
        for (int i = 0; i < _p->children->nodes.sizeAsInt(); ++i) {
            if (_p->children->nodes.at(i) == child_node) return i;
        }
    } else { // search indicies
        if (const auto* iv = _p->children->indices.ptr(child_node->name()); iv) {
            for (const int i : *iv)
                if (_p->children->nodes.value(i, nullptr) == child_node) return i;
        }
    }
    return -1;
}

int Node::count() const
{
    return _p->children ? _p->children->nodes.sizeAsInt() : 0;
}

int Node::count(const std::string& name) const
{
    if (name.empty()) return count();
    if (!_p->children) return 0;
    if (const auto* iv = _p->children->indices.ptr(name)) return iv->sizeAsInt();
    return 0;
}

Vector<Node*> Node::children() const
{
    if (!_p->children) return {};
    LockT lk(mutex());
    return _p->children->nodes;
}

Vector<Node*> Node::children(const std::string& name) const
{
    if (name.empty()) return children();
    if (!_p->children) return {};
    LockT lk(mutex());
    Vector<Node*> out;
    if (const auto* iv = _p->children->indices.ptr(name); iv) {
        for (const int i : *iv)
            out.push_back(_p->children->nodes.value(i, nullptr));
    }
    return out;
}

Strings Node::childNames() const
{
    if (!_p->children) return {};
    LockT lk(mutex());
    Strings out;
    Hash<char> seen;
    for (const auto* n : _p->children->nodes) {
        auto& nm = n->name();
        if (!nm.empty() && !seen.count(nm)) {
            seen[nm] = 0;
            out.push_back(nm);
        }
    }
    return out;
}

// ============================================================================
// Node — child management
// ============================================================================

bool Node::insert(Node* child)
{
    if (!child) {
        veLogE << "<ve.node> insert null child to " << path();
        return false;
    }
    if (child->parent()) {
        veLogW << "<ve.node> insert child with parent " << child->path() << " to " << path();
        child->parent()->take(child);
    }
    LockT lk(mutex());
    auto* ch = _p->ensure();
    if (!child->name().empty()) ch->indices[child->name()].push_back(ch->nodes.sizeAsInt());
    ch->nodes.push_back(child);
    child->_p->parent = this;
    trigger(NODE_CHILD_ADDED);
    return true;
}

bool Node::insert(Node* child, int index)
{
    if (!child || index < 0 || index >= count()) {
        veLogE << "<ve.node> insert null child (index " << index << ") to " << path();
        return false;
    }
    if (child->parent()) {
        veLogW << "<ve.node> insert child with parent " << child->path() << " to " << path();
        child->parent()->take(child);
    }
    LockT lk(mutex());
    auto* ch = _p->ensure();
    ch->shift(index, +1);
    ch->nodes.insert(ch->nodes.begin() + index, child);
    if (!child->name().empty()) ch->indices[child->name()].append(index);
    child->_p->parent = this;
    trigger(NODE_CHILD_ADDED);
    return true;
}

Node* Node::append(const std::string& name, int overlap)
{
    if (overlap < 0) return nullptr;
    Node* cn = new Node(name);
    insert(cn);
    for (int i = 0; i < overlap; ++i) {
        cn = new Node(name);
        insert(cn);
    }
    return cn;
}

Node* Node::take(Node* child)
{
    if (!child || !_p->children) return nullptr;
    LockT lk(mutex());

    int pos = indexOf(child);   // recursive mutex — re-locks OK
    if (pos < 0) return nullptr;

    // remove pos from indices[name]
    auto& nm = child->name();
    auto it = _p->children->indices.find(nm);
    if (it != _p->children->indices.end()) {
        auto& ivec = it->second;
        for (uint32_t j = 0; j < ivec.size(); ++j) {
            if (ivec[j] == pos) { ivec.erase(j); break; }
        }
        if (ivec.empty()) _p->children->indices.erase(it);
    }

    // remove from flat vector
    _p->children->nodes.erase(_p->children->nodes.begin() + pos);

    // shift remaining indices > pos by -1
    _p->children->shift(pos, -1);

    child->_p->parent = nullptr;
    trigger(NODE_CHILD_REMOVED);
    return child;
}

bool Node::remove(Node* child)
{
    if (auto* n = take(child)) { delete n; return true; }
    return false;
}

bool Node::remove(const std::string& name)
{
    bool removed = false;
    if (name.empty()) {
        remove(last());
    } else {
        while (auto* n = take(name, -1)) {
            delete n;
            removed = true;
        }
    }
    return removed;
}

void Node::clear(bool auto_delete)
{
    if (!_p->children) return;
    LockT lk(mutex());
    for (auto* c : _p->children->nodes)
        if (c) { c->_p->parent = nullptr; if (auto_delete) delete c; }
    delete _p->children;
    _p->children = nullptr;
    trigger(NODE_CHILD_REMOVED);
}

// ============================================================================
// Node — key helpers
// ============================================================================

// parse key: "name#N" → (name,N), "#N" → global, "name" → (name,0)
static bool _parse(std::string_view seg, std::string& name, int& idx, bool& global)
{
    global = false; idx = 0;
    if (seg.empty()) return false;

    auto pos = seg.rfind('#');
    if (pos == std::string_view::npos) { name.assign(seg.data(), seg.size()); return true; }

    auto ip = seg.substr(pos + 1);
    if (ip.empty()) { name.assign(seg.data(), seg.size()); return true; }

    int val = 0;
    for (char c : ip) {
        if (c < '0' || c > '9') { name.assign(seg.data(), seg.size()); return true; }
        val = val * 10 + (c - '0');
    }
    idx = val;

    auto kp = seg.substr(0, pos);
    if (kp.empty()) global = true;
    else name.assign(kp.data(), kp.size());
    return true;
}

// ============================================================================
// Node — key
// ============================================================================

bool Node::isKey(const std::string& key)
{
    if (key.empty()) return false;
    if (key.find('/') != std::string::npos) return false;

    auto pos = key.rfind('#');
    if (pos == std::string::npos) return true;   // plain name

    // must have digits after #
    if (pos + 1 >= key.size()) return false;
    for (std::size_t i = pos + 1; i < key.size(); ++i)
        if (key[i] < '0' || key[i] > '9') return false;

    return true;   // name#N  or  #N
}

int Node::keyIndex(const std::string& key)
{
    auto pos = key.rfind('#');
    if (pos == std::string::npos || pos + 1 >= key.size()) return -1;

    int val = 0;
    for (std::size_t i = pos + 1; i < key.size(); ++i) {
        if (key[i] < '0' || key[i] > '9') return -1;
        val = val * 10 + (key[i] - '0');
    }
    return val;
}

std::string Node::keyOf(const Node* child) const
{
    if (!child || !_p->children) return "";
    LockT lk(mutex());

    int gi = indexOf(child);
    if (gi < 0) return "";

    auto& nm = child->name();
    if (nm.empty()) return "#" + std::to_string(gi);

    auto* iv = _p->children->indices.ptr(nm);
    if (!iv || iv->sizeAsInt() <= 1) return nm;

    // find overlap index within the same-name group
    for (uint32_t k = 0; k < iv->size(); ++k)
        if ((*iv)[k] == gi) return nm + "#" + std::to_string(k);

    return nm;
}

Node* Node::childAt(const std::string& key) const
{
    std::string nm; int idx; bool gl;
    if (!_parse(key, nm, idx, gl)) return nullptr;
    return gl ? child(idx) : child(nm, idx);
}

// ============================================================================
// Node — container interface (iterators)
// ============================================================================

Node::ChildIterator Node::begin() const
{
    if (!_p->children || _p->children->nodes.empty()) return ChildIterator(nullptr);
    return ChildIterator(_p->children->nodes.data());
}

Node::ChildIterator Node::end() const
{
    if (!_p->children || _p->children->nodes.empty()) return ChildIterator(nullptr);
    auto& v = _p->children->nodes;
    return ChildIterator(v.data() + v.size());
}

Node::ReverseChildIterator Node::rbegin() const
{
    if (!_p->children || _p->children->nodes.empty()) return ReverseChildIterator(nullptr);
    auto& v = _p->children->nodes;
    return ReverseChildIterator(v.data() + v.size());
}

Node::ReverseChildIterator Node::rend() const
{
    if (!_p->children || _p->children->nodes.empty()) return ReverseChildIterator(nullptr);
    return ReverseChildIterator(_p->children->nodes.data());
}

// ============================================================================
// Node — shadow
// ============================================================================

Node* Node::shadow() const { return _p->shadow; }
void  Node::setShadow(Node* s) { LockT lk(mutex()); _p->shadow = s; }

// ============================================================================
// Node — path
// ============================================================================

// walk path segments, calling step(name, idx, global) for each
template<typename StepFn>
static Node* _walk(std::string_view sv, const Node* start, StepFn step)
{
    const Node* cur = start;
    while (!sv.empty() && cur) {
        auto slash = sv.find('/');
        auto seg = (slash == std::string_view::npos) ? sv : sv.substr(0, slash);
        sv = (slash == std::string_view::npos) ? std::string_view{} : sv.substr(slash + 1);
        if (seg.empty()) continue;

        std::string nm; int idx; bool gl;
        if (!_parse(seg, nm, idx, gl)) return nullptr;
        cur = step(const_cast<Node*>(cur), nm, idx, gl);
    }
    return const_cast<Node*>(cur);
}

static Node* _root(const Node* n) { auto* p = const_cast<Node*>(n); while (p->parent()) p = p->parent(); return p; }

Node* Node::resolve(const std::string& path) const
{
    if (path.empty()) return const_cast<Node*>(this);
    std::string_view sv(path);
    Node* s = const_cast<Node*>(this);
    if (sv[0] == '/') { s = _root(this); sv.remove_prefix(1); if (sv.empty()) return s; }
    return _walk(sv, s, [](Node* cur, auto& nm, int idx, bool gl) -> Node* {
        return gl ? cur->child(idx) : cur->child(nm, idx);
    });
}

std::string Node::path(Node* ancestor) const
{
    if (this == ancestor) return "";
    auto* p = _p->parent;
    auto seg = p ? p->keyOf(this) : name();
    if (!p || p == ancestor) return seg;
    auto pp = p->path(ancestor);
    return pp.empty() ? seg : pp + "/" + seg;
}

Node* Node::ensure(const std::string& path)
{
    if (path.empty()) return this;
    std::string_view sv(path);
    Node* start = this;
    if (sv[0] == '/') { start = _root(this); sv.remove_prefix(1); if (sv.empty()) return start; }
    return _walk(sv, start, [](Node* cur, auto& nm, int idx, bool gl) -> Node* {
        std::string key = gl ? "" : nm;
        int have = cur->count(key);
        for (int i = have; i <= idx; ++i) cur->insert(new Node(key));
        return cur->child(key, idx);
    });
}

bool Node::erase(const std::string& path, bool auto_delete)
{
    auto* t = resolve(path);
    if (!t || !t->_p->parent) return false;
    if (auto_delete) return t->_p->parent->remove(t);
    return t->_p->parent->take(t) != nullptr;
}

// ============================================================================
// Node — debug
// ============================================================================

std::string Node::dump(int depth) const
{
    LockT lk(mutex());
    std::string indent(depth * 2, ' ');
    auto key = _p->parent ? _p->parent->keyOf(this) : name();
    if (key.empty()) key = "(anon)";

    std::string out = indent + key;
    if (_p->shadow) out += "  -> " + _p->shadow->name();
    out += "\n";

    if (_p->children)
        for (auto* c : _p->children->nodes)
            if (c) out += c->dump(depth + 1);
    return out;
}

} // namespace ve
