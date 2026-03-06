// node.cpp — ve::Node + ve::Schema
#include "ve/core/node.h"
#include <string_view>

namespace ve {

// ============================================================================
// Schema
// ============================================================================

void Schema::build(Node* node) const
{
    if (!node) return;
    for (auto& f : fields) {
        auto* c = new Node();
        node->append(f.name, c);
        if (f.sub) f.sub->build(c);
    }
}

// ============================================================================
// Node::Private
// ============================================================================

struct Node::Private
{
    std::string name;
    Node* parent  = nullptr;
    Node* shadow  = nullptr;
    Dict<SmallVector<Node*, 1>> ch;   // children: name → [Node*]
    int cnt = 0;                      // total child count
    mutable std::recursive_mutex mtx;

    explicit Private(const std::string& n) : name(n) { ch[""]; }
};

using Lock = std::lock_guard<std::recursive_mutex>;

// ============================================================================
// Node — construction / static
// ============================================================================

Node::Node(const std::string& name) : _p(std::make_unique<Private>(name)) {}

Node::~Node()
{
    for (auto& kv : _p->ch)
        for (auto* c : kv.value)
            if (c) { c->_p->parent = nullptr; delete c; }
}

Node* Node::root() { static Node r; return &r; }

bool Node::isValidName(const std::string& name)
{
    for (char c : name) if (c == '#' || c == '/') return false;
    return true;
}

// ============================================================================
// Node — identity
// ============================================================================

const std::string& Node::name() const  { return _p->name; }
std::recursive_mutex& Node::mutex() const { return _p->mtx; }

// ============================================================================
// Node — tree navigation
// ============================================================================

Node* Node::parent() const { return _p->parent; }

Node* Node::parent(int level) const
{
    auto* n = _p->parent;
    for (int i = 0; i < level && n; ++i) n = n->_p->parent;
    return n;
}

Node* Node::sibling(int offset) const
{
    if (!_p->parent) return nullptr;
    int i = _p->parent->indexOf(this);
    return i < 0 ? nullptr : _p->parent->childAt(i + offset);
}

Node* Node::prev() const { return sibling(-1); }
Node* Node::next() const { return sibling(1); }

Node* Node::first() const
{
    Lock lk(_p->mtx);
    for (auto& kv : _p->ch) if (!kv.value.empty()) return kv.value[0];
    return nullptr;
}

Node* Node::last() const
{
    Lock lk(_p->mtx);
    Node* r = nullptr;
    for (auto& kv : _p->ch) if (!kv.value.empty()) r = kv.value[kv.value.size() - 1];
    return r;
}

int Node::indexInParent() const { return _p->parent ? _p->parent->indexOf(this) : -1; }

bool Node::isAncestorOf(const Node* node) const
{
    if (!node) return false;
    for (auto* p = node->_p->parent; p; p = p->_p->parent)
        if (p == this) return true;
    return false;
}

// ============================================================================
// Node — child by name
// ============================================================================

Node* Node::child(const std::string& name) const
{
    Lock lk(_p->mtx);
    auto* v = _p->ch.getptr(name);
    if (v && !v->empty()) return (*v)[0];
    return _p->shadow ? _p->shadow->child(name) : nullptr;
}

Node* Node::child(const std::string& name, int index) const
{
    Lock lk(_p->mtx);
    auto* v = _p->ch.getptr(name);
    if (v && index >= 0 && index < (int)v->size()) return (*v)[(uint32_t)index];
    return _p->shadow ? _p->shadow->child(name, index) : nullptr;
}

Node* Node::childAt(int index) const
{
    Lock lk(_p->mtx);
    if (index < 0) return nullptr;
    int r = index;
    for (auto& kv : _p->ch) {
        int s = (int)kv.value.size();
        if (r < s) return kv.value[(uint32_t)r];
        r -= s;
    }
    return _p->shadow ? _p->shadow->childAt(index) : nullptr;
}

int Node::childCount() const { return _p->cnt; }

int Node::childCount(const std::string& name) const
{
    Lock lk(_p->mtx);
    auto* v = _p->ch.getptr(name);
    return v ? (int)v->size() : 0;
}

bool Node::hasChild(const std::string& name) const
{
    Lock lk(_p->mtx);
    auto* v = _p->ch.getptr(name);
    if (v && !v->empty()) return true;
    return _p->shadow ? _p->shadow->hasChild(name) : false;
}

int Node::indexOf(const Node* child) const
{
    if (!child) return -1;
    Lock lk(_p->mtx);
    int idx = 0;
    for (auto& kv : _p->ch)
        for (uint32_t i = 0; i < kv.value.size(); ++i, ++idx)
            if (kv.value[i] == child) return idx;
    return -1;
}

Strings Node::childNames() const
{
    Lock lk(_p->mtx);
    Strings out;
    for (auto& kv : _p->ch) if (!kv.key.empty()) out.push_back(kv.key);
    return out;
}

Vector<Node*> Node::children() const
{
    Lock lk(_p->mtx);
    Vector<Node*> out;
    out.reserve((size_t)_p->cnt);
    for (auto& kv : _p->ch) for (auto* n : kv.value) out.push_back(n);
    return out;
}

Vector<Node*> Node::children(const std::string& name) const
{
    Lock lk(_p->mtx);
    Vector<Node*> out;
    auto* v = _p->ch.getptr(name);
    if (v) { out.reserve(v->size()); for (auto* n : *v) out.push_back(n); }
    return out;
}

// ============================================================================
// Node — child management
// ============================================================================

// detach child from its current parent (no delete)
static void _detach(Node* child, Node* new_parent)
{
    auto* p = child->parent();
    if (p && p != new_parent) p->remove(child, false);
}

bool Node::insert(const std::string& name, Node* child)
{
    if (!child || !isValidName(name)) return false;
    Lock lk(_p->mtx);
    _detach(child, this);
    child->_p->name = name;
    child->_p->parent = this;
    _p->ch[name].push_back(child);
    ++_p->cnt;
    return true;
}

bool Node::insert(const std::string& name, int index, Node* child)
{
    if (!child || !isValidName(name) || index < 0) return false;
    Lock lk(_p->mtx);
    auto* ex = _p->ch.getptr(name);
    int gs = ex ? (int)ex->size() : 0;
    if (index > gs) return false;
    _detach(child, this);
    child->_p->name = name;
    child->_p->parent = this;
    auto& v = _p->ch[name];
    v.insert(v.begin() + index, child);
    ++_p->cnt;
    return true;
}

bool Node::insertAt(int index, Node* child, bool auto_fill)
{
    if (!child || index < 0) return false;
    Lock lk(_p->mtx);
    auto& anon = _p->ch[""];
    int as = (int)anon.size();
    if (index > as) {
        if (!auto_fill) return false;
        for (int i = as; i < index; ++i) {
            auto* f = new Node();
            f->_p->parent = this;
            anon.push_back(f);
            ++_p->cnt;
        }
    }
    _detach(child, this);
    child->_p->name = "";
    child->_p->parent = this;
    anon.insert(anon.begin() + index, child);
    ++_p->cnt;
    return true;
}

Node* Node::append(const std::string& name, Node* child) { return insert(name, child) ? child : nullptr; }
Node* Node::append(Node* child) { return child ? append(child->name(), child) : nullptr; }

bool Node::remove(Node* child, bool auto_delete)
{
    if (!child) return false;
    Lock lk(_p->mtx);
    for (auto it = _p->ch.begin(); it != _p->ch.end(); ++it) {
        auto& v = it->value;
        for (uint32_t i = 0; i < v.size(); ++i) {
            if (v[i] != child) continue;
            v.erase(i);
            --_p->cnt;
            child->_p->parent = nullptr;
            if (v.empty() && !it->key.empty()) _p->ch.erase(it->key);
            if (auto_delete) delete child;
            return true;
        }
    }
    return false;
}

bool Node::remove(const std::string& name, int index, bool auto_delete)
{
    Lock lk(_p->mtx);
    auto* v = _p->ch.getptr(name);
    if (!v || index < 0 || index >= (int)v->size()) return false;
    auto* c = (*v)[(uint32_t)index];
    v->erase((uint32_t)index);
    --_p->cnt;
    c->_p->parent = nullptr;
    if (v->empty() && !name.empty()) _p->ch.erase(name);
    if (auto_delete) delete c;
    return true;
}

void Node::clearChildren(bool auto_delete)
{
    Lock lk(_p->mtx);
    for (auto& kv : _p->ch)
        for (auto* c : kv.value)
            if (c) { c->_p->parent = nullptr; if (auto_delete) delete c; }
    _p->ch.clear();
    _p->ch[""];
    _p->cnt = 0;
}

// ============================================================================
// Node — path
// ============================================================================

// build key for a node: "name", "name#N", or "#N"
static std::string _key(const Node* node)
{
    auto& nm = node->name();
    auto* p  = node->parent();

    if (nm.empty()) {
        int i = p ? p->indexOf(node) : -1;
        return i >= 0 ? "#" + std::to_string(i) : "";
    }
    if (p && p->childCount(nm) > 1) {
        auto sibs = p->children(nm);
        for (int i = 0; i < sibs.sizeAsInt(); ++i)
            if (sibs[i] == node) return nm + "#" + std::to_string(i);
    }
    return nm;
}

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

// climb to root from any node
static Node* _root(const Node* n) { auto* p = const_cast<Node*>(n); while (p->parent()) p = p->parent(); return p; }

Node* Node::resolve(const std::string& path) const
{
    if (path.empty()) return const_cast<Node*>(this);
    std::string_view sv(path);
    Node* s = const_cast<Node*>(this);
    if (sv[0] == '/') { s = _root(this); sv.remove_prefix(1); if (sv.empty()) return s; }
    return _walk(sv, s, [](Node* cur, auto& nm, int idx, bool gl) -> Node* {
        return gl ? cur->childAt(idx) : cur->child(nm, idx);
    });
}

std::string Node::path(Node* ancestor) const
{
    if (this == ancestor) return "";
    auto* p = _p->parent;
    if (!p || p == ancestor) return _key(this);
    auto pp = p->path(ancestor);
    auto seg = _key(this);
    return pp.empty() ? seg : pp + "/" + seg;
}

Node* Node::ensure(const std::string& path)
{
    if (path.empty()) return this;
    std::string_view sv(path);
    Node* start = this;
    if (sv[0] == '/') { start = const_cast<Node*>(_root(this)); sv.remove_prefix(1); if (sv.empty()) return start; }
    return _walk(sv, start, [](Node* cur, auto& nm, int idx, bool gl) -> Node* {
        std::string key = gl ? "" : nm;
        int have = cur->childCount(key);
        for (int i = have; i <= idx; ++i) cur->insert(key, new Node());
        return cur->child(key, idx);
    });
}

bool Node::erase(const std::string& path, bool auto_delete)
{
    auto* t = resolve(path);
    return (t && t->_p->parent) ? t->_p->parent->remove(t, auto_delete) : false;
}

// ============================================================================
// Node — shadow
// ============================================================================

Node* Node::shadow() const { return _p->shadow; }
void  Node::setShadow(Node* s) { Lock lk(_p->mtx); _p->shadow = s; }

// ============================================================================
// Node — debug
// ============================================================================

std::string Node::dump(int depth) const
{
    Lock lk(_p->mtx);
    std::string indent(depth * 2, ' ');
    std::string key = _key(this);
    if (key.empty()) key = "(anon)";

    std::string out = indent + key;
    if (_p->shadow) out += "  -> " + _p->shadow->name();
    out += "\n";

    for (auto& kv : _p->ch)
        for (auto* c : kv.value)
            if (c) out += c->dump(depth + 1);
    return out;
}

} // namespace ve
