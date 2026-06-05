// terminal_session.cpp - TerminalSession: pure REPL logic
//
// All terminal commands are session-local, using cur/root directly.
// Registered commands are executed through Command/Pipeline as fallback.

#include "terminal_session.h"
#include "ve/core/command.h"
#include "ve/core/pipeline.h"
#include "ve/core/impl/json.h"
#include "ve/core/impl/bin.h"
#include "ve/core/impl/xml.h"
#include "ve/core/impl/md.h"
#include "ve/core/schema.h"
#include "terminal_util.h"

#include <sstream>
#include <iomanip>
#include <fstream>
#include <algorithm>
#include <functional>
#include <unordered_map>
#include <mutex>

namespace ve {
namespace service {

using detail::isInt;
using detail::isDouble;
using detail::nodeSummary;
using detail::varTypeName;
using detail::varPreview;
using detail::parseVar;
using detail::Flags;
using detail::parseFlags;

// ============================================================================
// Helpers
// ============================================================================

static std::vector<std::string> split(const std::string& line)
{
    std::vector<std::string> tokens;
    std::string current;
    bool in_quote = false;
    char quote_char = '\0';
    bool escaped = false;

    for (size_t i = 0; i < line.size(); ++i) {
        char ch = line[i];

        if (escaped) {
            current.push_back(ch);
            escaped = false;
            continue;
        }

        if (ch == '\\') {
            escaped = true;
            continue;
        }

        if (!in_quote && (ch == '"' || ch == '\'')) {
            in_quote = true;
            quote_char = ch;
            continue;
        }

        if (in_quote && ch == quote_char) {
            in_quote = false;
            quote_char = '\0';
            continue;
        }

        if (!in_quote && std::isspace(static_cast<unsigned char>(ch))) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
            continue;
        }

        current.push_back(ch);
    }

    if (!current.empty()) {
        tokens.push_back(current);
    }

    return tokens;
}

static std::vector<std::string> completeNodePath(Node* root, Node* cur, const std::string& token)
{
    if (!root || !cur) return {};
    bool absolute = !token.empty() && token[0] == '/';
    size_t slash = token.find_last_of('/');
    std::string parentPath, leafPrefix;
    if (slash == std::string::npos) leafPrefix = token;
    else { parentPath = token.substr(0, slash); leafPrefix = token.substr(slash + 1); }
    Node* base = absolute ? root : cur;
    if (!parentPath.empty()) {
        if (absolute) {
            std::string rel = parentPath;
            if (!rel.empty() && rel[0] == '/') rel.erase(rel.begin());
            base = rel.empty() ? root : root->find(rel, false);
        } else {
            base = cur->find(parentPath, false);
        }
    }
    if (!base) return {};
    std::string pathPrefix = (slash == std::string::npos) ? "" : token.substr(0, slash + 1);
    std::vector<std::string> matches;
    for (auto* c : *base) {
        auto nm = c->name();
        if (!nm.empty() && nm.size() >= leafPrefix.size() && nm.compare(0, leafPrefix.size(), leafPrefix) == 0)
            matches.push_back(pathPrefix + nm);
    }
    return matches;
}

// Resolve the longest matching command from the front of tokens in one factory.
// Returns {factory-node, wordCount}. node is null if nothing matched.
static std::pair<Node*, size_t> resolveFactoryCommand(const Factory& factory,
                                                      const std::vector<std::string>& args)
{
    for (size_t i = args.size(); i >= 1; --i) {
        std::string candidate;
        for (size_t j = 0; j < i; ++j) {
            if (j > 0) candidate += ".";
            candidate += args[j];
        }
        if (Node* n = factory.node(candidate, VE_FACTORY_KEY_SEP)) {
            return {n, i};
        }
    }
    return {nullptr, 0};
}

static void prepareCommandInput(Node* in,
                                Node* root,
                                Node* current,
                                const std::vector<std::string>& args,
                                size_t start)
{
    if (!in) return;
    in->clear();
    Node* argv = in->at("argv", false);
    for (const auto& arg : args) {
        argv->append()->set(arg);
    }
    in->set("argc", static_cast<int64_t>(args.size()));
    in->set("command_words", static_cast<int64_t>(start));
    if (root) {
        in->at("root", false)->set(Var(static_cast<void*>(root)));
    }
    if (current) {
        in->at("current", false)->set(Var(static_cast<void*>(current)));
    }
}

static void updateCurrentFromOut(Node*& current, Node* out)
{
    if (!out) return;
    if (auto* cn = out->find("current", false)) {
        if (auto* p = cn->get().toPointer()) {
            current = static_cast<Node*>(p);
        }
    }
}

static std::string renderCommandOutput(Node* out, const Result& r)
{
    if (r.isError()) {
        return r.message.empty() ? std::string("command failed\n") : r.message + "\n";
    }
    if (out) {
        if (auto* text = out->find("text", false)) {
            return text->getString();
        }
        if (!out->get().isNull()) {
            return out->get().toString();
        }
    }
    return {};
}

static std::string availableSchemaFormatsText()
{
    std::vector<std::string> formats = schema::schemaFormatNames();
    if (std::find(formats.begin(), formats.end(), "var") == formats.end()) {
        formats.push_back("var");
    }

    std::string out = "available formats:";
    for (size_t i = 0; i < formats.size(); ++i) {
        out += (i == 0 ? " " : ", ");
        out += formats[i];
    }
    return out;
}

struct BuiltinContext
{
    Node* root = nullptr;
    Node* cur = nullptr;
    std::string output;

    std::string currentPath() const { return cur && root ? cur->path(root) : std::string{}; }

    Node* resolve(const std::string& path) const {
        if (!cur || !root) return nullptr;
        if (path.empty() || path == ".") return cur;
        if (path == "..") return cur->parent() ? cur->parent() : cur;
        bool absolute = path[0] == '/';
        Node* base = absolute ? root : cur;
        std::string relPath = absolute ? path.substr(1) : path;
        if (relPath.empty()) return base;
        return base->find(relPath);
    }

    Node* target(const std::vector<std::string>& args, int argIdx = 1) const {
        if ((int)args.size() > argIdx && !args[argIdx].empty() && args[argIdx][0] != '-') {
            auto* n = resolve(args[argIdx]);
            if (n) return n;
        }
        return cur;
    }

    void print(const std::string& text) { output += text; }
};

static Node* pointerInput(Node* in, const std::string& key, Node* fallback = nullptr)
{
    if (in) {
        if (auto* n = in->find(key)) {
            if (auto* p = n->get().toPointer()) {
                return static_cast<Node*>(p);
            }
        }
    }
    return fallback;
}

static std::vector<std::string> argvInput(Node* in)
{
    std::vector<std::string> args;
    if (!in) return args;
    if (auto* argv = in->find("argv", false)) {
        for (auto* arg : *argv) {
            args.push_back(arg->getString());
        }
    }
    return args;
}

static void writeBuiltinOut(BuiltinContext& s, Node* out)
{
    if (!out) return;
    if (!s.output.empty()) {
        out->at("text")->set(s.output);
    }
    if (s.cur) {
        out->at("current")->set(Var(static_cast<void*>(s.cur)));
        if (s.root) {
            out->at("path")->set(s.cur->path(s.root));
        }
    }
}

template<typename F>
static void regBuiltin(Factory& f, const std::string& key, F&& fn, const std::string& help = {})
{
    Proc proc = [key, fn = std::forward<F>(fn)](Node*, Node* in, Node* out) -> Result {
            BuiltinContext s;
            s.root = pointerInput(in, "root", node::root());
            s.cur = pointerInput(in, "current", s.root);
            auto args = argvInput(in);
            if (args.empty()) {
                args.push_back(key);
            }
            try {
                fn(s, args);
            } catch (const std::exception& e) {
                return Result::fail(e.what());
            } catch (...) {
                return Result::fail("builtin command failed");
            }
            writeBuiltinOut(s, out);
            return Result::ok();
    };

    command::regInto(f, key,
        [key, proc, help](Node* ctx, Node* in, Node* out) -> Command {
            return Command(key, proc, ctx, in, out, nullptr, help);
        },
        help);
}

static void registerTerminalBuiltins()
{
    static std::once_flag once;
    std::call_once(once, [] {
        auto& builtins = factory::at("builtin");
        using S = BuiltinContext;
        using Args = const std::vector<std::string>&;

        regBuiltin(builtins, "cd", [](S& s, Args args) {
            if (args.size() < 2) { s.print("usage: cd <path>\n"); return; }
            if (args[1] == ".") return;
            if (args[1] == "..") {
                if (s.cur->parent()) s.cur = s.cur->parent();
                else s.print("already at root\n");
            } else {
                auto* n = s.cur->find(args[1]);
                if (n) s.cur = n; else s.print("not found: " + args[1] + "\n");
            }
        }, "cd <path>");

        regBuiltin(builtins, "pwd", [](S& s, Args) {
            s.print("/" + s.currentPath() + "\n");
        }, "pwd");

        regBuiltin(builtins, "root", [](S& s, Args) { s.cur = s.root; }, "root");

        regBuiltin(builtins, "up", [](S& s, Args args) {
            int n = (args.size() > 1 && isInt(args[1])) ? std::stoi(args[1]) : 1;
            auto* p = s.cur->parent(n - 1);
            if (p) s.cur = p; else s.print("cannot go up " + std::to_string(n) + " levels\n");
        }, "up [N]");

        regBuiltin(builtins, "first", [](S& s, Args) {
            auto* f = s.cur->first();
            if (f) { s.cur = f; s.print("-> " + nodeSummary(f) + "\n"); }
            else s.print("(no children)\n");
        }, "first");
        regBuiltin(builtins, "last", [](S& s, Args) {
            auto* l = s.cur->last();
            if (l) { s.cur = l; s.print("-> " + nodeSummary(l) + "\n"); }
            else s.print("(no children)\n");
        }, "last");
        regBuiltin(builtins, "prev", [](S& s, Args) {
            auto* p = s.cur->prev();
            if (p) { s.cur = p; s.print("-> " + nodeSummary(p) + "\n"); }
            else s.print("(no prev sibling)\n");
        }, "prev");
        regBuiltin(builtins, "next", [](S& s, Args) {
            auto* n = s.cur->next();
            if (n) { s.cur = n; s.print("-> " + nodeSummary(n) + "\n"); }
            else s.print("(no next sibling)\n");
        }, "next");
        regBuiltin(builtins, "sibling", [](S& s, Args args) {
            if (args.size() < 2) { s.print("usage: sibling <offset>\n"); return; }
            int off = std::stoi(args[1]);
            auto* sib = s.cur->sibling(off);
            if (sib) { s.cur = sib; s.print("-> " + nodeSummary(sib) + "\n"); }
            else s.print("no sibling at offset " + std::to_string(off) + "\n");
        }, "sibling <offset>");

        regBuiltin(builtins, "ls", [](S& s, Args args) {
            auto f = parseFlags(args);
            const int lp = f.posCount();
            Node* t = nullptr;
            if (lp == 0) {
                t = s.cur;
            } else if (lp == 1) {
                t = s.resolve(f.pos(0));
                if (!t) { s.print("not found: " + f.pos(0) + "\n"); return; }
            } else {
                s.print("usage: ls [path] [-t] [-l] [-n]\n");
                return;
            }
            if (f.has("tree", 't')) { s.print(t->dump()); return; }
            if (f.has("names", 'n')) {
                auto names = t->childNames();
                int anonCnt = 0;
                for (auto* c : *t) if (c->name().empty()) ++anonCnt;
                std::string out;
                if (anonCnt > 0) out += "  (anon) x" + std::to_string(anonCnt) + "\n";
                for (auto& nm : names) out += "  " + nm + "\n";
                s.print(out); return;
            }
            if (f.has("long", 'l')) {
                auto nm = t->name().empty() ? "(anon)" : t->name();
                std::string out;
                out += "  name:      " + nm + "\n";
                out += "  path:      /" + t->path(s.root) + "\n";
                out += "  parent:    " + std::string(t->parent() ? nodeSummary(t->parent()) : "(none)") + "\n";
                out += "  children:  " + std::to_string(t->count()) + "\n";
                out += "  empty:     " + std::string(t->empty() ? "yes" : "no") + "\n";
                out += "  shadow:    " + std::string(t->shadow() ? nodeSummary(t->shadow()) : "(none)") + "\n";
                if (!t->get().isNull()) {
                    const auto& v = t->get();
                    out += "  value:     " + varPreview(v) + "\n";
                    out += "  type:      " + std::string(varTypeName(v.type())) + "\n";
                } else { out += "  value:     (none)\n"; }
                out += "  watching:  " + std::string(t->isWatching() ? "yes" : "no") + "\n";
                out += "  silent:    " + std::string(t->isSilent() ? "yes" : "no") + "\n";
                s.print(out); return;
            }
            int total = t->count();
            if (total == 0) { s.print("  (empty)\n"); return; }
            std::string out;
            for (int i = 0; i < total; ++i) {
                auto* c = t->child(i);
                auto nm = c->name().empty() ? "(anon)" : c->name();
                out += "  [" + std::to_string(i) + "] " + nm;
                auto k = t->keyOf(c);
                if (k != nm && k != "(anon)") out += "  (key: " + k + ")";
                if (!c->get().isNull()) out += "  = " + varPreview(c->get());
                out += "\n";
            }
            out += "  (" + std::to_string(total) + " total)\n";
            s.print(out);
        }, "ls [path] [-t] [-l] [-n]");

        auto getImpl = [](S& s, Args args) {
            auto f = parseFlags(args);
            const int pc = f.posCount();
            Node* t = nullptr;
            if (pc == 0) {
                t = s.cur;
            } else if (pc == 1) {
                t = s.resolve(f.pos(0));
                if (!t) { s.print("not found: " + f.pos(0) + "\n"); return; }
            } else {
                s.print("usage: get [path] [-t]\n");
                return;
            }
            if (f.has("type", 't')) {
                s.print(t->get().isNull() ? "(none)\n" : std::string(varTypeName(t->get().type())) + "\n");
                return;
            }
            if (t->get().isNull()) { s.print("(none)\n"); return; }
            const auto& v = t->get();
            s.print(varPreview(v, 256) + "  (" + varTypeName(v.type()) + ")\n");
        };
        regBuiltin(builtins, "get", getImpl, "get [path] [-t]");
        regBuiltin(builtins, "g", getImpl, "get [path] [-t]");

        auto setImpl = [](S& s, Args args) {
            auto f = parseFlags(args);
            const int pc = f.posCount();

            if (f.has("null")) {
                Node* t = nullptr;
                if (pc == 0) {
                    t = s.cur;
                } else if (pc == 1) {
                    t = s.resolve(f.pos(0));
                    if (!t) { s.print("not found: " + f.pos(0) + "\n"); return; }
                } else {
                    s.print("usage: set [path] --null\n");
                    return;
                }
                t->set(Var());
                s.print("value cleared\n");
                return;
            }

            if (f.has("trigger", 't')) {
                Node* t = nullptr;
                if (pc == 0) {
                    t = s.cur;
                } else if (pc == 1) {
                    t = s.resolve(f.pos(0));
                    if (!t) { s.print("not found: " + f.pos(0) + "\n"); return; }
                } else {
                    s.print("usage: set [path] --trigger\n");
                    return;
                }
                t->trigger<Node::NODE_CHANGED>();
                if (t->isWatching()) t->activate(Node::NODE_CHANGED, t);
                s.print("triggered: " + nodeSummary(t) + "\n");
                return;
            }

            Node* t = nullptr;
            std::string valueRaw;

            if (pc == 0) {
                t = s.cur;
                valueRaw = "";
            } else if (pc == 1) {
                t = s.cur;
                valueRaw = f.pos(0);
            } else if (pc == 2) {
                t = s.resolve(f.pos(0));
                if (!t) { s.print("not found: " + f.pos(0) + "\n"); return; }
                valueRaw = f.pos(1);
            } else {
                s.print("usage: set [path] [value] [--null] [--trigger/-t]\n");
                return;
            }

            Var v = parseVar(valueRaw);
            t->set(std::move(v));
            s.print("set: " + varPreview(t->get()) + "  (" + varTypeName(t->get().type()) + ")\n");
        };
        regBuiltin(builtins, "set", setImpl, "set [path] [value]");
        regBuiltin(builtins, "s", setImpl, "set [path] [value]");

        regBuiltin(builtins, "mk", [](S& s, Args args) {
            auto f = parseFlags(args);
            auto name = f.get("name", 'n');
            if (!name.empty() || f.has("anon", 'a')) {
                auto* t = s.target(args);
                auto ovStr = f.get("overlap", 'o', "0");
                int overlap = isInt(ovStr) ? std::stoi(ovStr) : 0;
                auto atStr = f.get("at");
                bool hasAt = f.has("at") && isInt(atStr);
                if (f.has("anon", 'a')) {
                    auto* n = t->append(overlap);
                    if (n) s.print("appended " + std::to_string(1 + overlap) + " anon -> index: " + std::to_string(t->indexOf(n)) + "\n");
                    else s.print("failed\n");
                    return;
                }
                if (hasAt) {
                    int idx = std::stoi(atStr);
                    auto* c = new Node(name);
                    if (t->insert(c, idx)) s.print("inserted '" + name + "' at [" + std::to_string(idx) + "]\n");
                    else { delete c; s.print("insert failed\n"); }
                    return;
                }
                auto* n = t->append(name, overlap);
                if (n) s.print("appended " + std::to_string(1 + overlap) + " '" + name + "' -> last: " + t->keyOf(n) + "\n");
                else s.print("failed\n");
                return;
            }
            auto path = f.pos(0);
            if (path.empty()) { s.print("usage: mk <path> | mk -n <name> [-o N] [-a] [--at IDX]\n"); return; }
            bool absolute = path[0] == '/';
            Node* base = absolute ? s.root : s.cur;
            std::string relPath = absolute ? path.substr(1) : path;
            auto* n = base->at(relPath);
            if (n) s.print("created: /" + n->path(s.root) + "\n");
            else s.print("failed\n");
        }, "mk <path> | mk -n <name>");

        regBuiltin(builtins, "rm", [](S& s, Args args) {
            auto f = parseFlags(args);
            auto* t = s.target(args);
            if (f.has("clear", 'c')) {
                int n = t->count();
                t->clear();
                s.print("cleared " + std::to_string(n) + " children\n");
                return;
            }
            if (f.has("index", 'i')) {
                auto idxStr = f.get("index", 'i');
                if (!isInt(idxStr)) { s.print("usage: rm -i <index>\n"); return; }
                int idx = std::stoi(idxStr);
                if (t->remove(idx)) s.print("removed [" + std::to_string(idx) + "]\n");
                else s.print("no child at index " + std::to_string(idx) + "\n");
                return;
            }
            if (f.has("name", 'n')) {
                auto nm = f.get("name", 'n');
                if (nm.empty()) { s.print("usage: rm -n <name> [-o N]\n"); return; }
                auto ovStr = f.get("overlap", 'o', "0");
                int overlap = isInt(ovStr) ? std::stoi(ovStr) : 0;
                if (t->remove(nm, overlap)) s.print("removed '" + nm + "'\n");
                else s.print("no child '" + nm + "' overlap " + std::to_string(overlap) + "\n");
                return;
            }
            if (f.has("all")) {
                auto nm = f.get("all");
                if (nm.empty()) { s.print("usage: rm --all <name>\n"); return; }
                int n = t->remove(nm);
                s.print("removed " + std::to_string(n) + " children named '" + nm + "'\n");
                return;
            }
            auto childPath = f.pos(0);
            if (!childPath.empty()) {
                bool absolute = childPath[0] == '/';
                Node* base = absolute ? s.root : s.cur;
                std::string relPath = absolute ? childPath.substr(1) : childPath;
                if (base->erase(relPath)) s.print("removed\n");
                else s.print("failed (not found or is root)\n");
                return;
            }
            s.print("usage: rm [path] <target> | rm -i IDX | rm -n NAME [-o N] | rm --all NAME | rm -c\n");
        }, "rm [path]");

        regBuiltin(builtins, "mv", [](S& s, Args args) {
            auto f = parseFlags(args);
            auto srcPath = f.pos(0);
            if (srcPath.empty()) { s.print("usage: mv <src> [dest] [--at INDEX]\n"); return; }
            auto* src = s.resolve(srcPath);
            if (!src) { s.print("not found: " + srcPath + "\n"); return; }
            auto destPath = f.pos(1);
            auto* dest = destPath.empty() ? s.cur : s.resolve(destPath);
            if (!dest) { s.print("dest not found: " + destPath + "\n"); return; }
            if (src == dest) { s.print("cannot move node into itself\n"); return; }
            auto atStr = f.get("at");
            if (f.has("at") && isInt(atStr)) {
                int idx = std::stoi(atStr);
                if (dest->insert(src, idx)) s.print("moved to [" + std::to_string(idx) + "] " + src->path(s.root) + "\n");
                else s.print("insert failed\n");
                return;
            }
            dest->insert(src);
            s.print("moved to " + src->path(s.root) + "\n");
        }, "mv <src> [dest]");

        regBuiltin(builtins, "cp", [](S& s, Args args) {
            auto f = parseFlags(args);
            auto srcPath = f.pos(0);
            if (srcPath.empty()) { s.print("usage: cp <src> [dest] [-r] [-u] [-I]\n"); return; }
            auto* src = s.resolve(srcPath);
            if (!src) { s.print("not found: " + srcPath + "\n"); return; }
            auto destPath = f.pos(1);
            auto* dest = destPath.empty() ? s.cur : s.resolve(destPath);
            if (!dest) { s.print("dest not found: " + destPath + "\n"); return; }
            if (src == dest) { s.print("cannot copy node onto itself\n"); return; }
            if (src->isAncestorOf(dest) || dest->isAncestorOf(src)) {
                s.print("cannot copy between overlapping subtrees\n"); return;
            }
            bool ai = !f.has("no-insert", 'I'), ar = f.has("remove", 'r'), au = f.has("update", 'u');
            dest->copy(src, ai, ar, au);
            s.print("copied /" + src->path(s.root) + " -> /" + dest->path(s.root)
                + "  (insert:" + std::string(ai?"on":"off") + ", remove:" + std::string(ar?"on":"off")
                + ", update:" + std::string(au?"on":"off") + ")\n");
        }, "cp <src> [dest]");

        regBuiltin(builtins, "schema", [](S& s, Args args) {
            auto f = parseFlags(args);
            auto format = f.pos(0);
            if (format.empty()) {
                s.print(availableSchemaFormatsText() + "\n"); return;
            }
            auto pathStr = f.pos(1);
            auto* t = pathStr.empty() ? s.cur : s.resolve(pathStr);
            if (!t) { s.print("not found: " + pathStr + "\n"); return; }

            bool isImport = f.has("import", 'i');
            auto file = f.get("file", 'f');
            auto importContent = f.get("import", 'i');

            if (isImport) {
                std::string content;
                if (!file.empty()) {
                    std::ifstream ifs(file, format == "bin" ? std::ios::binary : std::ios::in);
                    if (!ifs.is_open()) { s.print("cannot read: " + file + "\n"); return; }
                    content.assign((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
                } else if (!importContent.empty()) {
                    content = importContent;
                } else {
                    s.print("usage: schema " + format + " -i <content> | -i -f <file>\n");
                    return;
                }

                bool ok = schema::importSchemaFormat(format, t, content);
                if (ok) s.print(file.empty() ? "imported\n" : "imported from " + file + "\n");
                else s.print("import failed (invalid " + format + ")\n");
                return;
            }

            std::string result = schema::exportSchemaFormat(format, t);
            if (result.empty()) {
                s.print("unknown format: " + format + "\n");
                return;
            }

            if (format == "bin") {
                if (!file.empty()) {
                    auto bytes = impl::bin::exportTree(t);
                    std::ofstream ofs(file, std::ios::binary);
                    if (!ofs.is_open()) { s.print("cannot write: " + file + "\n"); return; }
                    ofs.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
                    s.print("saved to " + file + " (" + std::to_string(bytes.size()) + " bytes)\n");
                    return;
                }
                std::ostringstream oss;
                for (size_t i = 0; i < result.size(); i += 2) {
                    if (i > 0 && (i/2) % 16 == 0) oss << "\n";
                    else if (i > 0) oss << " ";
                    oss << result.substr(i, 2);
                }
                oss << "\n(" << std::dec << (result.size()/2) << " bytes)\n";
                s.print(oss.str());
                return;
            }

            if (!file.empty()) {
                std::ofstream ofs(file);
                if (!ofs.is_open()) { s.print("cannot write: " + file + "\n"); return; }
                ofs << result;
                s.print("saved to " + file + "\n");
            } else {
                s.print(result);
            }
        }, "schema <fmt> [path]");

        regBuiltin(builtins, "shadow", [](S& s, Args args) {
            auto f = parseFlags(args);
            auto* t = s.target(args);
            if (f.has("clear")) { t->setShadow(nullptr); s.print("shadow cleared\n"); return; }
            auto setPath = f.get("set");
            if (f.has("set")) {
                if (setPath.empty()) { s.print("usage: shadow --set <path>\n"); return; }
                auto* sh = s.resolve(setPath);
                if (!sh) { s.print("not found: " + setPath + "\n"); return; }
                t->setShadow(sh);
                s.print("shadow set to: " + sh->path(s.root) + "\n"); return;
            }
            auto* sh = t->shadow();
            s.print(sh ? "shadow: " + nodeSummary(sh) + " (/" + sh->path(s.root) + ")\n" : "(no shadow)\n");
        }, "shadow [path]");
    });
}



struct TerminalSession::Private
{
    using CmdFn = std::function<void(Private&, const std::vector<std::string>&)>;

    Node* root = nullptr;
    Node* cur  = nullptr;
    std::vector<std::string> history;
    std::vector<Node*> orphans;
    std::string output;
    std::unordered_map<std::string, CmdFn> cmds;
    TerminalSession::AsyncOutputFn asyncOutput;
    TerminalSession::Options opts;

    std::string currentPath() const { return cur->path(root); }

    Node* resolve(const std::string& path) const {
        if (path.empty() || path == ".") return cur;
        if (path == "..") return cur->parent() ? cur->parent() : cur;
        bool absolute = path[0] == '/';
        Node* base = absolute ? root : cur;
        std::string relPath = absolute ? path.substr(1) : path;
        if (relPath.empty()) return base;
        return base->find(relPath);
    }

    Node* target(const std::vector<std::string>& args, int argIdx = 1) const {
        if ((int)args.size() > argIdx && !args[argIdx].empty() && args[argIdx][0] != '-') {
            auto* n = resolve(args[argIdx]);
            if (n) return n;
        }
        return cur;
    }

    void print(const std::string& text) { output += text; }
    void initCommands();

    ~Private() { for (auto* o : orphans) delete o; }
};

// ============================================================================
// initCommands
// ============================================================================

void TerminalSession::Private::initCommands()
{
    using S = Private;
    using Args = const std::vector<std::string>&;

    registerTerminalBuiltins();

    cmds["take"] = [](S& s, Args args) {
        if (args.size() < 2) { s.print("usage: take <index|path>\n"); return; }
        Node* taken = nullptr;
        if (isInt(args[1])) {
            taken = s.cur->take(std::stoi(args[1]));
        } else {
            auto* c = s.cur->find(args[1], false);
            if (c && c->parent() == s.cur) taken = s.cur->take(c);
            else if (c) s.print("not a direct child\n");
            else s.print("not found: " + args[1] + "\n");
        }
        if (taken) {
            s.orphans.push_back(taken);
            s.print("taken: " + nodeSummary(taken) + " -> orphan pool [" + std::to_string(s.orphans.size() - 1) + "]\n");
        } else if (isInt(args[1])) {
            s.print("no child at index " + args[1] + "\n");
        }
    };

    cmds["orphans"] = [](S& s, Args) {
        if (s.orphans.empty()) { s.print("(empty)\n"); return; }
        std::string out;
        for (size_t i = 0; i < s.orphans.size(); ++i)
            out += "  [" + std::to_string(i) + "] " + nodeSummary(s.orphans[i]) +
                   " (" + std::to_string(s.orphans[i]->count()) + " children)\n";
        s.print(out);
    };

    cmds["adopt"] = [](S& s, Args args) {
        if (args.size() < 2) { s.print("usage: adopt <orphan_index>\n"); return; }
        int idx = std::stoi(args[1]);
        if (idx < 0 || idx >= (int)s.orphans.size()) { s.print("invalid orphan index\n"); return; }
        auto* n = s.orphans[idx];
        s.orphans.erase(s.orphans.begin() + idx);
        s.cur->insert(n);
        s.print("adopted: " + n->path(s.root) + "\n");
    };

    cmds["history"] = [](S& s, Args) {
        for (size_t i = 0; i < s.history.size(); ++i) {
            s.print(std::to_string(i) + "  " + s.history[i] + "\n");
        }
    };

    cmds["help"] = [](S& s, Args args) {
        auto f = parseFlags(args);
        auto specific = f.pos(0);
        if (!specific.empty()) {
            std::string key = specific;
            for (int pi = 1; ; ++pi) {
                auto w = f.pos(pi);
                if (w.empty()) break;
                key += "." + w;
            }
            Node* builtin = nullptr;
            if (const Node* root = factory::at("builtin").node()) {
                builtin = const_cast<Node*>(root)->find(key, false);
            }
            if (builtin) {
                auto h = builtin->get("help").toString();
                s.print(h.empty() ? key + "\n" : key + ": " + h + "\n");
                return;
            }
            auto h = command::help(key);
            s.print(h.empty() ? "unknown command: " + key + "\n" : key + ": " + h + "\n");
            return;
        }

        std::string out;
        out += "=== Builtin Commands ===\n";
        auto builtinKeys = factory::at("builtin").keys();
        std::sort(builtinKeys.begin(), builtinKeys.end());
        for (auto& k : builtinKeys) {
            Node* builtin = factory::at("builtin").node(k);
            out += "  " + k;
            auto h = builtin ? builtin->get("help").toString() : std::string{};
            if (!h.empty()) { int pad = 18 - (int)k.size(); out += std::string(pad > 0 ? pad : 2, ' ') + h; }
            out += "\n";
        }
        out += "\n=== Session Commands ===\n";
        out += "  take <idx|path>\n  orphans\n  adopt <N>\n  history\n  quit / exit\n";

        auto userCmds = command::keys();
        if (!userCmds.empty()) {
            std::sort(userCmds.begin(), userCmds.end());
            out += "\n=== User Commands ===\n";
            for (auto& k : userCmds) {
                auto h = command::help(k);
                out += "  " + k;
                if (!h.empty()) { int pad = 18 - (int)k.size(); out += std::string(pad > 0 ? pad : 2, ' ') + h; }
                out += "\n";
            }
        }
        s.print(out);
    };
}

// ============================================================================
// TerminalSession
// ============================================================================

TerminalSession::TerminalSession(Node* root, const Options& opts)
    : _p(std::make_unique<Private>())
{
    _p->root = root;
    _p->cur  = root;
    _p->opts = opts;
    _p->initCommands();
}

TerminalSession::~TerminalSession() = default;

void TerminalSession::setAsyncOutput(AsyncOutputFn fn)
{
    _p->asyncOutput = std::move(fn);
}

std::string TerminalSession::execute(const std::string& line)
{
    _p->output.clear();
    if (line.empty()) return {};

    auto args = split(line);
    if (args.empty()) return {};

    _p->history.push_back(line);
    std::string cmd = args[0];
    auto& s = *_p;

    if (cmd == "quit" || cmd == "exit")
        return "\x04";

    auto it = s.cmds.find(cmd);
    if (it != s.cmds.end()) {
        it->second(s, args);
        return s.output;
    }

    // Fallback: user-registered global commands
    // "async cmd ..." runs with wait=false; otherwise wait=true (default)
    bool asyncMode = false;
    if (cmd == "async" && args.size() > 1) {
        asyncMode = true;
        args.erase(args.begin());
        cmd = args[0];
    }

    // Try multi-word command: "ros topic once" -> "ros.topic.once"
    // Always find the longest match (most words consumed).
    auto [builtinNode, builtinWordCount] = resolveFactoryCommand(factory::at("builtin"), args);
    auto [cmdNode, cmdWordCount] = resolveFactoryCommand(command::factory(), args);
    Factory& resolvedFactory = builtinNode ? factory::at("builtin") : command::factory();
    std::string resolvedName;
    for (size_t i = 0; i < (builtinNode ? builtinWordCount : cmdWordCount); ++i) {
        if (i > 0) resolvedName += ".";
        resolvedName += args[i];
    }
    size_t resolvedWordCount = builtinNode ? builtinWordCount : cmdWordCount;

    if (builtinNode || cmdNode) {
        if (asyncMode) {
            auto* detached = new Pipeline(resolvedName);
            Node* request = detached->context()->at("request");
            Node* reply = detached->context()->at("reply");
            prepareCommandInput(request, s.root, s.cur, args, resolvedWordCount);
            Command cmdObj = command::create(resolvedFactory, resolvedName,
                                             detached->context(), request, reply);

            auto asyncOut = s.asyncOutput;
            detached->addCommand(cmdObj);
            detached->onFinished([asyncOut, detached, resolvedName](const Result& res) {
                std::string text = renderCommandOutput(detached->context()->find("reply", false), res);
                if (asyncOut && !text.empty()) {
                    if (text.back() != '\n') {
                        text.push_back('\n');
                    }
                    asyncOut("\x1b[33m[" + resolvedName + "]\x1b[0m " + text);
                }
                delete detached;
            });
            detached->start();
            s.print("accepted\n");
            return s.output;
        }

        Pipeline pipe(resolvedName);
        Node* request = pipe.context()->at("request");
        Node* reply = pipe.context()->at("reply");
        prepareCommandInput(request, s.root, s.cur, args, resolvedWordCount);
        Command cmdObj = command::create(resolvedFactory, resolvedName, pipe.context(), request, reply);
        pipe.addCommand(cmdObj);
        pipe.start();
        pipe.wait();
        updateCurrentFromOut(s.cur, reply);
        s.print(renderCommandOutput(reply, pipe.lastResult()));
        return s.output;
    }

    s.print("unknown: " + cmd + "  (type 'help')\n");
    return s.output;
}

std::string TerminalSession::prompt() const
{
    bool use_color = _p->opts.prompt_color;
    std::string path_color = "\x1b[36m";
    std::string prompt_color = "\x1b[32m";

    // Only read config if opts allow color
    if (_p->opts.prompt_color) {
        if (Node* cfg = _p->root->find("ve/server/terminal/repl/config")) {
            use_color = cfg->get("prompt_color").toBool(true);
            auto pc = cfg->get("prompt_path_color").toString();
            if (!pc.empty()) path_color = pc;
            auto sc = cfg->get("prompt_symbol_color").toString();
            if (!sc.empty()) prompt_color = sc;
        }
    }

    if (!use_color) {
        if (_p->cur == _p->root) return "/> ";
        return "/" + _p->cur->path(_p->root) + "> ";
    }

    if (_p->cur == _p->root) {
        return path_color + "/" + "\x1b[0m" + prompt_color + "> \x1b[0m";
    }
    return path_color + "/" + _p->cur->path(_p->root) + "\x1b[0m" + prompt_color + "> \x1b[0m";
}

std::vector<std::string> TerminalSession::complete(const std::string& partial)
{
    auto tokens = split(partial);
    bool endsWithSpace = !partial.empty() && std::isspace(static_cast<unsigned char>(partial.back()));

    // --- Determine command boundary ---
    auto [builtinNode, builtinWords] = resolveFactoryCommand(factory::at("builtin"), tokens);
    auto [cmdNode, cmdWords] = resolveFactoryCommand(command::factory(), tokens);
    size_t matchedWords = builtinNode ? builtinWords : cmdWords;

    // A built-in (session-local) command is always a single word.
    bool firstIsBuiltin = !tokens.empty() &&
                          (_p->cmds.count(tokens[0]) || tokens[0] == "quit" || tokens[0] == "exit");

    // We are past the command name when:
    //   - a registered command matched AND (there's a trailing space OR extra tokens after it)
    //   - OR the first token is a built-in AND (trailing space OR more tokens follow)
    bool pastCmd = ((builtinNode || cmdNode) && (endsWithSpace || tokens.size() > matchedWords)) ||
                   (firstIsBuiltin   && (endsWithSpace || tokens.size() > 1));

    if (pastCmd) {
        // Complete the current argument token as a node path.
        size_t cmdWordCount = (builtinNode || cmdNode) ? matchedWords : 1; // local command = 1 word
        std::string prefix;
        if (endsWithSpace)
            prefix = "";
        else if (tokens.size() > cmdWordCount)
            prefix = tokens.back();
        return completeNodePath(_p->root, _p->cur, prefix);
    }

    // --- Complete the command name ---
    // Build the typed prefix in space form (what the user has typed so far).
    // e.g. tokens=["ros","top"], no trailing space -> prefix = "ros top"
    // e.g. tokens=["ros"],       trailing space    -> prefix = "ros "
    std::string typedPrefix;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (i > 0) typedPrefix += ' ';
        typedPrefix += tokens[i];
    }
    if (endsWithSpace && !tokens.empty()) typedPrefix += ' ';

    std::vector<std::string> matches;

    // Built-in single-word commands (only relevant when no space in prefix yet)
    if (typedPrefix.find(' ') == std::string::npos) {
        for (auto& [name, _] : _p->cmds)
            if (name.compare(0, typedPrefix.size(), typedPrefix) == 0)
                matches.push_back(name);
        for (auto* extra : {"quit", "exit"})
            if (std::string(extra).compare(0, typedPrefix.size(), typedPrefix) == 0)
                matches.push_back(extra);
        for (auto& key : factory::at("builtin").keys())
            if (key.compare(0, typedPrefix.size(), typedPrefix) == 0)
                matches.push_back(key);
    }

    // Registered multi-word commands: keys use "." internally, display with spaces.
    for (auto& key : command::keys()) {
        std::string keySpace = key;
        std::replace(keySpace.begin(), keySpace.end(), '.', ' ');

        if (keySpace.size() < typedPrefix.size()) continue;
        if (keySpace.compare(0, typedPrefix.size(), typedPrefix) != 0) continue;

        // Return completion up to (but not including) the next space boundary after the prefix.
        size_t next = keySpace.find(' ', typedPrefix.size());
        matches.push_back(next == std::string::npos ? keySpace : keySpace.substr(0, next));
    }

    std::sort(matches.begin(), matches.end());
    matches.erase(std::unique(matches.begin(), matches.end()), matches.end());
    return matches;
}

const std::vector<std::string>& TerminalSession::history() const { return _p->history; }

Node* TerminalSession::root() const { return _p->root; }
Node* TerminalSession::current() const { return _p->cur; }

} // namespace service
} // namespace ve
