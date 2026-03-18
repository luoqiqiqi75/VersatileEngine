// terminal.cpp — ve::Terminal implementation
#include "ve/service/terminal.h"
#include "ve/core/var.h"
#include "ve/core/convert.h"
#include "ve/core/impl/json.h"
#include "ve/core/log.h"
#include <sstream>
#include <fstream>
#include <algorithm>
#include <chrono>
#include <unordered_map>

namespace ve {

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

static bool isInt(const std::string& s)
{
    if (s.empty()) return false;
    size_t start = (s[0] == '-' || s[0] == '+') ? 1 : 0;
    if (start >= s.size()) return false;
    return std::all_of(s.begin() + start, s.end(), ::isdigit);
}

static bool isDouble(const std::string& s)
{
    if (s.empty()) return false;
    bool dot = false;
    size_t start = (s[0] == '-' || s[0] == '+') ? 1 : 0;
    if (start >= s.size()) return false;
    for (size_t i = start; i < s.size(); ++i) {
        if (s[i] == '.') { if (dot) return false; dot = true; }
        else if (!std::isdigit((unsigned char)s[i])) return false;
    }
    return dot;
}

static std::string restOfLine(const std::string& line, size_t arg_start = 1)
{
    size_t pos = 0;
    for (size_t i = 0; i < arg_start; ++i) {
        while (pos < line.size() && std::isspace((unsigned char)line[pos])) ++pos;
        while (pos < line.size() && !std::isspace((unsigned char)line[pos])) ++pos;
    }
    while (pos < line.size() && std::isspace((unsigned char)line[pos])) ++pos;
    return (pos < line.size()) ? line.substr(pos) : "";
}

static const char* varTypeName(Var::Type t)
{
    switch (t) {
        case Var::Null:    return "Null";
        case Var::Bool:    return "Bool";
        case Var::Int:     return "Int";
        case Var::Double:  return "Double";
        case Var::String:  return "String";
        case Var::Bin:     return "Bin";
        case Var::List:    return "List";
        case Var::Dict:    return "Dict";
        case Var::Pointer: return "Pointer";
        case Var::Custom:  return "Custom";
        default:           return "?";
    }
}

static Var parseVar(const std::string& raw)
{
    if (raw == "null")  return Var();
    if (raw == "true")  return Var(true);
    if (raw == "false") return Var(false);

    if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"')
        return Var(raw.substr(1, raw.size() - 2));

    if (isInt(raw))    return Var(static_cast<std::int64_t>(std::stoll(raw)));
    if (isDouble(raw)) return Var(std::stod(raw));

    return Var(raw);
}

static std::string varPreview(const Var& v, size_t max_len = 60)
{
    std::string s = v.toString();
    if (s.size() > max_len) s = s.substr(0, max_len - 3) + "...";
    return s;
}

static std::string nodeSummary(const Node* n)
{
    if (!n) return "(null)";
    auto nm = n->name().empty() ? "(anon)" : n->name();
    return nm;
}

// JSON utilities now provided by ve/core/impl/json.h

// ============================================================================
// Private
// ============================================================================

struct Terminal::Private
{
    Node* root = nullptr;
    Node* cur  = nullptr;
    bool  ownsRoot = false;
    Terminal::Output output;
    std::vector<Node*> orphans;
    std::vector<std::string> history;

    struct CmdEntry {
        Terminal::Handler handler;
        std::string help;
    };
    std::unordered_map<std::string, CmdEntry> commands;
};

// ============================================================================
// Terminal
// ============================================================================

Terminal::Terminal(Node* root)
    : _p(std::make_unique<Private>())
{
    if (root) {
        _p->root = root;
    } else {
        _p->root = new Node("root");
        _p->ownsRoot = true;
    }
    _p->cur = _p->root;

    // Register built-in commands
    auto reg = [this](const char* name, Handler h, const char* help = "") {
        _p->commands[name] = {std::move(h), help};
    };

    // ===== Help =====
    reg("help", [](Terminal& t, const Args&, const std::string&) {
        t.print(
            "=== Navigation ===\n"
            "  cd <path>              navigate (supports '..')\n"
            "  pwd                    print current path\n"
            "  root                   go to root\n"
            "  up/p [N]               go up N levels (default 1)\n"
            "  first / last           go to first/last child\n"
            "  prev / next            go to prev/next sibling\n"
            "  sibling <N>            go to sibling at offset N\n"
            "\n"
            "=== Viewing ===\n"
            "  ls [path]              list children (with values)\n"
            "  tree [path]            dump subtree\n"
            "  info [path]            show node details\n"
            "  names [path]           list unique child names\n"
            "\n"
            "=== Value ===\n"
            "  get/g [path]           get value (auto: int/double/bool/string)\n"
            "  set/s <val> [path]     set value (42, 3.14, true, \"hi\", null)\n"
            "  type [path]            show value type\n"
            "  unset [path]           clear value\n"
            "\n"
            "=== Data Tree ===\n"
            "  data/d                 switch to global data root (ve::node::root)\n"
            "  n <dot.path>           ensure & cd via dot-path (a.b.c -> a/b/c)\n"
            "\n"
            "=== Child Access ===\n"
            "  child/c <idx|name> [o] get child by index or name+overlap\n"
            "  at <key>               childAt(key)\n"
            "  has <name|index>       check existence\n"
            "  count [name]           count children (or by name)\n"
            "  indexof <child_path>   indexOf resolved child\n"
            "\n"
            "=== Child Management ===\n"
            "  add <name> [overlap]   append node(s), overlap = extra copies\n"
            "  addi [overlap]         append anonymous node(s)\n"
            "  ins <name> <index>     create named node, insert at index\n"
            "  mv <src> [index]       reparent resolved node to current\n"
            "  take <index|path>      detach child (kept in orphan pool)\n"
            "  rm <path>              erase at path\n"
            "  rmi <index>            remove by global index\n"
            "  rmn <name> [overlap]   remove by name + overlap\n"
            "  rmall <name>           remove all with name\n"
            "  clear                  clear all children\n"
            "  orphans                list orphan pool\n"
            "  adopt <N>              insert orphan N into current\n"
            "\n"
            "=== Key System ===\n"
            "  key <child_path>       keyOf(child)\n"
            "  iskey <str>            test if valid key\n"
            "  keyidx <str>           extract index from key\n"
            "\n"
            "=== Shadow ===\n"
            "  shadow                 show current shadow\n"
            "  setshadow <path>       set shadow\n"
            "  unshadow               clear shadow\n"
            "\n"
            "=== Path ===\n"
            "  mk <path>              ensure nodes along path\n"
            "  resolve <path>         resolve with shadow\n"
            "  find <path>            resolve without shadow\n"
            "  erase <path>           erase at path\n"
            "\n"
            "=== Flags ===\n"
            "  watch / unwatch        toggle WATCHING (signal bubbling)\n"
            "  silent / unsilent      toggle SILENT (suppress signals)\n"
            "\n"
            "=== Iterator ===\n"
            "  iter [path]            iterate children forward\n"
            "  riter [path]           iterate children reverse\n"
            "\n"
            "=== JSON ===\n"
            "  showjson [path]        print current subtree as JSON\n"
            "  savejson <file> [path] save subtree as JSON\n"
            "\n"
            "=== Other ===\n"
            "  help                   show this help\n"
            "  quit / exit            exit\n"
        );
    });

    // ===== Navigation =====
    reg("root", [](Terminal& t, const Args&, const std::string&) {
        t.setCurrent(t.root());
    });

    reg("pwd", [](Terminal& t, const Args&, const std::string&) {
        t.print("/" + t.currentPath() + "\n");
    });

    reg("up", [](Terminal& t, const Args& args, const std::string&) {
        int n = (args.size() > 1 && isInt(args[1])) ? std::stoi(args[1]) : 1;
        auto* p = t.current()->parent(n - 1);
        if (p) t.setCurrent(p);
        else t.print("cannot go up " + std::to_string(n) + " levels\n");
    });
    reg("p", _p->commands["up"].handler, "alias for up");

    reg("cd", [](Terminal& t, const Args& args, const std::string&) {
        if (args.size() < 2) { t.print("usage: cd <path>\n"); return; }
        if (args[1] == "..") {
            if (t.current()->parent()) t.setCurrent(t.current()->parent());
            else t.print("already at root\n");
        } else {
            auto* n = t.current()->resolve(args[1]);
            if (n) t.setCurrent(n);
            else t.print("not found: " + args[1] + "\n");
        }
    });

    reg("first", [](Terminal& t, const Args&, const std::string&) {
        auto* f = t.current()->first();
        if (f) { t.setCurrent(f); t.print("-> " + nodeSummary(f) + "\n"); }
        else t.print("(no children)\n");
    });

    reg("last", [](Terminal& t, const Args&, const std::string&) {
        auto* l = t.current()->last();
        if (l) { t.setCurrent(l); t.print("-> " + nodeSummary(l) + "\n"); }
        else t.print("(no children)\n");
    });

    reg("prev", [](Terminal& t, const Args&, const std::string&) {
        auto* p = t.current()->prev();
        if (p) { t.setCurrent(p); t.print("-> " + nodeSummary(p) + "\n"); }
        else t.print("(no prev sibling)\n");
    });

    reg("next", [](Terminal& t, const Args&, const std::string&) {
        auto* n = t.current()->next();
        if (n) { t.setCurrent(n); t.print("-> " + nodeSummary(n) + "\n"); }
        else t.print("(no next sibling)\n");
    });

    reg("sibling", [](Terminal& t, const Args& args, const std::string&) {
        if (args.size() < 2) { t.print("usage: sibling <offset>\n"); return; }
        int off = std::stoi(args[1]);
        auto* s = t.current()->sibling(off);
        if (s) { t.setCurrent(s); t.print("-> " + nodeSummary(s) + "\n"); }
        else t.print("no sibling at offset " + std::to_string(off) + "\n");
    });

    // ===== Viewing =====
    reg("ls", [](Terminal& t, const Args& args, const std::string&) {
        Node* target = t.current();
        if (args.size() > 1) target = t.current()->resolve(args[1]);
        if (!target) { t.print("(null)\n"); return; }
        int total = target->count();
        if (total == 0) { t.print("  (empty)\n"); return; }
        std::string out;
        for (int i = 0; i < total; ++i) {
            auto* c = target->child(i);
            auto nm = c->name().empty() ? "(anon)" : c->name();
            out += "  [" + std::to_string(i) + "] " + nm;
            auto k = target->keyOf(c);
            if (k != nm && k != "(anon)") out += "  (key: " + k + ")";
            if (c->hasValue())
                out += "  = " + varPreview(c->value());
            out += "\n";
        }
        out += "  (" + std::to_string(total) + " total)\n";
        t.print(out);
    });

    reg("tree", [](Terminal& t, const Args& args, const std::string&) {
        Node* target = t.current();
        if (args.size() > 1) target = t.current()->resolve(args[1]);
        if (!target) { t.print("(null)\n"); return; }
        t.print(target->dump());
    });

    reg("info", [](Terminal& t, const Args& args, const std::string&) {
        Node* target = t.current();
        if (args.size() > 1) target = t.current()->resolve(args[1]);
        if (!target) { t.print("(null)\n"); return; }
        auto nm = target->name().empty() ? "(anon)" : target->name();
        std::string out;
        out += "  name:      " + nm + "\n";
        out += "  path:      /" + target->path(t.root()) + "\n";
        out += "  parent:    " + std::string(target->parent() ? nodeSummary(target->parent()) : "(none)") + "\n";
        out += "  children:  " + std::to_string(target->count()) + "\n";
        out += "  empty:     " + std::string(target->empty() ? "yes" : "no") + "\n";
        out += "  shadow:    " + std::string(target->shadow() ? nodeSummary(target->shadow()) : "(none)") + "\n";
        if (target->hasValue()) {
            auto& v = target->value();
            out += "  value:     " + varPreview(v) + "\n";
            out += "  type:      " + std::string(varTypeName(v.type())) + "\n";
        } else {
            out += "  value:     (none)\n";
        }
        out += "  watching:  " + std::string(target->isWatching() ? "yes" : "no") + "\n";
        out += "  silent:    " + std::string(target->isSilent() ? "yes" : "no") + "\n";
        if (target->parent()) {
            out += "  indexOf:   " + std::to_string(target->parent()->indexOf(target)) + "\n";
            out += "  keyOf:     " + target->parent()->keyOf(target) + "\n";
        }
        auto* f = target->first();
        auto* l = target->last();
        auto* p = target->prev();
        auto* n = target->next();
        out += "  first:     " + nodeSummary(f) + "\n";
        out += "  last:      " + nodeSummary(l) + "\n";
        out += "  prev:      " + std::string(p ? nodeSummary(p) : "(none)") + "\n";
        out += "  next:      " + std::string(n ? nodeSummary(n) : "(none)") + "\n";
        t.print(out);
    });

    reg("names", [](Terminal& t, const Args& args, const std::string&) {
        Node* target = t.current();
        if (args.size() > 1) target = t.current()->resolve(args[1]);
        if (!target) { t.print("(null)\n"); return; }
        auto names = target->childNames();
        int anonCnt = 0;
        for (auto* c : *target) if (c->name().empty()) ++anonCnt;
        std::string out;
        if (anonCnt > 0) out += "  (anon) x" + std::to_string(anonCnt) + "\n";
        for (auto& n : names) {
            int cnt = target->count(n);
            if (cnt == 1) out += "  " + n + "\n";
            else          out += "  " + n + " x" + std::to_string(cnt) + "\n";
        }
        t.print(out);
    });

    // ===== Value =====
    reg("get", [](Terminal& t, const Args& args, const std::string&) {
        Node* target = t.current();
        if (args.size() > 1) target = t.current()->resolve(args[1]);
        if (!target) { t.print("not found: " + args[1] + "\n"); return; }
        if (target->hasValue()) {
            auto& v = target->value();
            t.print(varPreview(v, 256) + "  (" + varTypeName(v.type()) + ")\n");
        } else {
            t.print("(no value)\n");
        }
    });
    reg("g", _p->commands["get"].handler, "alias for get");

    reg("set", [](Terminal& t, const Args& args, const std::string& line) {
        if (args.size() < 2) { t.print("usage: set <value> [path]\n"); return; }
        Node* target = t.current();
        std::string raw;
        if (args.size() > 2) {
            auto* maybe = t.current()->resolve(args.back());
            if (maybe) {
                target = maybe;
                raw = restOfLine(line, 1);
                auto lastArg = args.back();
                auto pos = raw.rfind(lastArg);
                if (pos != std::string::npos) {
                    raw = raw.substr(0, pos);
                    while (!raw.empty() && std::isspace((unsigned char)raw.back())) raw.pop_back();
                }
            } else {
                raw = restOfLine(line, 1);
            }
        } else {
            raw = args[1];
        }
        Var v = parseVar(raw);
        target->set(std::move(v));
        t.print("set: " + varPreview(target->value()) + "  (" + varTypeName(target->value().type()) + ")\n");
    });
    reg("s", _p->commands["set"].handler, "alias for set");

    reg("type", [](Terminal& t, const Args& args, const std::string&) {
        Node* target = t.current();
        if (args.size() > 1) target = t.current()->resolve(args[1]);
        if (!target) { t.print("not found: " + args[1] + "\n"); return; }
        if (target->hasValue())
            t.print(std::string(varTypeName(target->value().type())) + "\n");
        else
            t.print("(no value)\n");
    });

    reg("unset", [](Terminal& t, const Args& args, const std::string&) {
        Node* target = t.current();
        if (args.size() > 1) target = t.current()->resolve(args[1]);
        if (!target) { t.print("not found: " + args[1] + "\n"); return; }
        target->set(Var());
        t.print("value cleared\n");
    });

    // ===== Data Tree =====
    reg("data", [](Terminal& t, const Args&, const std::string&) {
        t._p->cur = ve::node::root();
        t._p->root = t._p->cur;
        t.print("switched to global data root\n");
    });
    reg("d", _p->commands["data"].handler, "alias for data");

    reg("n", [](Terminal& t, const Args& args, const std::string&) {
        if (args.size() < 2) { t.print("usage: n <dot.path>\n"); return; }
        auto* target = ve::d(args[1]);
        if (target) {
            t._p->root = ve::node::root();
            t._p->cur = target;
            t.print("-> /" + target->path(t._p->root) + "\n");
        } else {
            t.print("failed to ensure: " + args[1] + "\n");
        }
    });

    // ===== Flags =====
    reg("watch", [](Terminal& t, const Args&, const std::string&) {
        t.current()->watch(true);
        t.print("WATCHING = on\n");
    });
    reg("unwatch", [](Terminal& t, const Args&, const std::string&) {
        t.current()->watch(false);
        t.print("WATCHING = off\n");
    });
    reg("silent", [](Terminal& t, const Args&, const std::string&) {
        t.current()->silent(true);
        t.print("SILENT = on\n");
    });
    reg("unsilent", [](Terminal& t, const Args&, const std::string&) {
        t.current()->silent(false);
        t.print("SILENT = off\n");
    });

    // ===== Child Access =====
    reg("child", [](Terminal& t, const Args& args, const std::string&) {
        if (args.size() < 2) { t.print("usage: child <index> | child <name> [overlap]\n"); return; }
        auto* cur = t.current();
        if (isInt(args[1])) {
            int idx = std::stoi(args[1]);
            auto* c = cur->child(idx);
            if (c) t.print("[" + std::to_string(idx) + "] " + nodeSummary(c) + "  (key: " + cur->keyOf(c) + ")\n");
            else   t.print("no child at index " + std::to_string(idx) + "\n");
        } else {
            int overlap = (args.size() > 2 && isInt(args[2])) ? std::stoi(args[2]) : 0;
            auto* c = cur->child(args[1], overlap);
            if (c) t.print(nodeSummary(c) + "  (index: " + std::to_string(cur->indexOf(c)) + ")\n");
            else   t.print("no child '" + args[1] + "' overlap " + std::to_string(overlap) + "\n");
        }
    });
    reg("c", _p->commands["child"].handler, "alias for child");

    reg("at", [](Terminal& t, const Args& args, const std::string&) {
        if (args.size() < 2) { t.print("usage: at <key>\n"); return; }
        auto* c = t.current()->childAt(args[1]);
        if (c) t.print(nodeSummary(c) + "  (index: " + std::to_string(t.current()->indexOf(c)) + ")\n");
        else   t.print("no child at key '" + args[1] + "'\n");
    });

    reg("has", [](Terminal& t, const Args& args, const std::string&) {
        if (args.size() < 2) { t.print("usage: has <name|index>\n"); return; }
        if (isInt(args[1])) {
            int idx = std::stoi(args[1]);
            t.print(std::string(t.current()->has(idx) ? "true" : "false") + "\n");
        } else {
            int overlap = (args.size() > 2 && isInt(args[2])) ? std::stoi(args[2]) : 0;
            t.print(std::string(t.current()->has(args[1], overlap) ? "true" : "false") + "\n");
        }
    });

    reg("count", [](Terminal& t, const Args& args, const std::string&) {
        if (args.size() > 1)
            t.print("count(\"" + args[1] + "\") = " + std::to_string(t.current()->count(args[1])) + "\n");
        else
            t.print("count() = " + std::to_string(t.current()->count()) + "\n");
    });

    reg("indexof", [](Terminal& t, const Args& args, const std::string&) {
        if (args.size() < 2) { t.print("usage: indexof <child_path>\n"); return; }
        auto* c = t.current()->resolve(args[1]);
        if (!c) { t.print("not found: " + args[1] + "\n"); return; }
        int idx = t.current()->indexOf(c);
        if (idx >= 0) t.print("indexOf = " + std::to_string(idx) + "\n");
        else          t.print("not a direct child of current node\n");
    });

    // ===== Child Management =====
    reg("add", [](Terminal& t, const Args& args, const std::string&) {
        if (args.size() < 2) { t.print("usage: add <name> [overlap]\n"); return; }
        int overlap = (args.size() > 2 && isInt(args[2])) ? std::stoi(args[2]) : 0;
        auto* n = t.current()->append(args[1], overlap);
        if (n) t.print("appended " + std::to_string(1 + overlap) + " '" + args[1] + "' -> last: " + t.current()->keyOf(n) + "\n");
        else   t.print("failed\n");
    });

    reg("addi", [](Terminal& t, const Args& args, const std::string&) {
        int overlap = (args.size() > 1 && isInt(args[1])) ? std::stoi(args[1]) : 0;
        auto* n = t.current()->append(overlap);
        if (n) t.print("appended " + std::to_string(1 + overlap) + " anon -> index: " + std::to_string(t.current()->indexOf(n)) + "\n");
        else   t.print("failed\n");
    });

    reg("ins", [](Terminal& t, const Args& args, const std::string&) {
        if (args.size() < 3) { t.print("usage: ins <name> <index>\n"); return; }
        int idx = std::stoi(args[2]);
        auto* c = new Node(args[1]);
        if (t.current()->insert(c, idx))
            t.print("inserted '" + args[1] + "' at [" + std::to_string(idx) + "]\n");
        else {
            t.print("insert failed (index " + std::to_string(idx) + ", count " + std::to_string(t.current()->count()) + ")\n");
            delete c;
        }
    });

    reg("mv", [](Terminal& t, const Args& args, const std::string&) {
        if (args.size() < 2) { t.print("usage: mv <src_path> [index]\n"); return; }
        auto* src = t.current()->resolve(args[1]);
        if (!src) { t.print("not found: " + args[1] + "\n"); return; }
        if (src == t.current()) { t.print("cannot move node into itself\n"); return; }
        if (args.size() > 2 && isInt(args[2])) {
            int idx = std::stoi(args[2]);
            if (t.current()->insert(src, idx))
                t.print("moved to [" + std::to_string(idx) + "] " + src->path(t.root()) + "\n");
            else
                t.print("insert at index " + std::to_string(idx) + " failed\n");
        } else {
            t.current()->insert(src);
            t.print("moved to " + src->path(t.root()) + "\n");
        }
    });

    reg("take", [](Terminal& t, const Args& args, const std::string&) {
        if (args.size() < 2) { t.print("usage: take <index|path>\n"); return; }
        auto& orphans = t.orphans();
        Node* taken = nullptr;
        if (isInt(args[1])) {
            int idx = std::stoi(args[1]);
            taken = t.current()->take(idx);
        } else {
            auto* c = t.current()->resolve(args[1], false);
            if (c && c->parent() == t.current()) taken = t.current()->take(c);
            else if (c) t.print("not a direct child\n");
            else        t.print("not found: " + args[1] + "\n");
        }
        if (taken) {
            orphans.push_back(taken);
            t.print("taken: " + nodeSummary(taken) + " -> orphan pool [" + std::to_string(orphans.size() - 1) + "]\n");
        } else if (isInt(args[1])) {
            t.print("no child at index " + args[1] + "\n");
        }
    });

    reg("orphans", [](Terminal& t, const Args&, const std::string&) {
        auto& orphans = t.orphans();
        if (orphans.empty()) { t.print("(empty)\n"); return; }
        std::string out;
        for (size_t i = 0; i < orphans.size(); ++i)
            out += "  [" + std::to_string(i) + "] " + nodeSummary(orphans[i]) + " (" + std::to_string(orphans[i]->count()) + " children)\n";
        t.print(out);
    });

    reg("adopt", [](Terminal& t, const Args& args, const std::string&) {
        if (args.size() < 2) { t.print("usage: adopt <orphan_index>\n"); return; }
        auto& orphans = t.orphans();
        int idx = std::stoi(args[1]);
        if (idx < 0 || idx >= (int)orphans.size()) { t.print("invalid orphan index\n"); return; }
        auto* n = orphans[idx];
        orphans.erase(orphans.begin() + idx);
        t.current()->insert(n);
        t.print("adopted: " + n->path(t.root()) + "\n");
    });

    reg("rm", [](Terminal& t, const Args& args, const std::string&) {
        if (args.size() < 2) { t.print("usage: rm <path>\n"); return; }
        if (t.current()->erase(args[1])) t.print("removed\n");
        else t.print("failed (not found or is root)\n");
    });

    reg("rmi", [](Terminal& t, const Args& args, const std::string&) {
        if (args.size() < 2) { t.print("usage: rmi <index>\n"); return; }
        int idx = std::stoi(args[1]);
        if (t.current()->remove(idx)) t.print("removed [" + std::to_string(idx) + "]\n");
        else t.print("no child at index " + std::to_string(idx) + "\n");
    });

    reg("rmn", [](Terminal& t, const Args& args, const std::string&) {
        if (args.size() < 2) { t.print("usage: rmn <name> [overlap]\n"); return; }
        if (args.size() > 2 && isInt(args[2])) {
            int overlap = std::stoi(args[2]);
            if (t.current()->remove(args[1], overlap))
                t.print("removed '" + args[1] + "' overlap " + std::to_string(overlap) + "\n");
            else t.print("not found\n");
        } else {
            if (t.current()->remove(args[1], 0))
                t.print("removed '" + args[1] + "' #0\n");
            else t.print("not found\n");
        }
    });

    reg("rmall", [](Terminal& t, const Args& args, const std::string&) {
        if (args.size() < 2) { t.print("usage: rmall <name>\n"); return; }
        if (t.current()->remove(args[1])) t.print("removed all '" + args[1] + "'\n");
        else t.print("none found\n");
    });

    reg("clear", [](Terminal& t, const Args&, const std::string&) {
        int n = t.current()->count();
        t.current()->clear();
        t.print("cleared " + std::to_string(n) + " children\n");
    });

    // ===== Key System =====
    reg("key", [](Terminal& t, const Args& args, const std::string&) {
        if (args.size() < 2) { t.print("usage: key <child_path>\n"); return; }
        auto* c = t.current()->resolve(args[1]);
        if (!c) { t.print("not found: " + args[1] + "\n"); return; }
        if (c->parent() == t.current())
            t.print("keyOf = " + t.current()->keyOf(c) + "\n");
        else if (c->parent())
            t.print("keyOf (in parent) = " + c->parent()->keyOf(c) + "\n");
        else
            t.print("(no parent)\n");
    });

    reg("iskey", [](Terminal& t, const Args& args, const std::string&) {
        if (args.size() < 2) { t.print("usage: iskey <str>\n"); return; }
        t.print(std::string(Node::isKey(args[1]) ? "true" : "false") + "\n");
    });

    reg("keyidx", [](Terminal& t, const Args& args, const std::string&) {
        if (args.size() < 2) { t.print("usage: keyidx <str>\n"); return; }
        int idx = Node::keyIndex(args[1]);
        t.print("keyIndex = " + std::to_string(idx) + "\n");
    });

    // ===== Shadow =====
    reg("shadow", [](Terminal& t, const Args&, const std::string&) {
        auto* s = t.current()->shadow();
        if (s) t.print("shadow: " + nodeSummary(s) + " (/" + s->path(t.root()) + ")\n");
        else   t.print("(no shadow)\n");
    });

    reg("setshadow", [](Terminal& t, const Args& args, const std::string&) {
        if (args.size() < 2) { t.print("usage: setshadow <path>\n"); return; }
        auto* s = t.root()->resolve(args[1]);
        if (!s) { t.print("not found: " + args[1] + "\n"); return; }
        t.current()->setShadow(s);
        t.print("shadow set to: " + s->path(t.root()) + "\n");
    });

    reg("unshadow", [](Terminal& t, const Args&, const std::string&) {
        t.current()->setShadow(nullptr);
        t.print("shadow cleared\n");
    });

    // ===== Path =====
    reg("mk", [](Terminal& t, const Args& args, const std::string&) {
        if (args.size() < 2) { t.print("usage: mk <path>\n"); return; }
        auto* n = t.current()->ensure(args[1]);
        if (n) t.print("ensured: /" + n->path(t.root()) + "\n");
        else   t.print("failed\n");
    });

    reg("resolve", [](Terminal& t, const Args& args, const std::string&) {
        if (args.size() < 2) { t.print("usage: resolve <path>\n"); return; }
        auto* n = t.current()->resolve(args[1], true);
        if (n) t.print("found: /" + n->path(t.root()) + "\n");
        else   t.print("not found\n");
    });

    reg("find", [](Terminal& t, const Args& args, const std::string&) {
        if (args.size() < 2) { t.print("usage: find <path>\n"); return; }
        auto* n = t.current()->resolve(args[1], false);
        if (n) t.print("found: /" + n->path(t.root()) + "\n");
        else   t.print("not found (without shadow)\n");
    });

    reg("erase", [](Terminal& t, const Args& args, const std::string&) {
        if (args.size() < 2) { t.print("usage: erase <path>\n"); return; }
        if (t.current()->erase(args[1])) t.print("erased\n");
        else t.print("failed (not found or is root)\n");
    });

    // ===== Iterator =====
    reg("iter", [](Terminal& t, const Args& args, const std::string&) {
        Node* target = t.current();
        if (args.size() > 1) target = t.current()->resolve(args[1]);
        if (!target) { t.print("not found\n"); return; }
        int i = 0;
        std::string out;
        for (auto* c : *target) {
            auto nm = c->name().empty() ? "(anon)" : c->name();
            out += "  [" + std::to_string(i++) + "] " + nm + "\n";
        }
        if (i == 0) out += "  (empty)\n";
        t.print(out);
    });

    reg("riter", [](Terminal& t, const Args& args, const std::string&) {
        Node* target = t.current();
        if (args.size() > 1) target = t.current()->resolve(args[1]);
        if (!target) { t.print("not found\n"); return; }
        int total = target->count();
        int i = total;
        std::string out;
        for (auto it = target->rbegin(); it != target->rend(); ++it) {
            auto* c = *it;
            auto nm = c->name().empty() ? "(anon)" : c->name();
            out += "  [" + std::to_string(--i) + "] " + nm + "\n";
        }
        if (total == 0) out += "  (empty)\n";
        t.print(out);
    });

    // ===== Schema / JSON =====
    reg("schema", [](Terminal& t, const Args& args, const std::string&) {
        if (args.size() < 2) { t.print("usage: schema <field1> [field2 ...]\n"); return; }
        for (size_t i = 1; i < args.size(); ++i)
            t.current()->append(args[i]);
        t.print("built schema with " + std::to_string(args.size() - 1) + " fields on " + nodeSummary(t.current()) + "\n");
    });

    reg("showjson", [](Terminal& t, const Args& args, const std::string&) {
        Node* target = t.current();
        if (args.size() > 1) {
            target = t.current()->resolve(args[1]);
            if (!target) { t.print("not found: " + args[1] + "\n"); return; }
        }
        t.print(json::exportTree(target));
    });

    reg("savejson", [](Terminal& t, const Args& args, const std::string&) {
        if (args.size() < 2) { t.print("usage: savejson <file> [path]\n"); return; }
        Node* target = t.current();
        if (args.size() > 2) {
            target = t.current()->resolve(args[2]);
            if (!target) { t.print("not found: " + args[2] + "\n"); return; }
        }
        std::string js = json::exportTree(target);
        std::ofstream ofs(args[1]);
        if (!ofs.is_open()) { t.print("cannot write: " + args[1] + "\n"); return; }
        ofs << js;
        t.print("saved to " + args[1] + "\n");
    });
}

Terminal::~Terminal()
{
    for (auto* o : _p->orphans) delete o;
    if (_p->ownsRoot) delete _p->root;
}

void Terminal::setOutput(Output cb)
{
    _p->output = std::move(cb);
}

void Terminal::print(const std::string& text)
{
    if (_p->output) _p->output(text);
}

bool Terminal::execute(const std::string& line)
{
    if (line.empty()) return true;

    auto args = split(line);
    if (args.empty()) return true;

    _p->history.push_back(line);

    auto& cmd = args[0];

    if (cmd == "quit" || cmd == "exit")
        return false;

    auto it = _p->commands.find(cmd);
    if (it != _p->commands.end()) {
        it->second.handler(*this, args, line);
    } else {
        print("unknown: " + cmd + "  (type 'help')\n");
    }
    return true;
}

Node* Terminal::root() const    { return _p->root; }
Node* Terminal::current() const { return _p->cur; }

void Terminal::setCurrent(Node* node)
{
    if (node) _p->cur = node;
}

std::string Terminal::currentPath() const
{
    return _p->cur->path(_p->root);
}

std::string Terminal::prompt() const
{
    if (_p->cur == _p->root) return "/> ";
    return "/" + currentPath() + "> ";
}

void Terminal::registerCommand(const std::string& name, Handler handler, const std::string& help)
{
    _p->commands[name] = {std::move(handler), help};
}

const std::vector<std::string>& Terminal::history() const
{
    return _p->history;
}

std::string Terminal::nodeToJson(const Node* node, int indent)
{
    return json::exportTree(node, indent);
}

std::vector<Node*>& Terminal::orphans()
{
    return _p->orphans;
}

} // namespace ve
