// node.cpp — ve::Node
#include "ve/core/node.h"
#include "ve/core/var.h"
#include "ve/core/log.h"

namespace ve {

// ============================================================================
// Node::Private
// ============================================================================

struct Node::Private
{
    Node* parent  = nullptr;

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

    Children*   children = nullptr;
    Var         value;

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

Node::Node(const std::string& name) : Object(name), _p(std::make_unique<Private>()) {}
Node::~Node() { _p->clearChildren(true); }

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
    LockT lk(mutex());
    if (!_p->children) return nullptr;
    if (index < 0) index += _p->children->nodes.sizeAsInt();
    if (index < 0 || index >= _p->children->nodes.sizeAsInt()) return nullptr;
    return _p->children->nodes.at(index);
}

Node* Node::child(const std::string& name, int overlap) const
{
    if (name.empty()) return child(overlap);
    LockT lk(mutex());
    if (!_p->children || overlap < 0) return nullptr;
    if (const auto* iv = _p->children->indices.ptr(name); iv) {
        return _p->children->nodes.value((*iv).value(overlap, -1), nullptr);
    }
    return nullptr;
}

int Node::indexOf(const Node* child_node, int guess) const
{
    if (!child_node) return -1;
    LockT lk(mutex());
    if (!_p->children) return -1;

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
    // Many callers use count() as a cheap existence/size check on nodes that are
    // concurrently updated (insert/take/clear all lock mutex()).
    // Without locking here, reading the underlying containers is a data race
    // and can manifest as heap corruption later (for example glibc tcache aborts).
    LockT lk(mutex());
    return _p->children ? _p->children->nodes.sizeAsInt() : 0;
}

int Node::count(const std::string& name) const
{
    if (name.empty()) return count();
    LockT lk(mutex());
    if (!_p->children) return 0;
    if (const auto* iv = _p->children->indices.ptr(name)) return iv->sizeAsInt();
    return 0;
}

Vector<Node*> Node::children() const
{
    LockT lk(mutex());
    if (!_p->children) return {};
    return _p->children->nodes;
}

Vector<Node*> Node::children(const std::string& name) const
{
    if (name.empty()) return children();
    LockT lk(mutex());
    if (!_p->children) return {};
    Vector<Node*> out;
    if (const auto* iv = _p->children->indices.ptr(name); iv) {
        for (const int i : *iv)
            out.push_back(_p->children->nodes.value(i, nullptr));
    }
    return out;
}

Strings Node::childNames() const
{
    LockT lk(mutex());
    if (!_p->children) return {};
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
    if (isWatching()) activate(NODE_ADDED, this);
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
    if (isWatching()) activate(NODE_ADDED, this);
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
    if (isWatching()) activate(NODE_REMOVED, this);
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
        if (isWatching()) activate(NODE_REMOVED, this);
    }
}

namespace {

// Walk children, calling fn(child, name, overlap, index) with each child's key parts
// (key = name#overlap for named children, #index for anonymous ones).
template <typename Fn>
void forEachKeyed(const Node::Nodes& children, Fn fn)
{
    Hash<int> seen;
    for (int i = 0; i < children.sizeAsInt(); ++i) {
        auto* c = children[i];
        if (!c) continue;
        const int overlap = c->name().empty() ? 0 : seen[c->name()]++;
        fn(c, c->name(), overlap, i);
    }
}

} // namespace

void Node::copy(const Node* other, int copy_flags, int depth)
{
    if (!other || other == this) return;

    if (depth != 0) {
        // 1. remove children whose key does not exist in other
        if (flags::get(copy_flags, COPY_REMOVE))
            forEachKeyed(children(), [&](Node* d, const std::string& name, int overlap, int index) {
                if (!(name.empty() ? other->child(index) : other->child(name, overlap))) remove(d);
            });

        // 2. each child of other lands on the node at its key — named n#k → the k-th
        //    child named n, anonymous #i → the i-th child whatever its name — and is
        //    copied recursively; an unresolved key is appended first (COPY_INSERT)
        forEachKeyed(other->children(), [&, next = depth > 0 ? depth - 1 : -1] (Node* s, const std::string& name, int overlap, int index) {
            auto* d = name.empty() ? child(index) : child(name, overlap);
            if (!d) {
                if (!flags::get(copy_flags, COPY_INSERT)) return;
                d = append(name);
                if (!d) return;
            }
            d->copy(s, copy_flags, next);
        });
    }

    // 3. own value — COPY_REPLACE overwrites anything, otherwise only fill null
    if (flags::get(copy_flags, COPY_REPLACE) || get().isNull()) {
        if (flags::get(copy_flags, COPY_UPDATE)) {
            update(other->get());
        } else {
            set(other->get());
        }
    }
}

// ============================================================================
// Node — key helpers
// ============================================================================

// ============================================================================
// Node — key
// ============================================================================

// Core parser: "name#N"→(name,N)  "#N"→("",N)  "name"→(name,0)  "name#"→(name,0)  "#"→("",0)  "#abc"→false
bool Node::parseKey(std::string_view key, std::string_view& name, int& index, char key_sep)
{
    index = 0;
    if (key.empty()) { name = {}; return false; }

    auto pos = key.rfind(key_sep);
    if (pos == std::string_view::npos) { name = key; return true; } // plain name

    // parse digits after key_sep
    auto dp = key.substr(pos + 1);
    if (dp.empty()) { name = key.substr(0, pos); return true; }  // trailing key_sep — valid, index=0

    int val = 0;
    for (char c : dp) {
        if (c < '0' || c > '9') return false;  // non-digit after key_sep — invalid
        val = val * 10 + (c - '0');
    }

    index = val;
    name  = key.substr(0, pos);   // may be empty for "#N" (global)
    return true;
}

std::string Node::toKey(std::string_view name, int index, char key_sep)
{
    // inverse of parseKey: (name,-1)→"name"  ("",N)→"#N"  (name,N)→"name#N"
    char buf[16];
    if (name.empty()) {
        auto [p, _] = std::to_chars(buf, buf + sizeof(buf), index);
        std::string r;
        r.reserve(1 + static_cast<size_t>(p - buf));
        r.push_back(key_sep);
        r.append(buf, p);
        return r;
    }
    if (index < 0) return std::string(name);
    auto [p, _] = std::to_chars(buf, buf + sizeof(buf), index);
    std::string r;
    r.reserve(name.size() + 1 + static_cast<size_t>(p - buf));
    r.append(name);
    r.push_back(key_sep);
    r.append(buf, p);
    return r;
}

int Node::keyIndex(const std::string& key, char key_sep)
{
    std::string_view nm; int idx;
    return parseKey(key, nm, idx, key_sep) ? idx : -1;
}

std::string Node::keyOf(const Node* child, int guess) const
{
    if (!child) return "";
    LockT lk(mutex());
    if (!_p->children) return "";

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

// ============================================================================
// Node — atKey (single-level key access)
// ============================================================================

Node* Node::atKey(int index) const
{
    return child(index);
}

Node *Node::atKey(const std::string &name, int overlap) const
{
    return child(name, overlap);
}

Node* Node::atKey(std::string_view key, char key_sep) const
{
    std::string_view nm; int idx;
    if (!parseKey(key, nm, idx, key_sep)) return nullptr;
    return nm.empty() && idx >= 0 ? atKey(idx) : atKey(std::string(nm), idx < 0 ? 0 : idx);
}

Node* Node::atKey(int index)
{
    if (Node* cn = const_cast<const Node*>(this)->atKey(index)) return cn;

    if (index < 0) return nullptr;
    if (!append(index - count())) {
        veLogE << "<ve.node> at index failed append to " << path() << ", index = " << index << ", count = " << count();
        return nullptr;
    }
    Node* cn = child(index); // rematch
    if (!cn) veLogE << "<ve.node> at index failed on " << path() << " with index " << index;
    return cn;
}

Node* Node::atKey(const std::string& name, int overlap)
{
    if (Node* cn = const_cast<const Node*>(this)->atKey(name, overlap)) return cn;

    if (overlap < 0) return nullptr;
    if (!append(name, overlap - count(name))) {
        veLogE << "<ve.node> at index failed append to " << path() << ", key = " << toKey(name, overlap) << ", count = " << count(name);
        return nullptr;
    }
    Node* cn = child(name, overlap); // rematch
    if (!cn) veLogE << "<ve.node> at index failed on " << path() << " with key " << toKey(name, overlap);
    return cn;
}

Node* Node::atKey(std::string_view key, char key_sep)
{
    std::string_view nm; int idx;
    if (!parseKey(key, nm, idx, key_sep)) return nullptr;
    return (nm.empty() && idx >= 0) ? at(idx) : at(std::string(nm), idx);
}

// ============================================================================
// Node — atPath (multi-level path access)
// ============================================================================

static Node* _root(const Node* n) { auto* p = const_cast<Node*>(n); while (p->parent()) p = p->parent(); return p; }

std::string Node::path(Node* ancestor) const
{
    if (this == ancestor) return "";
    auto* p = _p->parent;
    auto seg = p ? p->keyOf(this) : name();
    if (!p || p == ancestor) return seg;
    auto pp = p->path(ancestor);
    return pp.empty() ? seg : pp + "/" + seg;
}

bool Node::isName(std::string_view name, char path_sep, char key_sep)
{
    if (name.empty()) return false;
    return name.find(path_sep) == std::string_view::npos
        && name.find(key_sep)  == std::string_view::npos;
}

Node* Node::atPath(std::string_view path, char path_sep, char key_sep) const
{
    if (path.empty()) return const_cast<Node*>(this);
    const Node* cur = this;
    if (path[0] == path_sep) {
        cur = _root(this);
        path.remove_prefix(1);
        if (path.empty()) return const_cast<Node*>(cur);
    }

    while (!path.empty() && cur) {
        auto slash = path.find(path_sep);
        auto seg = (slash == std::string_view::npos) ? path : path.substr(0, slash);
        path = (slash == std::string_view::npos) ? std::string_view{} : path.substr(slash + 1);
        if (seg.empty()) continue;

        cur = cur->atKey(seg, key_sep);
    }
    return const_cast<Node*>(cur);
}

Node* Node::atPath(std::string_view path, char path_sep, char key_sep)
{
    if (path.empty()) return this;
    Node* cur = this;
    if (path[0] == path_sep) {
        cur = _root(this);
        path.remove_prefix(1);
        if (path.empty()) return cur;
    }

    while (!path.empty() && cur) {
        auto slash = path.find(path_sep);
        auto seg = (slash == std::string_view::npos) ? path : path.substr(0, slash);
        path = (slash == std::string_view::npos) ? std::string_view{} : path.substr(slash + 1);
        if (seg.empty()) continue;

        cur = cur->atKey(seg, key_sep);
    }
    return cur;
}

bool Node::erase(const std::string& path, bool auto_delete)
{
    auto* t = find(path);
    if (!t || !t->_p->parent) return false;
    if (auto_delete) return t->_p->parent->remove(t);
    return t->_p->parent->take(t) != nullptr;
}

// ============================================================================
// Node — signal bubbling (activate)
// ============================================================================

void Node::activate(SignalT signal, Node* source)
{
    if (isSilent()) return;  // silent: suppress emission + stop bubbling

    // Trigger NODE_ACTIVATED on this node with (signal, source)
    trigger<NODE_ACTIVATED>(Var::ListV{signal, static_cast<void*>(source)});

    // Bubble up to parent only if parent is watching
    if (auto* p = parent()) {
        if (p->isWatching())
            p->activate(signal, source);
    }
}

void Node::watchAll(bool on)
{
    watch(on);
    for (auto* c : *this) c->watchAll(on);
}

void Node::silentAll(bool on)
{
    silent(on);
    for (auto* c : *this) c->silentAll(on);
}

// ============================================================================
// Node — value operations
// ============================================================================

const Var& Node::value() const {
    // No lock for read - accept stale read risk
    return _p->value;
}

Node* Node::set(const Var& v)
{
    Var nv(v);  // copy outside lock
    {
        LockT lk(mutex());  // use Object's mutex
        _p->value.swap(nv);  // swap only 16 bytes, very fast
    }
    // nv now holds old value, trigger signals outside lock
    trigger<NODE_CHANGED>(v, nv);
    if (isWatching()) activate(NODE_CHANGED, this);
    return this;
}

Node* Node::set(Var&& v)
{
    Var nv(std::move(v));  // move outside lock
    const Var sig(nv);     // snapshot for signal
    {
        LockT lk(mutex());  // use Object's mutex
        _p->value.swap(nv);  // swap only 16 bytes, very fast
    }
    // nv now holds old value, trigger signals outside lock
    trigger<NODE_CHANGED>(sig, nv);
    if (isWatching()) activate(NODE_CHANGED, this);
    return this;
}

bool Node::update(const Var& v)
{
    if (value() == v) return false;
    set(v);
    return true;
}

// ============================================================================
// Node — global data tree
// ============================================================================

namespace node {

Node* root() { static auto* s = new Node(); return s; }

}

Node* n(const std::string& path, bool auto_create)
{
    return auto_create ? node::root()->at(path) : node::root()->find(path);
}

// ============================================================================
// Node — debug
// ============================================================================

static std::string dumpImpl(const Node* n, bool color, int depth, int indent, int tree)
{
    const char* dim   = color ? "\x1b[2m"  : "";
    const char* cyan  = color ? "\x1b[36m" : "";
    const char* green = color ? "\x1b[32m" : "";
    const char* reset = color ? "\x1b[0m"  : "";

    std::string out;
    if (!n->get().isNull())
        out += "= " + std::string(green) + n->get().toString() + reset + "\n";
    else
        out += std::string(dim) + "(none)" + reset + "\n";

    if (depth == 0) return out;

    int total = n->count();
    if (total == 0) return out;

    std::string pad(indent, ' ');
    std::string bar;
    for (int t = 1; t < tree; ++t) bar += "\xe2\x94\x80";
    std::string connMid  = pad + "\xe2\x94\x9c" + bar;
    std::string connLast = pad + "\xe2\x94\x94" + bar;
    std::string contMid  = pad + "\xe2\x94\x82" + std::string(tree - 1, ' ');
    std::string contLast(indent + tree, ' ');

    int next = depth > 0 ? depth - 1 : depth;
    for (int i = 0; i < total; ++i) {
        auto* ch = n->child(i);
        if (!ch) continue;
        bool last = (i == total - 1);
        auto key = n->keyOf(ch);
        if (key.empty()) key = "(anon)";

        const std::string& conn = last ? connLast : connMid;
        const std::string& cont = last ? contLast : contMid;

        std::string sub = dumpImpl(ch, color, next, indent, tree);
        size_t nl = sub.find('\n');

        out += std::string(dim) + conn + reset
             + std::string(cyan) + key + reset + " "
             + sub.substr(0, nl + 1);

        size_t pos = nl + 1;
        while (pos < sub.size()) {
            size_t end = sub.find('\n', pos);
            if (end == std::string::npos) break;
            out += std::string(dim) + cont + reset + sub.substr(pos, end - pos + 1);
            pos = end + 1;
        }
    }
    return out;
}

std::string Node::dump(int depth, int indent, int tree, bool color) const
{
    LockT lk(mutex());
    return dumpImpl(this, color, depth, indent, tree);
}

} // namespace ve
