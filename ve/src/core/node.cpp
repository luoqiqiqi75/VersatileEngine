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

namespace {

struct CopyMatchState
{
    Hash<SmallVector<Node*>> named_children;
    Node::Nodes              anonymous_children;
    Hash<int>                named_cursor;
    int                      anonymous_cursor = 0;
    std::unordered_set<Node*> matched;
};

static void _bucket_children(const Node::Nodes& children, CopyMatchState& state)
{
    for (auto* child : children) {
        if (!child) continue;
        if (child->name().empty()) state.anonymous_children.push_back(child);
        else                       state.named_children[child->name()].push_back(child);
    }
}

static Node* _take_copy_match(const Node* src_child, CopyMatchState& state)
{
    if (!src_child) return nullptr;

    if (src_child->name().empty()) {
        while (state.anonymous_cursor < state.anonymous_children.sizeAsInt()) {
            auto* match = state.anonymous_children[state.anonymous_cursor++];
            if (state.matched.insert(match).second) return match;
        }
        return nullptr;
    }

    int& cursor = state.named_cursor[src_child->name()];
    if (auto* bucket = state.named_children.ptr(src_child->name())) {
        while (cursor < bucket->sizeAsInt()) {
            auto* match = (*bucket)[cursor++];
            if (state.matched.insert(match).second) return match;
        }
    }
    return nullptr;
}

} // namespace

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

void Node::copy(const Node* other, bool auto_insert, bool auto_remove, bool auto_update)
{
    if (!other || other == this) return;

    auto src_children = other->children();
    auto dst_children = children();

    if (auto_remove && !dst_children.empty()) {
        CopyMatchState remove_state;
        _bucket_children(dst_children, remove_state);
        for (const auto* src_child : src_children)
            (void)_take_copy_match(src_child, remove_state);

        for (int i = dst_children.sizeAsInt() - 1; i >= 0; --i) {
            auto* dst_child = dst_children[i];
            if (remove_state.matched.count(dst_child) == 0)
                remove(dst_child);
        }
    }

    dst_children = children();
    CopyMatchState copy_state;
    _bucket_children(dst_children, copy_state);

    Vector<const Node*> pending_sources;
    if (auto_insert) pending_sources.reserve(src_children.size());

    auto flush_pending_before = [&](Node* anchor) {
        if (pending_sources.empty()) return;
        for (const auto* pending_src : pending_sources) {
            auto* inserted = new Node(pending_src->name());
            int insert_index = anchor ? indexOf(anchor) : count();
            if (insert_index < 0) insert_index = count();
            if (!insert(inserted, insert_index)) {
                delete inserted;
                continue;
            }
            inserted->copy(pending_src, auto_insert, auto_remove, auto_update);
        }
        pending_sources.clear();
    };

    for (const auto* src_child : src_children) {
        auto* match = _take_copy_match(src_child, copy_state);
        if (!match) {
            if (auto_insert) pending_sources.push_back(src_child);
            continue;
        }

        flush_pending_before(match);
        match->copy(src_child, auto_insert, auto_remove, auto_update);
    }

    flush_pending_before(nullptr);

    if (auto_update) update(other->get());
    else             set(other->get());
}

// ============================================================================
// Node — key helpers
// ============================================================================

// ============================================================================
// Node — key
// ============================================================================

// Core parser: "name#N"→(name,N)  "#N"→("",N)  "name"→(name,0)  "name#"→(name,0)  "#"→("",0)  "#abc"→false
bool Node::parseKey(std::string_view key, std::string_view& name, int& index)
{
    index = 0;
    if (key.empty()) { name = {}; return false; }

    auto pos = key.rfind('#');
    if (pos == std::string_view::npos) { name = key; return true; } // plain name

    // parse digits after #
    auto dp = key.substr(pos + 1);
    if (dp.empty()) { name = key.substr(0, pos); return true; }  // trailing '#' — valid, index=0

    int val = 0;
    for (char c : dp) {
        if (c < '0' || c > '9') return false;  // non-digit after # — invalid
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

const Node* Node::shadow() const { return _p->shadow; }
void  Node::setShadow(Node* s) { LockT lk(mutex()); _p->shadow = s; }

// ============================================================================
// Node — atKey (single-level key access)
// ============================================================================

Node* Node::atKey(int index, bool use_shadow) const
{
    if (Node* cn = child(index)) return cn;
    if (use_shadow) {
        if (auto* sn = shadow()) return sn->atKey(index, true);
    }
    return nullptr;
}

Node *Node::atKey(const std::string &name, int overlap, bool use_shadow) const
{
    if (Node* cn = child(name, overlap)) return cn;
    if (use_shadow) {
        if (auto* sn = shadow()) return sn->atKey(name, overlap, true);
    }
    return nullptr;
}

Node* Node::atKey(std::string_view key, bool use_shadow) const
{
    std::string_view nm; int idx;
    if (!parseKey(key, nm, idx)) return nullptr;
    return nm.empty() && idx >= 0 ? atKey(idx, use_shadow) : atKey(std::string(nm), idx < 0 ? 0 : idx, use_shadow);
}

Node* Node::atKey(int index, bool use_shadow)
{
    if (Node* cn = const_cast<const Node*>(this)->atKey(index, use_shadow)) return cn;

    if (index < 0) return nullptr;
    if (!append(index - count())) {
        veLogE << "<ve.node> at index failed append to " << path() << ", index = " << index << ", count = " << count();
        return nullptr;
    }
    Node* cn = child(index); // rematch
    if (!cn) veLogE << "<ve.node> at index failed on " << path() << " with index " << index;
    return cn;
}

Node* Node::atKey(const std::string& name, int overlap, bool use_shadow)
{
    if (Node* cn = const_cast<const Node*>(this)->atKey(name, overlap, use_shadow)) return cn;

    if (overlap < 0) return nullptr;
    if (!append(name, overlap - count(name))) {
        veLogE << "<ve.node> at index failed append to " << path() << ", key = " << toKey(name, overlap) << ", count = " << count(name);
        return nullptr;
    }
    Node* cn = child(name, overlap); // rematch
    if (!cn) veLogE << "<ve.node> at index failed on " << path() << " with key " << toKey(name, overlap);
    return cn;
}

Node* Node::atKey(std::string_view key, bool use_shadow)
{
    std::string_view nm; int idx;
    if (!parseKey(key, nm, idx)) return nullptr;
    return (nm.empty() && idx >= 0) ? at(idx, use_shadow) : at(std::string(nm), idx, use_shadow);
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

Node* Node::atPath(std::string_view path, bool use_shadow) const
{
    if (path.empty()) return const_cast<Node*>(this);
    const Node* cur = this;
    if (path[0] == '/') {
        cur = _root(this);
        path.remove_prefix(1);
        if (path.empty()) return const_cast<Node*>(cur);
    }

    while (!path.empty() && cur) {
        auto slash = path.find('/');
        auto seg = (slash == std::string_view::npos) ? path : path.substr(0, slash);
        path = (slash == std::string_view::npos) ? std::string_view{} : path.substr(slash + 1);
        if (seg.empty()) continue;

        cur = cur->atKey(seg, use_shadow);
    }
    return const_cast<Node*>(cur);
}

Node* Node::atPath(std::string_view path, bool use_shadow)
{
    if (path.empty()) return this;
    Node* cur = this;
    if (path[0] == '/') {
        cur = _root(this);
        path.remove_prefix(1);
        if (path.empty()) return cur;
    }

    while (!path.empty() && cur) {
        auto slash = path.find('/');
        auto seg = (slash == std::string_view::npos) ? path : path.substr(0, slash);
        path = (slash == std::string_view::npos) ? std::string_view{} : path.substr(slash + 1);
        if (seg.empty()) continue;

        cur = cur->atKey(seg, use_shadow);
    }
    return cur;
}

bool Node::erase(const std::string& path, bool auto_delete)
{
    auto* t = find(path, false);
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

Node* root() { return &basic::_t_static_var_helper<Node>::var; }

}

Node* n(const std::string& path, bool auto_create)
{
    return auto_create ? node::root()->at(path) : node::root()->find(path);
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
    if (!_p->value.isNull()) out += " = " + _p->value.toString();
    if (_p->shadow) out += "  -> " + _p->shadow->name();
    out += "\n";

    if (_p->children)
        for (auto* c : _p->children->nodes)
            if (c) out += c->dump(depth + 1);
    return out;
}

} // namespace ve
