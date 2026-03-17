// node.cpp — ve::Node + ve::Schema
#include "ve/core/node.h"
#include "ve/core/var.h"
#include <algorithm>
#include <charconv>
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

    struct Children : Pooled<Children> {
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
    Var*      value    = nullptr;

    Children* ensureChildren()
    {
        return children ? children : (children = new Children());
    }
    void clearChildren(bool auto_delete)
    {
        if (!children) return;
        for (const auto* cn : children->nodes)
            if (cn) { cn->_p->parent = nullptr; if (auto_delete) delete cn; }
        delete children;
        children = nullptr;
    }
};

// ============================================================================
// Node — construction / static
// ============================================================================

Node::Node(const std::string& name) : Object(name) {}
Node::~Node() { _p->clearChildren(true); delete _p->value; }

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
    if (index < 0 || index >= _p->children->nodes.sizeAsInt()) return nullptr;
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

int Node::indexOf(const Node* child_node, int guess) const
{
    if (!child_node || !_p->children) return -1;
    LockT lk(mutex());

    auto& nodes = _p->children->nodes;
    const int sz = nodes.sizeAsInt();

    if (!child_node->name().empty()) {
        // named: search via indices map (fast)
        if (const auto* iv = _p->children->indices.ptr(child_node->name()); iv) {
            for (const int i : *iv)
                if (nodes.value(i, nullptr) == child_node) return i;
        }
        return -1;
    }

    // anonymous: linear scan — use guess for bidirectional expansion
    if (guess >= 0 && guess < sz) {
        // check guess first
        if (nodes.at(guess) == child_node) return guess;
        // expand outward: guess-1, guess+1, guess-2, guess+2, ...
        for (int d = 1; d < sz; ++d) {
            int lo = guess - d;
            int hi = guess + d;
            if (lo >= 0 && nodes.at(lo) == child_node) return lo;
            if (hi < sz && nodes.at(hi) == child_node) return hi;
            if (lo < 0 && hi >= sz) break; // both out of range
        }
    } else {
        // no guess: plain forward scan
        for (int i = 0; i < sz; ++i)
            if (nodes.at(i) == child_node) return i;
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

bool Node::insert(Node* child, int index)
{
    if (!child) {
        veLogE << "<ve.node> insert null child to " << path();
        return false;
    }
    if (child->parent()) {
        veLogW << "<ve.node> insert child with parent " << child->path() << " to " << path();
        child->parent()->take(child);
    }

    std::string key;
    {
        LockT lk(mutex());
        auto* ch = _p->ensureChildren();
        int sz = ch->nodes.sizeAsInt();

        // resolve negative index: -1 → append, -2 → before last, ...
        if (index < 0) index += sz + 1;
        if (index < 0 || index > sz) {
            veLogE << "<ve.node> insert index " << index << " out of range [0," << sz << "] on " << path();
            return false;
        }

        auto& nm = child->name();
        int oi = -1; // overlap index within same-name group (-1 = sole)

        if (index == sz) {
            // append — fast path, no shift needed
            ch->nodes.push_back(child);
            if (!nm.empty()) {
                auto& iv = ch->indices[nm];
                iv.push_back(sz);
                if (iv.sizeAsInt() > 1) oi = iv.sizeAsInt() - 1;
            }
        } else {
            // insert at position — shift existing indices
            ch->shift(index, +1);
            ch->nodes.insert(ch->nodes.begin() + index, child);
            if (!nm.empty()) {
                auto& iv = ch->indices[nm];
                auto it = std::lower_bound(iv.begin(), iv.end(), index);
                oi = static_cast<int>(it - iv.begin());
                iv.insert(it, index);
                if (iv.sizeAsInt() <= 1) oi = -1;
            }
        }
        child->_p->parent = this;

        // build key (index already known — no indexOf needed)
        key = toKey(nm, nm.empty() ? index : oi);
    }

    trigger<NODE_ADDED>(key, 0);
    activate(NODE_ADDED, this);
    return true;
}

bool Node::insert(const Nodes& children, int index)
{
    if (children.empty()) return true;

    for (auto* c : children) {
        if (!c) {
            veLogE << "<ve.node> batch insert contains null child to " << path();
            return false;
        }
    }

    // detach from existing parents (before locking, take() will lock)
    for (auto* c : children) {
        if (c->parent()) {
            veLogW << "<ve.node> batch insert child with parent " << c->path() << " to " << path();
            c->parent()->take(c);
        }
    }

    std::string firstKey;
    int batch = 0;
    {
        LockT lk(mutex());
        auto* ch = _p->ensureChildren();
        int sz    = ch->nodes.sizeAsInt();
        batch = static_cast<int>(children.size());

        // resolve negative index
        if (index < 0) index += sz + 1;
        if (index < 0 || index > sz) {
            veLogE << "<ve.node> batch insert index " << index << " out of range [0," << sz << "] on " << path();
            return false;
        }

        // shift existing indices >= pos by batch size (once!)
        if (index < sz) ch->shift(index, +batch);

        // bulk-insert into flat vector
        ch->nodes.insert(ch->nodes.begin() + index, children.begin(), children.end());

        // update indices + parent for each child in the batch
        int firstOI = -1; // overlap index for first child
        for (int i = 0; i < batch; ++i) {
            auto* c  = children[i];
            int   gi = index + i;
            c->_p->parent = this;
            if (!c->name().empty()) {
                auto& iv = ch->indices[c->name()];
                auto  it = std::lower_bound(iv.begin(), iv.end(), gi);
                if (i == 0) firstOI = static_cast<int>(it - iv.begin());
                iv.insert(it, gi);
                if (i == 0 && iv.sizeAsInt() <= 1) firstOI = -1;
            }
        }

        // build firstKey (index already known)
        auto& nm = children[0]->name();
        firstKey = toKey(nm, nm.empty() ? index : firstOI);
    }

    trigger<NODE_ADDED>(firstKey, batch - 1);
    activate(NODE_ADDED, this);
    return true;
}

Node* Node::append(const std::string& name, int overlap)
{
    if (overlap < 0) return nullptr;
    if (overlap == 0) {
        auto* cn = new Node(name);
        insert(cn);          // insert(child, -1) → append
        return cn;
    }
    // batch: create 1 + overlap nodes, single batch insert
    Nodes batch;
    batch.reserve(1 + overlap);
    for (int i = 0; i <= overlap; ++i)
        batch.push_back(new Node(name));
    insert(batch);            // batch insert at end (-1)
    return batch.front();
}

Node* Node::take(Node* child)
{
    if (!child || !_p->children) return nullptr;

    std::string key;
    {
        LockT lk(mutex());

        auto& nm = child->name();
        int pos = -1;
        int oi  = -1; // overlap index (-1 = sole)

        if (nm.empty()) {
            // anonymous: linear scan (only way without extra indexing)
            for (int i = 0; i < _p->children->nodes.sizeAsInt(); ++i) {
                if (_p->children->nodes.at(i) == child) { pos = i; break; }
            }
        } else {
            // named: one-pass → find pos + overlap index + erase from indices
            auto it = _p->children->indices.find(nm);
            if (it != _p->children->indices.end()) {
                auto& ivec = it->second;
                for (uint32_t j = 0; j < ivec.size(); ++j) {
                    if (_p->children->nodes.value(ivec[j], nullptr) == child) {
                        pos = ivec[j];
                        if (ivec.sizeAsInt() > 1) oi = static_cast<int>(j);
                        ivec.erase(j);
                        break;
                    }
                }
                if (ivec.empty()) _p->children->indices.erase(it);
            }
        }

        if (pos < 0) return nullptr;

        // build key before removal (pos/oi already known)
        key = nm.empty() ? toKey("", pos) : toKey(nm, oi);

        // remove from flat vector + shift remaining indices
        _p->children->nodes.erase(_p->children->nodes.begin() + pos);
        _p->children->shift(pos, -1);

        child->_p->parent = nullptr;
    }

    trigger<NODE_REMOVED>(key, 0);
    activate(NODE_REMOVED, this);
    return child;
}

bool Node::remove(Node* child)
{
    if (auto* n = take(child)) { delete n; return true; }
    return false;
}

bool Node::remove(const std::string& name)
{
    if (name.empty()) return remove(last());
    bool removed = false;
    while (const auto* n = take(name)) {
        delete n;
        removed = true;
    }
    return removed;
}

void Node::clear(bool auto_delete)
{
    if (!_p->children) return;
    int cnt;
    {
        LockT lk(mutex());
        cnt = _p->children->nodes.sizeAsInt();
        _p->clearChildren(auto_delete);
    }
    if (cnt > 0) {
        trigger<NODE_REMOVED>(std::string("#0"), cnt - 1);
        activate(NODE_REMOVED, this);
    }
}

// ============================================================================
// Node — key helpers
// ============================================================================

// ============================================================================
// Node — key
// ============================================================================

// Core parser: "name#N"→(name,N)  "#N"→("",N)  "name"→(name,-1)  ""→false
bool Node::parseKey(std::string_view key, std::string_view& name, int& index)
{
    index = -1;
    if (key.empty()) { name = {}; return false; }

    auto pos = key.rfind('#');
    if (pos == std::string_view::npos) { name = key; return true; } // plain name

    // parse digits after #
    auto dp = key.substr(pos + 1);
    if (dp.empty()) { name = key; return true; }  // trailing '#' with no digits — treat whole as name

    int val = 0;
    for (char c : dp) {
        if (c < '0' || c > '9') { name = key; return true; }  // non-digit — treat whole as name
        val = val * 10 + (c - '0');
    }

    index = val;
    name  = key.substr(0, pos);   // may be empty for "#N" (global)
    return true;
}

std::string Node::toKey(std::string_view name, int index)
{
    // inverse of parseKey: (name,-1)→"name"  ("",N)→"#N"  (name,N)→"name#N"
    char buf[16];
    if (name.empty()) {
        auto [p, _] = std::to_chars(buf, buf + sizeof(buf), index);
        std::string r;
        r.reserve(1 + static_cast<size_t>(p - buf));
        r.push_back('#');
        r.append(buf, p);
        return r;
    }
    if (index < 0) return std::string(name);
    auto [p, _] = std::to_chars(buf, buf + sizeof(buf), index);
    std::string r;
    r.reserve(name.size() + 1 + static_cast<size_t>(p - buf));
    r.append(name);
    r.push_back('#');
    r.append(buf, p);
    return r;
}

bool Node::isKey(const std::string& key)
{
    if (key.find('/') != std::string::npos) return false;
    std::string_view nm; int idx;
    if (!parseKey(key, nm, idx)) return false;
    // key with '#' must have been successfully parsed (idx >= 0)
    return key.find('#') == std::string::npos || idx >= 0;
}

int Node::keyIndex(const std::string& key)
{
    std::string_view nm; int idx;
    return parseKey(key, nm, idx) ? idx : -1;
}

std::string Node::keyOf(const Node* child, int guess) const
{
    if (!child || !_p->children) return "";
    LockT lk(mutex());

    int gi = indexOf(child, guess);
    if (gi < 0) return "";

    auto& nm = child->name();
    if (nm.empty()) return toKey("", gi);

    auto* iv = _p->children->indices.ptr(nm);
    if (!iv || iv->sizeAsInt() <= 1) return nm;

    for (uint32_t k = 0; k < iv->size(); ++k)
        if ((*iv)[k] == gi) return toKey(nm, static_cast<int>(k));

    return nm;
}

Node* Node::childAt(const std::string& key) const
{
    std::string_view nm; int idx;
    if (!parseKey(key, nm, idx)) return nullptr;
    if (nm.empty() && idx >= 0) return child(idx);       // global #N
    return child(std::string(nm), idx < 0 ? 0 : idx);
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
// Node — path
// ============================================================================

Node* Node::shadow() const { return _p->shadow; }
void  Node::setShadow(Node* s) { LockT lk(mutex()); _p->shadow = s; }

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

        std::string_view nmv; int idx;
        if (!Node::parseKey(seg, nmv, idx)) return nullptr;
        bool gl = nmv.empty() && idx >= 0;
        std::string nm(nmv);
        cur = step(const_cast<Node*>(cur), nm, gl ? idx : std::max(idx, 0), gl);
    }
    return const_cast<Node*>(cur);
}

static Node* _root(const Node* n) { auto* p = const_cast<Node*>(n); while (p->parent()) p = p->parent(); return p; }

Node* Node::resolve(const std::string& path, bool use_shadow) const
{
    if (path.empty()) return const_cast<Node*>(this);
    std::string_view sv(path);
    auto* s = const_cast<Node*>(this);
    if (sv[0] == '/') { s = _root(this); sv.remove_prefix(1); if (sv.empty()) return s; }
    return _walk(sv, s, [use_shadow](Node* cur, auto& nm, int idx, bool gl) -> Node* {
        auto* c = gl ? cur->child(idx) : cur->child(nm, idx);
        if (use_shadow)
            for (auto* sh = cur->shadow(); !c && sh; sh = sh->shadow())
                c = gl ? sh->child(idx) : sh->child(nm, idx);
        return c;
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
    auto* t = resolve(path, false);   // no shadow — only erase local nodes
    if (!t || !t->_p->parent) return false;
    if (auto_delete) return t->_p->parent->remove(t);
    return t->_p->parent->take(t) != nullptr;
}

// ============================================================================
// Node — signal bubbling (activate)
// ============================================================================

void Node::activate(int signal, Node* source)
{
    if (isSilent()) return;  // silent: suppress emission + stop bubbling

    // Trigger NODE_ACTIVATED on this node with (signal, source)
    Object::trigger(NODE_ACTIVATED, Var(Var::ListV{
        Var(signal), Var(static_cast<void*>(source))
    }));

    // Bubble up to parent only if parent is watching
    if (auto* p = parent()) {
        if (p->isWatching())
            p->activate(signal, source);
    }
}

// ============================================================================
// Node — value operations
// ============================================================================

static const Var _null_var;

bool Node::hasValue() const { return _p->value != nullptr; }

const Var& Node::value() const { return _p->value ? *_p->value : _null_var; }

void Node::set(const Var& v)
{
    Var nv(v);                              // copy outside lock
    {
        LockT lk(mutex());
        if (!_p->value) _p->value = new Var();
        _p->value->swap(nv);               // swap ≈ 16 bytes, no heap op
    }
    // nv = old value (swapped out); v = new value (original ref still valid)
    trigger<NODE_CHANGED>(v, nv);
    activate(NODE_CHANGED, this);
}

void Node::set(Var&& v)
{
    Var nv(std::move(v));                   // move outside lock
    const Var sig(nv);                      // snapshot for signal
    {
        LockT lk(mutex());
        if (!_p->value) _p->value = new Var();
        _p->value->swap(nv);               // swap ≈ 16 bytes, no heap op
    }
    // nv = old value; sig = new value
    trigger<NODE_CHANGED>(sig, nv);
    activate(NODE_CHANGED, this);
}

bool Node::update(const Var& v)
{
    if (_p->value && *_p->value == v) return false;
    set(v);
    return true;
}

// ============================================================================
// Node — global data tree
// ============================================================================

namespace node {

Node* root() { return &basic::_t_static_var_helper<Node>::var; }

}

Node* n(const std::string& path)
{
    return node::root()->ensure(path);
}

Node* d(const std::string& dot_path)
{
    std::string slash_path(dot_path);
    std::replace(slash_path.begin(), slash_path.end(), '.', '/');
    return node::root()->ensure(slash_path);
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
    if (_p->value) out += " = " + _p->value->toString();
    if (_p->shadow) out += "  -> " + _p->shadow->name();
    out += "\n";

    if (_p->children)
        for (auto* c : _p->children->nodes)
            if (c) out += c->dump(depth + 1);
    return out;
}

} // namespace ve
