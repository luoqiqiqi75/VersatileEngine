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
    Dict<SmallVector<Node*, 1>> children;   // children: name → [Node*]
    int cnt = 0;                      // total child count

    Private() { children[""]; }
};

// ============================================================================
// Node — construction / static
// ============================================================================

Node::Node(const std::string& name) : Object(name), _p(std::make_unique<Private>()) {}
Node::~Node()
{
    for (auto& kv : _p->children)
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
// Node — tree navigation
// ============================================================================

Node* Node::parent() const { return _p->parent; }
Node* Node::parent(const int level) const
{
    auto* n = _p->parent;
    for (int i = 0; i < level && n; ++i, n = n->_p->parent) {}
    return n;
}

bool Node::isAncestorOf(const Node* node) const
{
    if (!node) return false;
    for (auto* p = node->_p->parent; p; p = p->_p->parent)
        if (p == this) return true;
    return false;
}

// ============================================================================
// Node — child
// ============================================================================

Node* Node::child(const std::string& name, int index) const
{
    LockT lk(mutex());
    if (auto* v = _p->children.getptr(name)) return v->value(index, nullptr);
    return _p->shadow ? _p->shadow->child(name, index) : nullptr;
}

Node* Node::child(int global_index) const
{
    LockT lk(mutex());
    if (global_index < 0) return nullptr;
    for (auto& kv : _p->children) {
        int s = kv.value.sizeAsInt();
        if (global_index < s) return kv.value[(uint32_t)global_index];
        global_index -= s;
    }
    return _p->shadow ? _p->shadow->child(global_index) : nullptr;
}

bool Node::has(const std::string& name, int index) const
{
    LockT lk(mutex());
    if (const auto* v = _p->children.getptr(name); v && !v->empty()) return v->sizeAsInt() > index && index >= 0;
    return _p->shadow ? _p->shadow->has(name, index) : false;
}

bool Node::has(int global_index) const
{
    LockT lk(mutex());
    if (global_index >= 0 && global_index < _p->cnt) return true;
    return _p->shadow ? _p->shadow->has(global_index) : false;
}

bool Node::has(const Node* child) const
{
    if (!child) return false;
    LockT lk(mutex());
    if (const auto* v = _p->children.getptr(child->name())) {
        for (const auto* n : *v) {
            if (n == child) return true;
        }
    }
    return false;
}

int Node::count() const { return _p->cnt; }

int Node::count(const std::string& name) const
{
    LockT lk(mutex());
    const auto* v = _p->children.getptr(name);
    return v ? (int)v->size() : 0;
}

Vector<Node*> Node::children() const
{
    LockT lk(mutex());
    Vector<Node*> out;
    out.reserve((size_t)_p->cnt);
    for (auto& kv : _p->children) for (auto* n : kv.value) out.push_back(n);
    return out;
}

Vector<Node*> Node::children(const std::string& name) const
{
    LockT lk(mutex());
    Vector<Node*> out;
    auto* v = _p->children.getptr(name);
    if (v) { out.reserve(v->size()); for (auto* n : *v) out.push_back(n); }
    return out;
}

Strings Node::childNames() const
{
    LockT lk(mutex());
    Strings out;
    for (auto& kv : _p->children) if (!kv.key.empty()) out.push_back(kv.key);
    return out;
}

Node* Node::first() const
{
    LockT lk(mutex());
    for (auto& kv : _p->children) if (!kv.value.empty()) return kv.value[0];
    return nullptr;
}

Node* Node::last() const
{
    LockT lk(mutex());
    Node* r = nullptr;
    for (auto& kv : _p->children) if (!kv.value.empty()) r = kv.value[kv.value.size() - 1];
    return r;
}

template<bool IsGlobal> int Node::indexOf(const Node* child) const
{
    if (!child || child->parent() != this) return -1;
    LockT lk(mutex());
    auto* v = _p->children.getptr(child->name());
    if (!v) return -1;
    if constexpr (IsGlobal) {
        int gi = 0;
        for (auto& kv : _p->children) {
            if (&kv.value != v) { gi += kv.value.sizeAsInt(); continue; }
            for (uint32_t i = 0; i < kv.value.size(); ++i)
                if (kv.value[i] == child) return gi + (int)i;
            return -1;
        }
    } else {
        for (uint32_t i = 0; i < v->size(); ++i)
            if (v->at(i) == child) return (int)i;
    }
    return -1;
}

template VE_API int Node::indexOf<true>(const Node*) const;
template VE_API int Node::indexOf<false>(const Node*) const;

template<bool IsGlobal> Node* Node::sibling(int offset) const
{
    auto* p = _p->parent;
    if (!p) return nullptr;
    LockT lk(p->mutex());

    // locate this node in its name group via Dict iterator
    auto it = p->_p->children.find(name());
    if (it == p->_p->children.end()) return nullptr;
    auto& grp = it->value;
    int li = -1;
    for (uint32_t i = 0; i < grp.size(); ++i)
        if (grp[i] == this) { li = (int)i; break; }
    if (li < 0) return nullptr;

    // fast: within same name group
    int t = li + offset;
    if (t >= 0 && t < (int)grp.size()) return grp[(uint32_t)t];
    if constexpr (!IsGlobal) return nullptr;

    // cross-group: walk adjacent Dict entries (no global indexOf)
    if (t >= (int)grp.size()) {
        int rem = t - (int)grp.size();
        auto fwd = it; ++fwd;
        while (fwd != p->_p->children.end()) {
            int gs = (int)fwd->value.size();
            if (rem < gs) return fwd->value[(uint32_t)rem];
            rem -= gs;
            ++fwd;
        }
    } else { // t < 0
        int rem = t; // negative
        auto bwd = it; --bwd;
        while (bwd) {
            int gs = (int)bwd->value.size();
            rem += gs;
            if (rem >= 0) return bwd->value[(uint32_t)rem];
            --bwd;
        }
    }
    return nullptr;
}

template VE_API Node* Node::sibling<true>(int) const;
template VE_API Node* Node::sibling<false>(int) const;

// ============================================================================
// Node — child management
// ============================================================================

bool Node::insert(Node* child)
{
    if (!child) {
        veLogE << "<ve.node> insert null child to " << path();
        return false;
    }
    if (!isValidName(child->name())) {
        veLogE << "<ve.node> insert invalid child " << child->name() << " to " << path();
        return false;
    }
    if (child->parent()) {
        veLogW << "<ve.node> insert child with parent " << child->path() << " to " << path();
        child->parent()->take(child);
    }
    LockT lk(mutex());
    child->_p->parent = this;
    _p->children[child->name()].push_back(child);
    ++_p->cnt;
    trigger(NODE_CHILD_ADDED);
    return true;
}

bool Node::insert(Node* child, int index, bool auto_fill)
{
    if (!child || index < 0) {
        veLogE << "<ve.node> insert null child (index " << index << ") to " << path();
        return false;
    }
    if (!isValidName(child->name())) {
        veLogE << "<ve.node> insert invalid child " << child->name() << " to " << path();
        return false;
    }
    if (child->parent()) {
        veLogW << "<ve.node> insert child with parent " << child->path() << " to " << path();
        child->parent()->take(child);
    }
    LockT lk(mutex());
    auto& grp = _p->children[child->name()];
    int gs = (int)grp.size();
    if (index > gs) {
        if (auto_fill) {
            for (int i = gs; i < index; ++i) {
                auto* f = new Node(child->name());
                f->_p->parent = this;
                grp.push_back(f);
                ++_p->cnt;
            }
        } else { // invalid index just append
            index = gs;
        }
    }
    child->_p->parent = this;
    grp.insert(grp.begin() + index, child);
    ++_p->cnt;
    trigger(NODE_CHILD_ADDED);
    return true;
}

Node* Node::append(const std::string& name)
{
    Node* n = new Node(name);
    if (insert(n)) return n;
    delete n;
    return nullptr;
}
Node* Node::append(const std::string& name, int index, bool auto_fill)
{
    Node* n = new Node(name);
    if (insert(n, index, auto_fill)) return n;
    delete n;
    return nullptr;
}
Node* Node::append(int index, bool auto_fill)
{
    return append("", index, auto_fill);
}

Node* Node::take(Node* child)
{
    if (!child) return nullptr;
    LockT lk(mutex());
    auto* v = _p->children.getptr(child->name());
    if (!v) return nullptr;
    for (uint32_t i = 0; i < v->size(); ++i) {
        if (v->at(i) != child) continue;
        v->erase(i);
        --_p->cnt;
        child->_p->parent = nullptr;
        if (v->empty() && !child->name().empty()) _p->children.erase(child->name());
        trigger(NODE_CHILD_REMOVED);
        return child;
    }
    return nullptr;
}
Node* Node::take(const std::string& name, int index) { return take(child(name, index)); }

bool Node::remove(Node* child)
{
    if (auto n = take(child)) {
        delete n;
        return true;
    }
    return false;
}

bool Node::remove(const std::string& name, int index)
{
    if (auto n = take(name, index)) {
        delete n;
        return true;
    }
    return false;
}
bool Node::remove(const std::string& name)
{
    bool removed = false;
    while (auto* n = take(name)) { delete n; removed = true; }
    return removed;
}

void Node::clear(bool auto_delete)
{
    LockT lk(mutex());
    for (auto& kv : _p->children)
        for (auto* c : kv.value)
            if (c) { c->_p->parent = nullptr; if (auto_delete) delete c; }
    _p->children.clear();
    _p->children[""];
    _p->cnt = 0;
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

std::string Node::keyOf(const Node* child) const
{
    int i = indexOf<false>(child);
    if (i < 0) return "";
    auto& nm = child->name();
    if (nm.empty()) return "#" + std::to_string(indexOf<true>(child));
    if (count(nm) <= 1) return nm;
    return nm + "#" + std::to_string(i);
}

Node* Node::childAt(const std::string& key) const
{
    std::string nm; int idx; bool gl;
    if (!_parse(key, nm, idx, gl)) return nullptr;
    return gl ? child(idx) : child(nm, idx);
}

// ============================================================================
// Node — container interface
// ============================================================================

using MapElem = impl::HashMapElement<std::string, SmallVector<Node*, 1>>;

static const MapElem* _skip_fwd(const MapElem* e)
{
    while (e && e->data.value.empty()) e = e->next;
    return e;
}

static const MapElem* _skip_bwd(const MapElem* e)
{
    while (e && e->data.value.empty()) e = e->prev;
    return e;
}

// --- forward ---

Node* Node::ChildIterator::operator*() const
{
    auto* e = static_cast<const MapElem*>(_e);
    return e ? e->data.value[_i] : nullptr;
}

Node::ChildIterator& Node::ChildIterator::operator++()
{
    if (!_e) return *this;
    auto* e = static_cast<const MapElem*>(_e);
    if (++_i >= e->data.value.size()) {
        _e = _skip_fwd(e->next);
        _i = 0;
    }
    return *this;
}

Node::ChildIterator Node::begin() const
{
    LockT lk(mutex());
    return ChildIterator(_skip_fwd(_p->children.begin().element()), 0);
}

Node::ChildIterator Node::end() const { return ChildIterator(nullptr, 0); }

// --- reverse ---

Node* Node::ReverseChildIterator::operator*() const
{
    auto* e = static_cast<const MapElem*>(_e);
    return e ? e->data.value[_i] : nullptr;
}

Node::ReverseChildIterator& Node::ReverseChildIterator::operator++()
{
    if (!_e) return *this;
    if (_i > 0) { --_i; return *this; }
    auto* e = _skip_bwd(static_cast<const MapElem*>(_e)->prev);
    _e = e;
    _i = e ? (uint32_t)(e->data.value.size() - 1) : 0;
    return *this;
}

Node::ReverseChildIterator Node::rbegin() const
{
    LockT lk(mutex());
    auto* e = _skip_bwd(_p->children.last().element());
    uint32_t i = e ? (uint32_t)(e->data.value.size() - 1) : 0;
    return ReverseChildIterator(e, i);
}

Node::ReverseChildIterator Node::rend() const { return ReverseChildIterator(nullptr, 0); }

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

    for (auto& kv : _p->children)
        for (auto* c : kv.value)
            if (c) out += c->dump(depth + 1);
    return out;
}

} // namespace ve
