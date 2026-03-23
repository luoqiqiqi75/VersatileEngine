// terminal_builtin_commands.cpp — TCP Terminal REPL commands (command::reg)
//
// Hybrid command style (Linux-inspired):
//   Tier 1 (high-freq): ls  info  get  set  add  rm  mv  mk  find  erase  json  help
//   Tier 2 (subcommands): child  shadow  watch  iter  schema  cmd
//
// Flags: -x (POSIX short), --long (GNU long), -abc (combined shorts)
//
// Input convention:
//   Var::LIST where [0] = absolute path, [1..] = extra args/flags
//   Flags are parsed from the list by parseFlags().

#include "ve/core/command.h"
#include "ve/core/node.h"
#include "ve/core/impl/json.h"
#include "terminal_builtins.h"
#include "terminal_util.h"
#include <fstream>
#include <mutex>
#include <sstream>

namespace ve {

using detail::varTypeName;
using detail::varPreview;
using detail::nodeSummary;
using detail::isInt;
using detail::isDouble;
using detail::parseVar;

// ============================================================================
// Flags parser — lightweight POSIX/GNU flag extraction
// ============================================================================

struct Flags {
    std::vector<std::pair<std::string, std::string>> named;
    std::vector<std::string> positional;

    bool has(const std::string& longName, char shortName = 0) const {
        for (auto& [k, _] : named)
            if (k == longName || (shortName && k.size() == 1 && k[0] == shortName))
                return true;
        return false;
    }

    std::string get(const std::string& longName, char shortName = 0,
                    const std::string& def = "") const {
        for (auto& [k, v] : named)
            if (k == longName || (shortName && k.size() == 1 && k[0] == shortName))
                return v.empty() ? def : v;
        return def;
    }

    std::string pos(int idx) const {
        return (idx >= 0 && idx < (int)positional.size()) ? positional[idx] : std::string{};
    }
    int posCount() const { return (int)positional.size(); }
};

static Flags parseFlags(const Var& v)
{
    Flags f;
    std::vector<std::string> args;
    if (v.type() == Var::LIST) {
        for (auto& item : v.toList())
            args.push_back(item.toString());
    } else if (!v.isNull()) {
        args.push_back(v.toString());
    }

    bool endOfFlags = false;
    for (size_t i = 0; i < args.size(); ++i) {
        auto& a = args[i];
        if (endOfFlags) {
            f.positional.push_back(a);
            continue;
        }
        if (a == "--") {
            endOfFlags = true;
            continue;
        }
        if (a.size() > 2 && a[0] == '-' && a[1] == '-') {
            auto eq = a.find('=', 2);
            if (eq != std::string::npos)
                f.named.push_back({a.substr(2, eq - 2), a.substr(eq + 1)});
            else if (i + 1 < args.size() && !args[i + 1].empty() && args[i + 1][0] != '-')
                f.named.push_back({a.substr(2), args[++i]});
            else
                f.named.push_back({a.substr(2), ""});
        } else if (a.size() > 1 && a[0] == '-' && !isInt(a) && !isDouble(a)) {
            for (size_t j = 1; j < a.size(); ++j) {
                std::string key(1, a[j]);
                if (j == a.size() - 1 && i + 1 < args.size() && !args[i + 1].empty() && args[i + 1][0] != '-')
                    f.named.push_back({key, args[++i]});
                else
                    f.named.push_back({key, ""});
            }
        } else {
            f.positional.push_back(a);
        }
    }
    return f;
}

static Node* resolveTarget(const Flags& f, int posIdx = 0)
{
    auto path = f.pos(posIdx);
    auto* root = ve::node::root();
    if (path.empty() || path == "/") return root;
    return root->find(path, false);
}

#define RET_OK(text)   return Result(Result::SUCCESS, Var(text))
#define RET_FAIL(text) return Result(Result::FAIL, Var(std::string(text)))
#define RESOLVE_OR_FAIL(posIdx) \
    auto* target = resolveTarget(f, posIdx); \
    if (!target) RET_FAIL("target node not found")

// ============================================================================
// terminalBuiltinsEnsureRegistered()
// ============================================================================

void terminalBuiltinsEnsureRegistered()
{
    static std::once_flag once;
    std::call_once(once, [] {
        using command::reg;
    // =================================================================
    // ls [path] [-t|--tree] [-l|--long] [-n|--names]
    // =================================================================
    reg("ls", [](const Var& input) -> Result {
        auto f = parseFlags(input);
        RESOLVE_OR_FAIL(0);

        if (f.has("tree", 't'))
            RET_OK(target->dump());

        if (f.has("names", 'n')) {
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
            RET_OK(out);
        }

        int total = target->count();
        if (total == 0) RET_OK("  (empty)\n");
        bool detailed = f.has("long", 'l');
        std::string out;
        for (int i = 0; i < total; ++i) {
            auto* c = target->child(i);
            auto nm = c->name().empty() ? "(anon)" : c->name();
            out += "  [" + std::to_string(i) + "] " + nm;
            auto k = target->keyOf(c);
            if (k != nm && k != "(anon)") out += "  (key: " + k + ")";
            if (!c->get().isNull())
                out += "  = " + varPreview(c->get());
            if (detailed) {
                out += "  (" + std::string(varTypeName(c->get().type())) + ")";
                out += "  children:" + std::to_string(c->count());
            }
            out += "\n";
        }
        out += "  (" + std::to_string(total) + " total)\n";
        RET_OK(out);
    }, "ls [path] [-t|--tree] [-l|--long] [-n|--names]  list children");

    // =================================================================
    // info [path]
    // =================================================================
    reg("info", [](const Var& input) -> Result {
        auto f = parseFlags(input);
        RESOLVE_OR_FAIL(0);
        auto nm = target->name().empty() ? "(anon)" : target->name();
        auto* root = ve::node::root();
        std::string out;
        out += "  name:      " + nm + "\n";
        out += "  path:      /" + target->path(root) + "\n";
        out += "  parent:    " + std::string(target->parent() ? nodeSummary(target->parent()) : "(none)") + "\n";
        out += "  children:  " + std::to_string(target->count()) + "\n";
        out += "  empty:     " + std::string(target->empty() ? "yes" : "no") + "\n";
        out += "  shadow:    " + std::string(target->shadow() ? nodeSummary(target->shadow()) : "(none)") + "\n";
        if (!target->get().isNull()) {
            auto& v = target->get();
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
        auto* fi = target->first();
        auto* la = target->last();
        auto* pr = target->prev();
        auto* ne = target->next();
        out += "  first:     " + nodeSummary(fi) + "\n";
        out += "  last:      " + nodeSummary(la) + "\n";
        out += "  prev:      " + std::string(pr ? nodeSummary(pr) : "(none)") + "\n";
        out += "  next:      " + std::string(ne ? nodeSummary(ne) : "(none)") + "\n";
        RET_OK(out);
    }, "info [path]  show node details");

    // =================================================================
    // get [path] [-t|--type]
    // =================================================================
    reg("get", [](const Var& input) -> Result {
        auto f = parseFlags(input);
        RESOLVE_OR_FAIL(0);
        if (f.has("type", 't')) {
            if (!target->get().isNull())
                RET_OK(std::string(varTypeName(target->get().type())) + "\n");
            RET_OK("(no value)\n");
        }
        if (!target->get().isNull()) {
            auto& v = target->get();
            RET_OK(varPreview(v, 256) + "  (" + varTypeName(v.type()) + ")\n");
        }
        RET_OK("(no value)\n");
    }, "get [path] [-t|--type]  get value or type");

    // =================================================================
    // set [path] [value] [--null]
    // =================================================================
    reg("set", [](const Var& input) -> Result {
        auto f = parseFlags(input);
        RESOLVE_OR_FAIL(0);
        if (f.has("null")) {
            target->set(Var());
            RET_OK("value cleared\n");
        }
        auto raw = f.pos(1);
        if (raw.empty()) RET_FAIL("usage: set [path] <value> [--null]");
        Var v = parseVar(raw);
        target->set(std::move(v));
        RET_OK("set: " + varPreview(target->get()) + "  (" + varTypeName(target->get().type()) + ")\n");
    }, "set [path] <value> [--null]  set or clear value");

    // =================================================================
    // add [path] [name] [-o|--overlap N] [-a|--anon] [--at INDEX]
    // =================================================================
    reg("add", [](const Var& input) -> Result {
        auto f = parseFlags(input);
        RESOLVE_OR_FAIL(0);

        auto ovStr = f.get("overlap", 'o', "0");
        int overlap = isInt(ovStr) ? std::stoi(ovStr) : 0;

        auto atStr = f.get("at");
        bool hasAt = f.has("at") && isInt(atStr);

        if (f.has("anon", 'a')) {
            auto* n = target->append(overlap);
            if (n) RET_OK("appended " + std::to_string(1 + overlap) + " anon -> index: " + std::to_string(target->indexOf(n)) + "\n");
            RET_FAIL("failed");
        }

        auto name = f.pos(1);
        if (name.empty()) RET_FAIL("usage: add [path] <name> [-o N] [-a|--anon] [--at INDEX]");

        if (hasAt) {
            int idx = std::stoi(atStr);
            auto* c = new Node(name);
            if (target->insert(c, idx))
                RET_OK("inserted '" + name + "' at [" + std::to_string(idx) + "]\n");
            delete c;
            RET_FAIL("insert failed (index " + std::to_string(idx) + ", count " + std::to_string(target->count()) + ")");
        }

        auto* n = target->append(name, overlap);
        if (n) RET_OK("appended " + std::to_string(1 + overlap) + " '" + name + "' -> last: " + target->keyOf(n) + "\n");
        RET_FAIL("failed");
    }, "add [path] <name> [-o N] [-a] [--at IDX]  append/insert child");

    // =================================================================
    // rm [path] [target] [-i|--index IDX] [-n|--name NAME [-o N]]
    //                     [--all NAME] [-c|--clear]
    // =================================================================
    reg("rm", [](const Var& input) -> Result {
        auto f = parseFlags(input);
        RESOLVE_OR_FAIL(0);

        if (f.has("clear", 'c')) {
            int n = target->count();
            target->clear();
            RET_OK("cleared " + std::to_string(n) + " children\n");
        }

        auto allName = f.get("all");
        if (f.has("all") && !allName.empty()) {
            if (target->remove(allName)) RET_OK("removed all '" + allName + "'\n");
            RET_FAIL("none found");
        }

        auto idxStr = f.get("index", 'i');
        if (f.has("index", 'i') && isInt(idxStr)) {
            int idx = std::stoi(idxStr);
            if (target->remove(idx)) RET_OK("removed [" + std::to_string(idx) + "]\n");
            RET_FAIL("no child at index " + std::to_string(idx));
        }

        auto nmStr = f.get("name", 'n');
        if (f.has("name", 'n') && !nmStr.empty()) {
            auto ovStr = f.get("overlap", 'o', "0");
            int overlap = isInt(ovStr) ? std::stoi(ovStr) : 0;
            if (target->remove(nmStr, overlap))
                RET_OK("removed '" + nmStr + "' overlap " + std::to_string(overlap) + "\n");
            RET_FAIL("not found");
        }

        auto childPath = f.pos(1);
        if (!childPath.empty()) {
            if (target->erase(childPath)) RET_OK("removed\n");
            RET_FAIL("failed (not found or is root)");
        }

        RET_FAIL("usage: rm [path] <target> | rm -i IDX | rm -n NAME [-o N] | rm --all NAME | rm -c");
    }, "rm [path] [target] [-i IDX] [-n NAME] [--all] [-c]  remove child(ren)");

    // =================================================================
    // mv [dest] [src] [--at INDEX]
    // =================================================================
    reg("mv", [](const Var& input) -> Result {
        auto f = parseFlags(input);
        RESOLVE_OR_FAIL(0);
        auto srcPath = f.pos(1);
        if (srcPath.empty()) RET_FAIL("usage: mv [dest] <src> [--at INDEX]");
        auto* src = ve::node::root()->find(srcPath, false);
        if (!src) RET_FAIL("not found: " + srcPath);
        if (src == target) RET_FAIL("cannot move node into itself");
        auto* root = ve::node::root();
        auto atStr = f.get("at");
        if (f.has("at") && isInt(atStr)) {
            int idx = std::stoi(atStr);
            if (target->insert(src, idx))
                RET_OK("moved to [" + std::to_string(idx) + "] " + src->path(root) + "\n");
            RET_FAIL("insert at index " + std::to_string(idx) + " failed");
        }
        target->insert(src);
        RET_OK("moved to " + src->path(root) + "\n");
    }, "mv [dest] <src> [--at INDEX]  reparent node");

    // =================================================================
    // mk [path] <subpath>
    // =================================================================
    reg("mk", [](const Var& input) -> Result {
        auto f = parseFlags(input);
        RESOLVE_OR_FAIL(0);
        auto path = f.pos(1);
        if (path.empty()) RET_FAIL("usage: mk <path>");
        auto* n = target->at(path);
        if (n) RET_OK("created: /" + n->path(ve::node::root()) + "\n");
        RET_FAIL("failed");
    }, "mk [path] <subpath>  ensure nodes along path");

    // =================================================================
    // find [path] [subpath] [-S|--no-shadow]
    // =================================================================
    reg("find", [](const Var& input) -> Result {
        auto f = parseFlags(input);
        RESOLVE_OR_FAIL(0);
        auto path = f.pos(1);
        if (path.empty()) RET_FAIL("usage: find [path] <subpath> [-S|--no-shadow]");
        bool useShadow = !f.has("no-shadow", 'S');
        auto* n = target->find(path, useShadow);
        if (n) RET_OK("found: /" + n->path(ve::node::root()) + "\n");
        RET_FAIL(useShadow ? "not found" : "not found (without shadow)");
    }, "find [path] <subpath> [-S|--no-shadow]  resolve path");

    // =================================================================
    // erase [path] <subpath>
    // =================================================================
    reg("erase", [](const Var& input) -> Result {
        auto f = parseFlags(input);
        RESOLVE_OR_FAIL(0);
        auto path = f.pos(1);
        if (path.empty()) RET_FAIL("usage: erase <path>");
        if (target->erase(path)) RET_OK("erased\n");
        RET_FAIL("failed (not found or is root)");
    }, "erase [path] <subpath>  erase at path");

    // =================================================================
    // json [path] [-o|--output FILE] [--import FILE]
    // =================================================================
    reg("json", [](const Var& input) -> Result {
        auto f = parseFlags(input);
        RESOLVE_OR_FAIL(0);

        auto importFile = f.get("import");
        if (f.has("import")) {
            if (importFile.empty()) RET_FAIL("usage: json --import <file>");
            std::ifstream ifs(importFile);
            if (!ifs.is_open()) RET_FAIL("cannot read: " + importFile);
            std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
            if (impl::json::importTree(target, content))
                RET_OK("imported from " + importFile + "\n");
            RET_FAIL("import failed (invalid JSON)");
        }

        std::string js = impl::json::exportTree(target);

        auto outFile = f.get("output", 'o');
        if (f.has("output", 'o') && !outFile.empty()) {
            std::ofstream ofs(outFile);
            if (!ofs.is_open()) RET_FAIL("cannot write: " + outFile);
            ofs << js;
            RET_OK("saved to " + outFile + "\n");
        }

        RET_OK(js);
    }, "json [path] [-o FILE] [--import FILE]  export/import JSON");

    // =================================================================
    // child [path] [idx|name] [-o N] [--has NAME] [--count [NAME]]
    //       [--key CHILD] [--index CHILD] [--at KEY]
    // =================================================================
    reg("child", [](const Var& input) -> Result {
        auto f = parseFlags(input);
        RESOLVE_OR_FAIL(0);

        if (f.has("has")) {
            auto arg = f.get("has");
            if (arg.empty()) RET_FAIL("usage: child --has <name|index>");
            if (isInt(arg)) {
                int idx = std::stoi(arg);
                RET_OK(std::string(target->has(idx) ? "true" : "false") + "\n");
            }
            auto ovStr = f.get("overlap", 'o', "0");
            int overlap = isInt(ovStr) ? std::stoi(ovStr) : 0;
            RET_OK(std::string(target->has(arg, overlap) ? "true" : "false") + "\n");
        }

        if (f.has("count")) {
            auto name = f.get("count");
            if (!name.empty())
                RET_OK("count(\"" + name + "\") = " + std::to_string(target->count(name)) + "\n");
            RET_OK("count() = " + std::to_string(target->count()) + "\n");
        }

        if (f.has("key")) {
            auto childPath = f.get("key");
            if (childPath.empty()) RET_FAIL("usage: child --key <child_path>");
            auto* c = target->find(childPath);
            if (!c) RET_FAIL("not found: " + childPath);
            if (c->parent() == target)
                RET_OK("keyOf = " + target->keyOf(c) + "\n");
            if (c->parent())
                RET_OK("keyOf (in parent) = " + c->parent()->keyOf(c) + "\n");
            RET_FAIL("(no parent)");
        }

        if (f.has("index")) {
            auto childPath = f.get("index");
            if (childPath.empty()) RET_FAIL("usage: child --index <child_path>");
            auto* c = target->find(childPath);
            if (!c) RET_FAIL("not found: " + childPath);
            int idx = target->indexOf(c);
            if (idx >= 0) RET_OK("indexOf = " + std::to_string(idx) + "\n");
            RET_FAIL("not a direct child of target node");
        }

        if (f.has("at")) {
            auto key = f.get("at");
            if (key.empty()) RET_FAIL("usage: child --at <key>");
            auto* c = target->childAt(key);
            if (c) RET_OK(nodeSummary(c) + "  (index: " + std::to_string(target->indexOf(c)) + ")\n");
            RET_FAIL("no child at key '" + key + "'");
        }

        auto arg1 = f.pos(1);
        if (arg1.empty()) RET_FAIL("usage: child [path] <idx|name> [--has|--count|--key|--index|--at]");
        if (isInt(arg1)) {
            int idx = std::stoi(arg1);
            auto* c = target->child(idx);
            if (c) RET_OK("[" + std::to_string(idx) + "] " + nodeSummary(c) + "  (key: " + target->keyOf(c) + ")\n");
            RET_FAIL("no child at index " + std::to_string(idx));
        }
        auto ovStr = f.get("overlap", 'o', "0");
        int overlap = isInt(ovStr) ? std::stoi(ovStr) : 0;
        auto* c = target->child(arg1, overlap);
        if (c) RET_OK(nodeSummary(c) + "  (index: " + std::to_string(target->indexOf(c)) + ")\n");
        RET_FAIL("no child '" + arg1 + "' overlap " + std::to_string(overlap));
    }, "child [path] <idx|name> [--has|--count|--key|--index|--at]  child access");

    // =================================================================
    // shadow [path] [--set TARGET] [--clear]
    // =================================================================
    reg("shadow", [](const Var& input) -> Result {
        auto f = parseFlags(input);
        RESOLVE_OR_FAIL(0);

        if (f.has("clear")) {
            target->setShadow(nullptr);
            RET_OK("shadow cleared\n");
        }

        auto setPath = f.get("set");
        if (f.has("set")) {
            if (setPath.empty()) RET_FAIL("usage: shadow --set <path>");
            auto* s = ve::node::root()->find(setPath, false);
            if (!s) RET_FAIL("not found: " + setPath);
            target->setShadow(s);
            RET_OK("shadow set to: " + s->path(ve::node::root()) + "\n");
        }

        auto* s = target->shadow();
        auto* root = ve::node::root();
        if (s) RET_OK("shadow: " + nodeSummary(s) + " (/" + s->path(root) + ")\n");
        RET_OK("(no shadow)\n");
    }, "shadow [path] [--set TARGET] [--clear]  shadow operations");

    // =================================================================
    // watch [path] [--off] [--all] [--silent [--off] [--all]]
    // =================================================================
    reg("watch", [](const Var& input) -> Result {
        auto f = parseFlags(input);
        RESOLVE_OR_FAIL(0);
        bool off = f.has("off");

        if (f.has("silent")) {
            if (f.has("all")) {
                target->silentAll(!off);
                RET_OK("SILENT " + std::string(!off ? "on" : "off") + " (recursive)\n");
            }
            target->silent(!off);
            RET_OK("SILENT = " + std::string(!off ? "on" : "off") + "\n");
        }

        if (f.has("all")) {
            target->watchAll(!off);
            RET_OK("WATCHING " + std::string(!off ? "on" : "off") + " (recursive)\n");
        }
        target->watch(!off);
        RET_OK("WATCHING = " + std::string(!off ? "on" : "off") + "\n");
    }, "watch [path] [--off] [--all] [--silent]  watch/silent flags");

    // =================================================================
    // iter [path] [-r|--reverse]
    // =================================================================
    reg("iter", [](const Var& input) -> Result {
        auto f = parseFlags(input);
        RESOLVE_OR_FAIL(0);
        bool rev = f.has("reverse", 'r');
        std::string out;
        if (rev) {
            int total = target->count();
            int i = total;
            for (auto it = target->rbegin(); it != target->rend(); ++it) {
                auto* c = *it;
                auto nm = c->name().empty() ? "(anon)" : c->name();
                out += "  [" + std::to_string(--i) + "] " + nm + "\n";
            }
            if (total == 0) out += "  (empty)\n";
        } else {
            int i = 0;
            for (auto* c : *target) {
                auto nm = c->name().empty() ? "(anon)" : c->name();
                out += "  [" + std::to_string(i++) + "] " + nm + "\n";
            }
            if (i == 0) out += "  (empty)\n";
        }
        RET_OK(out);
    }, "iter [path] [-r|--reverse]  iterate children");

    // =================================================================
    // schema [path] <field1> [field2 ...]
    // =================================================================
    reg("schema", [](const Var& input) -> Result {
        auto f = parseFlags(input);
        RESOLVE_OR_FAIL(0);
        int n = f.posCount();
        if (n < 2) RET_FAIL("usage: schema [path] <field1> [field2 ...]");
        for (int i = 1; i < n; ++i)
            target->append(f.pos(i));
        RET_OK("built schema with " + std::to_string(n - 1) + " fields on " + nodeSummary(target) + "\n");
    }, "schema [path] <fields...>  build schema on node");

    // =================================================================
    // cmd call|list|help|has [key] [args...]
    // =================================================================
    reg("cmd", [](const Var& input) -> Result {
        auto f = parseFlags(input);
        auto sub = f.pos(0);
        if (sub.empty() || sub == "list") {
            auto cmds = command::keys();
            std::string out;
            for (auto& k : cmds) {
                auto h = command::help(k);
                out += "  " + k;
                if (!h.empty()) {
                    int pad = 22 - (int)k.size();
                    out += std::string(pad > 0 ? pad : 2, ' ') + h;
                }
                out += "\n";
            }
            RET_OK(out);
        }
        if (sub == "has") {
            auto key = f.pos(1);
            if (key.empty()) RET_FAIL("usage: cmd has <key>");
            RET_OK(std::string(command::has(key) ? "true" : "false") + "\n");
        }
        if (sub == "help") {
            auto key = f.pos(1);
            if (key.empty()) RET_FAIL("usage: cmd help <key>");
            auto h = command::help(key);
            if (h.empty()) RET_FAIL("unknown command: " + key);
            RET_OK(key + ": " + h + "\n");
        }
        if (sub == "call") {
            auto key = f.pos(1);
            if (key.empty()) RET_FAIL("usage: cmd call <key> [args...]");
            Var::ListV args;
            for (int i = 2; i < f.posCount(); ++i)
                args.push_back(Var(f.pos(i)));
            Var callInput = args.empty() ? Var() : Var(std::move(args));
            return command::call(key, callInput);
        }
        RET_FAIL("usage: cmd [call|list|help|has] ...");
    }, "cmd [call|list|help|has]  command management");

    // =================================================================
    // help [command]
    // =================================================================
    reg("help", [](const Var& input) -> Result {
        auto f = parseFlags(input);
        auto specific = f.pos(0);

        if (!specific.empty()) {
            auto h = command::help(specific);
            if (!h.empty()) RET_OK(specific + ": " + h + "\n");
            RET_FAIL("unknown command: " + specific);
        }

        auto cmds = command::keys();
        std::string out = "=== Registered Commands ===\n";
        for (auto& k : cmds) {
            auto h = command::help(k);
            out += "  " + k;
            if (!h.empty()) {
                int pad = 22 - (int)k.size();
                out += std::string(pad > 0 ? pad : 2, ' ') + h;
            }
            out += "\n";
        }
        out += "\n=== Session Commands (Terminal only) ===\n";
        out += "  cd <path>              navigate (supports '..')\n";
        out += "  pwd                    print current path\n";
        out += "  root                   go to root\n";
        out += "  up/p [N]               go up N levels\n";
        out += "  first / last           go to first/last child\n";
        out += "  prev / next            go to prev/next sibling\n";
        out += "  sibling <N>            go to sibling at offset\n";
        out += "  data/d                 switch to global data root\n";
        out += "  n <dot.path>           ensure & cd via dot-path\n";
        out += "  orphans                list orphan pool\n";
        out += "  adopt <N>              adopt orphan into current\n";
        out += "  take <index|path>      detach child to orphan pool\n";
        out += "  quit / exit            disconnect\n";
        RET_OK(out);
    }, "help [command]  show help");

    // =================================================================
    // Aliases for backward compatibility
    // =================================================================

    reg("tree", [](const Var& input) -> Result {
        Var::ListV args;
        if (input.type() == Var::LIST) for (auto& i : input.toList()) args.push_back(i);
        else if (!input.isNull()) args.push_back(input);
        args.push_back(Var("--tree"));
        return command::call("ls", Var(std::move(args)));
    }, "(alias) ls --tree");

    reg("names", [](const Var& input) -> Result {
        Var::ListV args;
        if (input.type() == Var::LIST) for (auto& i : input.toList()) args.push_back(i);
        else if (!input.isNull()) args.push_back(input);
        args.push_back(Var("--names"));
        return command::call("ls", Var(std::move(args)));
    }, "(alias) ls --names");

    reg("type", [](const Var& input) -> Result {
        Var::ListV args;
        if (input.type() == Var::LIST) for (auto& i : input.toList()) args.push_back(i);
        else if (!input.isNull()) args.push_back(input);
        args.push_back(Var("--type"));
        return command::call("get", Var(std::move(args)));
    }, "(alias) get --type");

    reg("unset", [](const Var& input) -> Result {
        Var::ListV args;
        if (input.type() == Var::LIST) for (auto& i : input.toList()) args.push_back(i);
        else if (!input.isNull()) args.push_back(input);
        args.push_back(Var("--null"));
        return command::call("set", Var(std::move(args)));
    }, "(alias) set --null");

    reg("showjson", [](const Var& input) -> Result {
        return command::call("json", input);
    }, "(alias) json");

    reg("savejson", [](const Var& input) -> Result {
        Var::ListV args;
        if (input.type() == Var::LIST) {
            auto& list = input.toList();
            if (list.size() >= 2) {
                args.push_back(list[0]);
                args.push_back(Var("--output"));
                args.push_back(list[1]);
            }
        }
        return command::call("json", Var(std::move(args)));
    }, "(alias) json --output");

    reg("resolve", [](const Var& input) -> Result {
        return command::call("find", input);
    }, "(alias) find (with shadow)");
    });
}

#undef RET_OK
#undef RET_FAIL
#undef RESOLVE_OR_FAIL

} // namespace ve
