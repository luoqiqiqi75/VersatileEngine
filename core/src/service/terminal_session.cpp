// terminal_session.cpp — TerminalSession: pure REPL logic extracted from Terminal
//
// Session navigation (cd/pwd/up/...) handled internally.
// Node-operation commands dispatch to command::call() via the flags-based system.
// No I/O — execute() returns output text as a string.

#include "ve/service/terminal_session.h"
#include "ve/core/command.h"
#include "ve/core/impl/json.h"
#include "../terminal_util.h"

#include <sstream>
#include <algorithm>

namespace ve {

using detail::isInt;
using detail::nodeSummary;

// ============================================================================
// Utilities
// ============================================================================

static std::vector<std::string> split(const std::string& s)
{
    std::vector<std::string> tokens;
    std::istringstream iss(s);
    std::string tok;
    while (iss >> tok) tokens.push_back(tok);
    return tokens;
}

static std::string commonPrefix(const std::vector<std::string>& v) {
    if (v.empty()) return {};
    std::string p = v[0];
    for (size_t i = 1; i < v.size(); ++i) {
        size_t j = 0;
        while (j < p.size() && j < v[i].size() && p[j] == v[i][j]) ++j;
        p.resize(j);
    }
    return p;
}

static const std::vector<std::string>& sessionCommandNames()
{
    static std::vector<std::string> names = {
        "cd", "pwd", "root", "up", "p", "first", "last", "prev", "next",
        "sibling", "data", "d", "n", "orphans", "adopt", "take",
        "quit", "exit"
    };
    return names;
}

static std::vector<std::string> allCommandNames()
{
    auto names = sessionCommandNames();
    auto global = command::keys();
    names.insert(names.end(), global.begin(), global.end());
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

// ============================================================================
// Private
// ============================================================================

struct TerminalSession::Private
{
    Node* root = nullptr;
    Node* cur  = nullptr;
    std::vector<std::string> history;
    std::vector<Node*> orphans;
    std::string output;

    std::string currentPath() const { return cur->path(root); }

    std::string absPath(const std::string& relPath = "") const {
        if (relPath.empty()) return cur->path(root);
        auto* n = cur->resolve(relPath, false);
        if (n) return n->path(root);
        return {};
    }

    Node* resolveRel(const std::string& relPath) const {
        return cur->resolve(relPath);
    }

    void print(const std::string& text) { output += text; }

    ~Private() {
        for (auto* o : orphans) delete o;
    }
};

// ============================================================================
// TerminalSession
// ============================================================================

TerminalSession::TerminalSession(Node* root)
    : _p(std::make_unique<Private>())
{
    _p->root = root;
    _p->cur  = root;
}

TerminalSession::~TerminalSession() = default;

std::string TerminalSession::execute(const std::string& line)
{
    _p->output.clear();

    if (line.empty()) return {};

    auto args = split(line);
    if (args.empty()) return {};

    _p->history.push_back(line);
    auto& cmd = args[0];
    auto& s = *_p;

    // --- quit/exit returns special marker ---
    if (cmd == "quit" || cmd == "exit")
        return "\x04";

    // --- Session navigation commands ---

    if (cmd == "cd") {
        if (args.size() < 2) { s.print("usage: cd <path>\n"); return s.output; }
        if (args[1] == "..") {
            if (s.cur->parent()) s.cur = s.cur->parent();
            else s.print("already at root\n");
        } else {
            auto* n = s.cur->resolve(args[1]);
            if (n) s.cur = n;
            else s.print("not found: " + args[1] + "\n");
        }
        return s.output;
    }
    if (cmd == "pwd") {
        s.print("/" + s.currentPath() + "\n");
        return s.output;
    }
    if (cmd == "root") {
        s.cur = s.root;
        return s.output;
    }
    if (cmd == "up" || cmd == "p") {
        int n = (args.size() > 1 && isInt(args[1])) ? std::stoi(args[1]) : 1;
        auto* p = s.cur->parent(n - 1);
        if (p) s.cur = p;
        else s.print("cannot go up " + std::to_string(n) + " levels\n");
        return s.output;
    }
    if (cmd == "first") {
        auto* f = s.cur->first();
        if (f) { s.cur = f; s.print("-> " + nodeSummary(f) + "\n"); }
        else s.print("(no children)\n");
        return s.output;
    }
    if (cmd == "last") {
        auto* l = s.cur->last();
        if (l) { s.cur = l; s.print("-> " + nodeSummary(l) + "\n"); }
        else s.print("(no children)\n");
        return s.output;
    }
    if (cmd == "prev") {
        auto* p = s.cur->prev();
        if (p) { s.cur = p; s.print("-> " + nodeSummary(p) + "\n"); }
        else s.print("(no prev sibling)\n");
        return s.output;
    }
    if (cmd == "next") {
        auto* n = s.cur->next();
        if (n) { s.cur = n; s.print("-> " + nodeSummary(n) + "\n"); }
        else s.print("(no next sibling)\n");
        return s.output;
    }
    if (cmd == "sibling") {
        if (args.size() < 2) { s.print("usage: sibling <offset>\n"); return s.output; }
        int off = std::stoi(args[1]);
        auto* sib = s.cur->sibling(off);
        if (sib) { s.cur = sib; s.print("-> " + nodeSummary(sib) + "\n"); }
        else s.print("no sibling at offset " + std::to_string(off) + "\n");
        return s.output;
    }
    if (cmd == "data" || cmd == "d") {
        s.cur = ve::node::root();
        s.root = s.cur;
        s.print("switched to global data root\n");
        return s.output;
    }
    if (cmd == "n") {
        if (args.size() < 2) { s.print("usage: n <slash/path>\n"); return s.output; }
        auto* target = ve::n(args[1]);
        if (target) {
            s.root = ve::node::root();
            s.cur = target;
            s.print("-> /" + target->path(s.root) + "\n");
        } else {
            s.print("failed to ensure: " + args[1] + "\n");
        }
        return s.output;
    }

    // --- Session-local orphan commands ---

    if (cmd == "take") {
        if (args.size() < 2) { s.print("usage: take <index|path>\n"); return s.output; }
        Node* taken = nullptr;
        if (isInt(args[1])) {
            int idx = std::stoi(args[1]);
            taken = s.cur->take(idx);
        } else {
            auto* c = s.cur->resolve(args[1], false);
            if (c && c->parent() == s.cur) taken = s.cur->take(c);
            else if (c) s.print("not a direct child\n");
            else        s.print("not found: " + args[1] + "\n");
        }
        if (taken) {
            s.orphans.push_back(taken);
            s.print("taken: " + nodeSummary(taken) + " -> orphan pool [" + std::to_string(s.orphans.size() - 1) + "]\n");
        } else if (isInt(args[1])) {
            s.print("no child at index " + args[1] + "\n");
        }
        return s.output;
    }
    if (cmd == "orphans") {
        if (s.orphans.empty()) { s.print("(empty)\n"); return s.output; }
        std::string out;
        for (size_t i = 0; i < s.orphans.size(); ++i)
            out += "  [" + std::to_string(i) + "] " + nodeSummary(s.orphans[i]) +
                   " (" + std::to_string(s.orphans[i]->count()) + " children)\n";
        s.print(out);
        return s.output;
    }
    if (cmd == "adopt") {
        if (args.size() < 2) { s.print("usage: adopt <orphan_index>\n"); return s.output; }
        int idx = std::stoi(args[1]);
        if (idx < 0 || idx >= (int)s.orphans.size()) { s.print("invalid orphan index\n"); return s.output; }
        auto* n = s.orphans[idx];
        s.orphans.erase(s.orphans.begin() + idx);
        s.cur->insert(n);
        s.print("adopted: " + n->path(s.root) + "\n");
        return s.output;
    }

    // --- Dispatch to global command system ---

    std::string canonical = cmd;
    if (cmd == "g") canonical = "get";
    else if (cmd == "s") canonical = "set";
    else if (cmd == "c") canonical = "child";

    if (!command::has(canonical)) {
        s.print("unknown: " + cmd + "  (type 'help')\n");
        return s.output;
    }

    // Build input LIST: [0]=absPath, [1..]=remaining args and flags
    Var::ListV list;
    size_t pathArgIdx = 0;
    std::string absPath;

    if (args.size() > 1 && !args[1].empty() && args[1][0] != '-') {
        auto* n = s.resolveRel(args[1]);
        if (n) {
            absPath = n->path(s.root);
            pathArgIdx = 1;
        }
    }
    if (absPath.empty()) absPath = s.absPath();

    list.push_back(Var(absPath));
    for (size_t i = 1; i < args.size(); ++i) {
        if (i == pathArgIdx) continue;
        list.push_back(Var(args[i]));
    }

    auto r = command::call(canonical, Var(std::move(list)));
    if (r.isSuccess() || r.isAccepted()) {
        auto& content = r.content();
        if (!content.isNull())
            s.print(content.toString());
    } else {
        s.print(r.toString() + "\n");
    }

    return s.output;
}

std::string TerminalSession::prompt() const
{
    if (_p->cur == _p->root) return "/> ";
    return "/" + _p->cur->path(_p->root) + "> ";
}

std::vector<std::string> TerminalSession::complete(const std::string& partial)
{
    size_t wordStart = partial.find_last_of(' ');
    bool completingCmd = (wordStart == std::string::npos);
    std::string prefix = completingCmd ? partial : partial.substr(wordStart + 1);

    std::vector<std::string> matches;
    if (completingCmd) {
        for (auto& name : allCommandNames())
            if (name.size() >= prefix.size() && name.compare(0, prefix.size(), prefix) == 0)
                matches.push_back(name);
    } else {
        if (_p->cur) {
            for (auto& name : _p->cur->childNames())
                if (name.size() >= prefix.size() && name.compare(0, prefix.size(), prefix) == 0)
                    matches.push_back(name);
        }
    }

    return matches;
}

Node* TerminalSession::root() const { return _p->root; }
Node* TerminalSession::current() const { return _p->cur; }

} // namespace ve
