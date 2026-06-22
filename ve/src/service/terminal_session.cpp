// terminal_session.cpp — TerminalSession REPL logic
//
// All commands (builtins + session) registered in factory::at("service/repl").
// User commands fall through to command::factory().
// Commands receive TerminalSession* via ctx->get("_session").

#include "terminal_session.h"
#include "ve/core/command.h"
#include "ve/core/pipeline.h"
#include "ve/core/impl/json.h"
#include "ve/core/impl/bin.h"
#include "ve/core/impl/xml.h"
#include "ve/core/impl/md.h"
#include "ve/core/schema.h"
#include "ve/core/res.h"
#include "terminal_util.h"

#include <sstream>
#include <iomanip>
#include <fstream>
#include <algorithm>
#include <functional>
#include <unordered_map>
#include <unordered_set>

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
            base = rel.empty() ? root : root->find(rel);
        } else {
            base = cur->find(parentPath);
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
            if (n->get().isCallable()) return {n, i};
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
    Node* argv = in->at("argv");
    for (const auto& arg : args) {
        argv->append()->set(arg);
    }
    in->set("argc", static_cast<int64_t>(args.size()));
    in->set("command_words", static_cast<int64_t>(start));
    if (root) in->at("root")->set(Var::ptr(root));
    if (current) in->at("current")->set(Var::ptr(current));
}

static void updateCurrentFromOut(Node*& current, Node* out)
{
    if (!out) return;
    if (auto* cn = out->find("current")) {
        if (auto* p = cn->get().toPointer()) {
            current = static_cast<Node*>(p);
        }
    }
}

static std::string renderCommandOutput(Node* out, const Result& r)
{
    if (r.isError()) {
        return r.message().empty() ? std::string("command failed\n") : r.message() + "\n";
    }
    if (out) {
        if (auto* text = out->find("text")) {
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

// ============================================================================
// Repl command helpers
// ============================================================================

static TerminalSession* sess(Node* ctx)
{
    return static_cast<TerminalSession*>(ctx->get("_session").as<Session*>());
}

static Node* resolveNode(Node* root, Node* cur, const std::string& path)
{
    if (path.empty() || path == ".") return cur;
    bool absolute = path[0] == '/';
    Node* base = absolute ? root : cur;
    std::string relPath = absolute ? path.substr(1) : path;
    if (relPath.empty()) return base;
    return base->find(relPath);
}

static std::vector<std::string> argvFromInput(Node* in)
{
    std::vector<std::string> args;
    if (!in) return args;
    if (auto* argv = in->find("argv")) {
        for (auto* arg : *argv)
            args.push_back(arg->getString());
    }
    return args;
}

static Node* targetFromArgs(Node* root, Node* cur, const std::vector<std::string>& args, int argIdx = 1)
{
    if ((int)args.size() > argIdx && !args[argIdx].empty() && args[argIdx][0] != '-') {
        auto* n = resolveNode(root, cur, args[argIdx]);
        if (n) return n;
    }
    return cur;
}

static void setCurrentOut(Node* out, Node* cur, Node* root)
{
    if (!out || !cur) return;
    out->at("current")->set(Var::ptr(cur));
    if (root) out->at("path")->set(cur->path(root));
}

static void setTextOut(Node* out, const std::string& text)
{
    if (out && !text.empty()) out->at("text")->set(text);
}

// ============================================================================
// repl:: command implementations — Proc(ctx, in, out)
// ============================================================================

namespace repl {

static Result cd(Node* ctx, Node* in, Node* out)
{
    auto* s = sess(ctx);
    auto args = argvFromInput(in);
    if (args.size() < 2) return Result::fail("usage: cd <path>");
    auto& path = args[1];
    if (path == "..") {
        s->current = s->current->parent() ? s->current->parent() : s->current;
        setCurrentOut(out, s->current, s->root);
        return Result::ok();
    }
    auto* n = resolveNode(s->root, s->current, path);
    if (!n) return Result::fail("not found: " + path);
    s->current = n;
    setCurrentOut(out, s->current, s->root);
    return Result::ok();
}

static Result pwd(Node* ctx, Node*, Node* out)
{
    auto* s = sess(ctx);
    setTextOut(out, "/" + s->current->path(s->root) + "\n");
    return Result::ok();
}

static Result root(Node* ctx, Node*, Node* out)
{
    auto* s = sess(ctx);
    s->current = s->root;
    setCurrentOut(out, s->current, s->root);
    return Result::ok();
}

static Result up(Node* ctx, Node* in, Node* out)
{
    auto* s = sess(ctx);
    auto args = argvFromInput(in);
    int n = (args.size() > 1 && isInt(args[1])) ? std::stoi(args[1]) : 1;
    auto* p = s->current->parent(n - 1);
    if (!p) return Result::fail("cannot go up " + std::to_string(n) + " levels");
    s->current = p;
    setCurrentOut(out, s->current, s->root);
    return Result::ok();
}

static Result first(Node* ctx, Node*, Node* out)
{
    auto* s = sess(ctx);
    auto* f = s->current->first();
    if (!f) { setTextOut(out, "(no children)\n"); return Result::ok(); }
    s->current = f;
    setTextOut(out, "-> " + nodeSummary(f) + "\n");
    setCurrentOut(out, s->current, s->root);
    return Result::ok();
}

static Result last(Node* ctx, Node*, Node* out)
{
    auto* s = sess(ctx);
    auto* l = s->current->last();
    if (!l) { setTextOut(out, "(no children)\n"); return Result::ok(); }
    s->current = l;
    setTextOut(out, "-> " + nodeSummary(l) + "\n");
    setCurrentOut(out, s->current, s->root);
    return Result::ok();
}

static Result prev(Node* ctx, Node*, Node* out)
{
    auto* s = sess(ctx);
    auto* p = s->current->prev();
    if (!p) { setTextOut(out, "(no prev sibling)\n"); return Result::ok(); }
    s->current = p;
    setTextOut(out, "-> " + nodeSummary(p) + "\n");
    setCurrentOut(out, s->current, s->root);
    return Result::ok();
}

static Result next(Node* ctx, Node*, Node* out)
{
    auto* s = sess(ctx);
    auto* n = s->current->next();
    if (!n) { setTextOut(out, "(no next sibling)\n"); return Result::ok(); }
    s->current = n;
    setTextOut(out, "-> " + nodeSummary(n) + "\n");
    setCurrentOut(out, s->current, s->root);
    return Result::ok();
}

static Result sibling(Node* ctx, Node* in, Node* out)
{
    auto* s = sess(ctx);
    auto args = argvFromInput(in);
    if (args.size() < 2) return Result::fail("usage: sibling <offset>");
    int off = std::stoi(args[1]);
    auto* sib = s->current->sibling(off);
    if (!sib) return Result::fail("no sibling at offset " + std::to_string(off));
    s->current = sib;
    setTextOut(out, "-> " + nodeSummary(sib) + "\n");
    setCurrentOut(out, s->current, s->root);
    return Result::ok();
}

static Result ls(Node* ctx, Node* in, Node* out)
{
    auto* s = sess(ctx);
    auto args = argvFromInput(in);
    auto f = parseFlags(args);
    const int lp = f.posCount();
    Node* t = nullptr;
    if (lp == 0) {
        t = s->current;
    } else if (lp == 1) {
        t = resolveNode(s->root, s->current, f.pos(0));
        if (!t) return Result::fail("not found: " + f.pos(0));
    } else {
        return Result::fail("usage: ls [path] [-t] [-l] [-n]");
    }

    std::string text;
    if (f.has("tree", 't')) { text = t->dump(); setTextOut(out, text); return Result::ok(); }
    if (f.has("names", 'n')) {
        auto names = t->childNames();
        int anonCnt = 0;
        for (auto* c : *t) if (c->name().empty()) ++anonCnt;
        if (anonCnt > 0) text += "  (anon) x" + std::to_string(anonCnt) + "\n";
        for (auto& nm : names) text += "  " + nm + "\n";
        setTextOut(out, text); return Result::ok();
    }
    if (f.has("long", 'l')) {
        auto nm = t->name().empty() ? "(anon)" : t->name();
        text += "  name:      " + nm + "\n";
        text += "  path:      /" + t->path(s->root) + "\n";
        text += "  parent:    " + std::string(t->parent() ? nodeSummary(t->parent()) : "(none)") + "\n";
        text += "  children:  " + std::to_string(t->count()) + "\n";
        text += "  empty:     " + std::string(t->empty() ? "yes" : "no") + "\n";
        if (!t->get().isNull()) {
            const auto& v = t->get();
            text += "  value:     " + varPreview(v) + "\n";
            text += "  type:      " + std::string(varTypeName(v.type())) + "\n";
        } else { text += "  value:     (none)\n"; }
        text += "  watching:  " + std::string(t->isWatching() ? "yes" : "no") + "\n";
        text += "  silent:    " + std::string(t->isSilent() ? "yes" : "no") + "\n";
        setTextOut(out, text); return Result::ok();
    }

    int total = t->count();
    if (total == 0) { setTextOut(out, "  (empty)\n"); return Result::ok(); }
    for (int i = 0; i < total; ++i) {
        auto* c = t->child(i);
        auto nm = c->name().empty() ? "(anon)" : c->name();
        text += "  [" + std::to_string(i) + "] " + nm;
        auto k = t->keyOf(c);
        if (k != nm && k != "(anon)") text += "  (key: " + k + ")";
        if (!c->get().isNull()) text += "  = " + varPreview(c->get());
        text += "\n";
    }
    text += "  (" + std::to_string(total) + " total)\n";
    setTextOut(out, text);
    return Result::ok();
}

static Result get(Node* ctx, Node* in, Node* out)
{
    auto* s = sess(ctx);
    auto args = argvFromInput(in);
    auto f = parseFlags(args);
    const int pc = f.posCount();
    Node* t = nullptr;
    if (pc == 0) {
        t = s->current;
    } else if (pc == 1) {
        t = resolveNode(s->root, s->current, f.pos(0));
        if (!t) return Result::fail("not found: " + f.pos(0));
    } else {
        return Result::fail("usage: get [path] [-t]");
    }
    if (f.has("type", 't')) {
        setTextOut(out, t->get().isNull() ? "(none)\n" : std::string(varTypeName(t->get().type())) + "\n");
        return Result::ok();
    }
    if (t->get().isNull()) { setTextOut(out, "(none)\n"); return Result::ok(); }
    const auto& v = t->get();
    setTextOut(out, varPreview(v, 256) + "  (" + varTypeName(v.type()) + ")\n");
    return Result::ok();
}

static Result set(Node* ctx, Node* in, Node* out)
{
    auto* s = sess(ctx);
    auto args = argvFromInput(in);
    auto f = parseFlags(args);
    const int pc = f.posCount();

    if (f.has("null")) {
        Node* t = nullptr;
        if (pc == 0) t = s->current;
        else if (pc == 1) {
            t = resolveNode(s->root, s->current, f.pos(0));
            if (!t) return Result::fail("not found: " + f.pos(0));
        } else return Result::fail("usage: set [path] --null");
        t->set(Var());
        setTextOut(out, "value cleared\n");
        return Result::ok();
    }

    if (f.has("trigger", 't')) {
        Node* t = nullptr;
        if (pc == 0) t = s->current;
        else if (pc == 1) {
            t = resolveNode(s->root, s->current, f.pos(0));
            if (!t) return Result::fail("not found: " + f.pos(0));
        } else return Result::fail("usage: set [path] --trigger");
        t->trigger<Node::NODE_CHANGED>();
        if (t->isWatching()) t->activate(Node::NODE_CHANGED, t);
        setTextOut(out, "triggered: " + nodeSummary(t) + "\n");
        return Result::ok();
    }

    Node* t = nullptr;
    std::string valueRaw;
    if (pc == 0) {
        t = s->current;
        valueRaw = "";
    } else if (pc == 1) {
        t = s->current;
        valueRaw = f.pos(0);
    } else if (pc == 2) {
        t = resolveNode(s->root, s->current, f.pos(0));
        if (!t) return Result::fail("not found: " + f.pos(0));
        valueRaw = f.pos(1);
    } else {
        return Result::fail("usage: set [path] [value] [--null] [--trigger/-t]");
    }

    Var v = parseVar(valueRaw);
    t->set(std::move(v));
    setTextOut(out, "set: " + varPreview(t->get()) + "  (" + varTypeName(t->get().type()) + ")\n");
    return Result::ok();
}

static Result mk(Node* ctx, Node* in, Node* out)
{
    auto* s = sess(ctx);
    auto args = argvFromInput(in);
    auto f = parseFlags(args);
    auto name = f.get("name", 'n');
    if (!name.empty() || f.has("anon", 'a')) {
        auto* t = targetFromArgs(s->root, s->current, args);
        auto ovStr = f.get("overlap", 'o', "0");
        int overlap = isInt(ovStr) ? std::stoi(ovStr) : 0;
        auto atStr = f.get("at");
        bool hasAt = f.has("at") && isInt(atStr);
        if (f.has("anon", 'a')) {
            auto* n = t->append(overlap);
            if (n) setTextOut(out, "appended " + std::to_string(1 + overlap) + " anon -> index: " + std::to_string(t->indexOf(n)) + "\n");
            else return Result::fail("append failed");
            return Result::ok();
        }
        if (hasAt) {
            int idx = std::stoi(atStr);
            auto* c = new Node(name);
            if (t->insert(c, idx)) setTextOut(out, "inserted '" + name + "' at [" + std::to_string(idx) + "]\n");
            else { delete c; return Result::fail("insert failed"); }
            return Result::ok();
        }
        auto* n = t->append(name, overlap);
        if (n) setTextOut(out, "appended " + std::to_string(1 + overlap) + " '" + name + "' -> last: " + t->keyOf(n) + "\n");
        else return Result::fail("append failed");
        return Result::ok();
    }
    auto path = f.pos(0);
    if (path.empty()) return Result::fail("usage: mk <path> | mk -n <name> [-o N] [-a] [--at IDX]");
    bool absolute = path[0] == '/';
    Node* base = absolute ? s->root : s->current;
    std::string relPath = absolute ? path.substr(1) : path;
    auto* n = base->at(relPath);
    if (n) setTextOut(out, "created: /" + n->path(s->root) + "\n");
    else return Result::fail("create failed");
    return Result::ok();
}

static Result rm(Node* ctx, Node* in, Node* out)
{
    auto* s = sess(ctx);
    auto args = argvFromInput(in);
    auto f = parseFlags(args);
    auto* t = targetFromArgs(s->root, s->current, args);
    if (f.has("clear", 'c')) {
        int n = t->count();
        t->clear();
        setTextOut(out, "cleared " + std::to_string(n) + " children\n");
        return Result::ok();
    }
    if (f.has("index", 'i')) {
        auto idxStr = f.get("index", 'i');
        if (!isInt(idxStr)) return Result::fail("usage: rm -i <index>");
        int idx = std::stoi(idxStr);
        if (t->remove(idx)) setTextOut(out, "removed [" + std::to_string(idx) + "]\n");
        else return Result::fail("no child at index " + std::to_string(idx));
        return Result::ok();
    }
    if (f.has("name", 'n')) {
        auto nm = f.get("name", 'n');
        if (nm.empty()) return Result::fail("usage: rm -n <name> [-o N]");
        auto ovStr = f.get("overlap", 'o', "0");
        int overlap = isInt(ovStr) ? std::stoi(ovStr) : 0;
        if (t->remove(nm, overlap)) setTextOut(out, "removed '" + nm + "'\n");
        else return Result::fail("no child '" + nm + "' overlap " + std::to_string(overlap));
        return Result::ok();
    }
    if (f.has("all")) {
        auto nm = f.get("all");
        if (nm.empty()) return Result::fail("usage: rm --all <name>");
        int n = t->remove(nm);
        setTextOut(out, "removed " + std::to_string(n) + " children named '" + nm + "'\n");
        return Result::ok();
    }
    auto childPath = f.pos(0);
    if (!childPath.empty()) {
        bool absolute = childPath[0] == '/';
        Node* base = absolute ? s->root : s->current;
        std::string relPath = absolute ? childPath.substr(1) : childPath;
        if (base->erase(relPath)) setTextOut(out, "removed\n");
        else return Result::fail("not found or is root");
        return Result::ok();
    }
    return Result::fail("usage: rm [path] | rm -i IDX | rm -n NAME [-o N] | rm --all NAME | rm -c");
}

static Result mv(Node* ctx, Node* in, Node* out)
{
    auto* s = sess(ctx);
    auto args = argvFromInput(in);
    auto f = parseFlags(args);
    auto srcPath = f.pos(0);
    if (srcPath.empty()) return Result::fail("usage: mv <src> [dest] [--at INDEX]");
    auto* src = resolveNode(s->root, s->current, srcPath);
    if (!src) return Result::fail("not found: " + srcPath);
    auto destPath = f.pos(1);
    auto* dest = destPath.empty() ? s->current : resolveNode(s->root, s->current, destPath);
    if (!dest) return Result::fail("dest not found: " + destPath);
    if (src == dest) return Result::fail("cannot move node into itself");
    auto atStr = f.get("at");
    if (f.has("at") && isInt(atStr)) {
        int idx = std::stoi(atStr);
        if (dest->insert(src, idx)) setTextOut(out, "moved to [" + std::to_string(idx) + "] " + src->path(s->root) + "\n");
        else return Result::fail("insert failed");
        return Result::ok();
    }
    dest->insert(src);
    setTextOut(out, "moved to " + src->path(s->root) + "\n");
    return Result::ok();
}

static Result cp(Node* ctx, Node* in, Node* out)
{
    auto* s = sess(ctx);
    auto args = argvFromInput(in);
    auto f = parseFlags(args);
    auto srcPath = f.pos(0);
    if (srcPath.empty()) return Result::fail("usage: cp <src> [dest] [-r] [-u] [-I] [-R]");
    auto* src = resolveNode(s->root, s->current, srcPath);
    if (!src) return Result::fail("not found: " + srcPath);
    auto destPath = f.pos(1);
    auto* dest = destPath.empty() ? s->current : resolveNode(s->root, s->current, destPath);
    if (!dest) return Result::fail("dest not found: " + destPath);
    if (src == dest) return Result::fail("cannot copy node onto itself");
    if (src->isAncestorOf(dest) || dest->isAncestorOf(src))
        return Result::fail("cannot copy between overlapping subtrees");
    bool ai = !f.has("no-insert", 'I'), ar = f.has("remove", 'r'), au = f.has("update", 'u'), arp = !f.has("no-replace", 'R');
    int copy_flags = (ai ? Node::COPY_INSERT : 0) | (ar ? Node::COPY_REMOVE : 0)
                   | (au ? Node::COPY_UPDATE : 0) | (arp ? Node::COPY_REPLACE : 0);
    dest->copy(src, copy_flags);
    setTextOut(out, "copied /" + src->path(s->root) + " -> /" + dest->path(s->root)
        + "  (insert:" + std::string(ai?"on":"off") + ", remove:" + std::string(ar?"on":"off")
        + ", update:" + std::string(au?"on":"off") + ", replace:" + std::string(arp?"on":"off") + ")\n");
    return Result::ok();
}

static Result schema(Node* ctx, Node* in, Node* out)
{
    auto* s = sess(ctx);
    auto args = argvFromInput(in);
    auto f = parseFlags(args);
    auto format = f.pos(0);
    if (format.empty()) {
        setTextOut(out, availableSchemaFormatsText() + "\n");
        return Result::ok();
    }
    auto pathStr = f.pos(1);
    auto* t = pathStr.empty() ? s->current : resolveNode(s->root, s->current, pathStr);
    if (!t) return Result::fail("not found: " + pathStr);

    bool isImport = f.has("import", 'i');
    auto file = f.get("file", 'f');
    auto importContent = f.get("import", 'i');

    if (isImport) {
        std::string content;
        if (!file.empty()) {
            std::ifstream ifs(file, format == "bin" ? std::ios::binary : std::ios::in);
            if (!ifs.is_open()) return Result::fail("cannot read: " + file);
            content.assign((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        } else if (!importContent.empty()) {
            content = importContent;
        } else {
            return Result::fail("usage: schema " + format + " -i <content> | -i -f <file>");
        }
        bool ok = schema::schemaToNode(format, t, content);
        if (ok) setTextOut(out, file.empty() ? "imported\n" : "imported from " + file + "\n");
        else return Result::fail("import failed (invalid " + format + ")");
        return Result::ok();
    }

    std::string result = schema::schemaFromNode(format, t);
    if (result.empty()) return Result::fail("unknown format: " + format);

    if (format == "bin") {
        if (!file.empty()) {
            auto bytes = impl::bin::exportTree(t);
            std::ofstream ofs(file, std::ios::binary);
            if (!ofs.is_open()) return Result::fail("cannot write: " + file);
            ofs.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            setTextOut(out, "saved to " + file + " (" + std::to_string(bytes.size()) + " bytes)\n");
            return Result::ok();
        }
        std::ostringstream oss;
        for (size_t i = 0; i < result.size(); i += 2) {
            if (i > 0 && (i/2) % 16 == 0) oss << "\n";
            else if (i > 0) oss << " ";
            oss << result.substr(i, 2);
        }
        oss << "\n(" << std::dec << (result.size()/2) << " bytes)\n";
        setTextOut(out, oss.str());
        return Result::ok();
    }

    if (!file.empty()) {
        std::ofstream ofs(file);
        if (!ofs.is_open()) return Result::fail("cannot write: " + file);
        ofs << result;
        setTextOut(out, "saved to " + file + "\n");
    } else {
        setTextOut(out, result);
    }
    return Result::ok();
}

// --- session commands ---

static Result take(Node* ctx, Node* in, Node* out)
{
    auto* s = sess(ctx);
    auto args = argvFromInput(in);
    if (args.size() < 2) return Result::fail("usage: take <index|path>");
    Node* taken = nullptr;
    if (isInt(args[1])) {
        taken = s->current->take(std::stoi(args[1]));
    } else {
        auto* c = s->current->find(args[1]);
        if (c && c->parent() == s->current) taken = s->current->take(c);
        else if (c) return Result::fail("not a direct child");
        else return Result::fail("not found: " + args[1]);
    }
    if (taken) {
        auto& pool = static_cast<TerminalSession*>(s)->orphans();
        pool.push_back(taken);
        setTextOut(out, "taken: " + nodeSummary(taken) + " -> orphan pool [" + std::to_string(pool.size() - 1) + "]\n");
    } else if (isInt(args[1])) {
        return Result::fail("no child at index " + args[1]);
    }
    return Result::ok();
}

static Result orphans(Node* ctx, Node*, Node* out)
{
    auto* s = sess(ctx);
    auto& pool = static_cast<TerminalSession*>(s)->orphans();
    if (pool.empty()) { setTextOut(out, "(empty)\n"); return Result::ok(); }
    std::string text;
    for (size_t i = 0; i < pool.size(); ++i)
        text += "  [" + std::to_string(i) + "] " + nodeSummary(pool[i]) +
                " (" + std::to_string(pool[i]->count()) + " children)\n";
    setTextOut(out, text);
    return Result::ok();
}

static Result adopt(Node* ctx, Node* in, Node* out)
{
    auto* s = sess(ctx);
    auto args = argvFromInput(in);
    if (args.size() < 2) return Result::fail("usage: adopt <orphan_index>");
    int idx = std::stoi(args[1]);
    auto& pool = static_cast<TerminalSession*>(s)->orphans();
    if (idx < 0 || idx >= (int)pool.size()) return Result::fail("invalid orphan index");
    auto* n = pool[idx];
    pool.erase(pool.begin() + idx);
    s->current->insert(n);
    setTextOut(out, "adopted: " + n->path(s->root) + "\n");
    return Result::ok();
}

static Result history(Node* ctx, Node* in, Node* out)
{
    auto* s = sess(ctx);
    auto& hist = static_cast<TerminalSession*>(s)->history();
    std::string text;
    for (size_t i = 0; i < hist.size(); ++i)
        text += std::to_string(i) + "  " + hist[i] + "\n";
    setTextOut(out, text);
    return Result::ok();
}

// --- help ---

struct HelpCategory {
    const char* id;
    const char* title;
};

static const HelpCategory kCategories[] = {
    {"nav",     "Navigation"},
    {"inspect", "Inspection"},
    {"mutate",  "Mutation"},
    {"session", "Session"},
};

static Result help(Node* ctx, Node* in, Node* out)
{
    auto args = argvFromInput(in);
    auto fl = parseFlags(args);
    bool jsonMode = fl.has("json");

    std::string topic;
    for (int pi = 0; ; ++pi) {
        auto w = fl.pos(pi);
        if (w.empty()) break;
        if (!topic.empty()) topic += ".";
        topic += w;
    }

    const Factory& replF = factory::at("service/repl");
    const Factory& cmdF  = command::factory();

    // --- Single command help ---
    if (!topic.empty()) {
        Node* instr = command::instruction(replF, topic);
        const Factory* sourceF = &replF;
        if (!instr) { instr = command::instruction(cmdF, topic); sourceF = &cmdF; }

        if (!instr) {
            auto desc = command::description(replF, topic);
            if (desc.empty()) desc = command::description(cmdF, topic);
            if (desc.empty()) return Result::fail("unknown command: " + topic);
            setTextOut(out, topic + " - " + desc + "\n");
            return Result::ok();
        }

        if (jsonMode) {
            Node wrapper;
            wrapper.set("name", topic);
            wrapper.copy(instr);
            setTextOut(out, schema::fromNode<schema::JsonS>(&wrapper) + "\n");
            return Result::ok();
        }

        std::string text;
        std::string desc = instr->get("description").toString();
        text += topic;
        if (!desc.empty()) text += " - " + desc;
        text += "\n\n";

        std::string usage = instr->get("usage").toString();
        if (usage.empty()) usage = command::usage(*sourceF, topic);
        text += "Usage: " + usage + "\n";

        if (Node* props = instr->find("input_schema/properties")) {
            std::unordered_set<std::string> req;
            if (Node* r = instr->find("input_schema/required"))
                for (Node* c : r->children()) req.insert(c->get().toString());

            bool hasParams = false;
            for (Node* p : props->children()) {
                if (!hasParams) { text += "\nParameters:\n"; hasParams = true; }
                std::string pname = p->name();
                std::string ptype = p->get("type").toString();
                std::string pdesc = p->get("description").toString();
                std::string pshort = p->get("short").toString();
                bool isReq = req.count(pname) > 0;

                text += "  ";
                if (!pshort.empty()) text += "-" + pshort + ", ";
                text += "--" + pname;
                if (!ptype.empty()) text += " (" + ptype + ")";
                if (isReq) text += " [required]";
                if (!pdesc.empty()) text += "  " + pdesc;
                text += "\n";
            }
        }

        setTextOut(out, text);
        return Result::ok();
    }

    // --- Full command listing ---
    if (jsonMode) {
        Node listing;
        auto addCmds = [&](const Factory& f, const char* source) {
            for (auto& key : f.keys()) {
                Node* item = listing.at("commands")->append();
                item->set("name", key);
                item->set("source", source);
                if (Node* instr = command::instruction(f, key))
                    item->copy(instr);
            }
        };
        addCmds(replF, "repl");
        addCmds(cmdF, "cmd");
        setTextOut(out, schema::fromNode<schema::JsonS>(&listing) + "\n");
        return Result::ok();
    }

    // Group repl commands by category
    std::unordered_map<std::string, std::vector<std::string>> groups;
    std::vector<std::string> uncategorized;

    for (auto& key : replF.keys()) {
        if (key == "g" || key == "s") continue; // skip aliases
        Node* instr = command::instruction(replF, key);
        std::string cat = instr ? instr->get("category").toString() : "";
        if (cat.empty()) uncategorized.push_back(key);
        else groups[cat].push_back(key);
    }

    std::string text;
    for (auto& cat : kCategories) {
        auto it = groups.find(cat.id);
        if (it == groups.end() || it->second.empty()) continue;

        text += "=== ";
        text += cat.title;
        text += " ===\n";

        auto& keys = it->second;
        std::sort(keys.begin(), keys.end());
        for (auto& key : keys) {
            Node* instr = command::instruction(replF, key);
            std::string usage = instr ? instr->get("usage").toString() : "";
            if (usage.empty()) usage = key;
            std::string desc = instr ? instr->get("description").toString() : "";

            text += "  " + usage;
            if (!desc.empty()) {
                int pad = 30 - (int)usage.size();
                text += std::string(pad > 0 ? pad : 2, ' ') + desc;
            }
            text += "\n";
        }
        text += "\n";
    }

    if (!uncategorized.empty()) {
        std::sort(uncategorized.begin(), uncategorized.end());
        text += "=== Other ===\n";
        for (auto& key : uncategorized) {
            auto desc = command::description(replF, key);
            text += "  " + key;
            if (!desc.empty()) { int pad = 30 - (int)key.size(); text += std::string(pad > 0 ? pad : 2, ' ') + desc; }
            text += "\n";
        }
        text += "\n";
    }

    auto userCmds = cmdF.keys();
    if (!userCmds.empty()) {
        std::sort(userCmds.begin(), userCmds.end());
        text += "=== User Commands ===\n";
        for (auto& k : userCmds) {
            std::string usage = command::usage(cmdF, k);
            std::string desc  = command::description(cmdF, k);
            text += "  " + usage;
            if (!desc.empty()) {
                int pad = 30 - (int)usage.size();
                text += std::string(pad > 0 ? pad : 2, ' ') + desc;
            }
            text += "\n";
        }
        text += "\n";
    }

    text += "Type 'help <command>' for details. Use --json for machine-readable output.\n";
    setTextOut(out, text);
    return Result::ok();
}

} // namespace repl

// ============================================================================
// Registration
// ============================================================================

void registerReplCommands(Factory& f)
{
    f.reg("cd",       Var::callable(Proc(repl::cd)));
    f.reg("pwd",      Var::callable(Proc(repl::pwd)));
    f.reg("root",     Var::callable(Proc(repl::root)));
    f.reg("up",       Var::callable(Proc(repl::up)));
    f.reg("first",    Var::callable(Proc(repl::first)));
    f.reg("last",     Var::callable(Proc(repl::last)));
    f.reg("prev",     Var::callable(Proc(repl::prev)));
    f.reg("next",     Var::callable(Proc(repl::next)));
    f.reg("sibling",  Var::callable(Proc(repl::sibling)));
    f.reg("ls",       Var::callable(Proc(repl::ls)));
    f.reg("get",      Var::callable(Proc(repl::get)));
    f.reg("set",      Var::callable(Proc(repl::set)));
    f.reg("mk",       Var::callable(Proc(repl::mk)));
    f.reg("rm",       Var::callable(Proc(repl::rm)));
    f.reg("mv",       Var::callable(Proc(repl::mv)));
    f.reg("cp",       Var::callable(Proc(repl::cp)));
    f.reg("schema",   Var::callable(Proc(repl::schema)));
    f.reg("take",     Var::callable(Proc(repl::take)));
    f.reg("orphans",  Var::callable(Proc(repl::orphans)));
    f.reg("adopt",    Var::callable(Proc(repl::adopt)));
    f.reg("history",  Var::callable(Proc(repl::history)));
    f.reg("help",     Var::callable(Proc(repl::help)));

    // aliases
    f.reg("g", Var::callable(Proc(repl::get)));
    f.reg("s", Var::callable(Proc(repl::set)));
}

// ============================================================================
// TerminalSession
// ============================================================================

struct TerminalSession::Private
{
    std::vector<std::string> hist;
    std::vector<Node*> orphan_pool;
    std::string output;
    TerminalSession::AsyncOutputFn asyncOutput;
    TerminalSession::Options opts;

    ~Private() { for (auto* o : orphan_pool) delete o; }
};

TerminalSession::TerminalSession(Node* root, const Options& opts)
    : Session(root, root)
    , _p(std::make_unique<Private>())
{
    _p->opts = opts;
}

TerminalSession::~TerminalSession() = default;

void TerminalSession::setAsyncOutput(AsyncOutputFn fn)
{
    _p->asyncOutput = std::move(fn);
}

std::vector<Node*>& TerminalSession::orphans() { return _p->orphan_pool; }
const std::vector<std::string>& TerminalSession::history() const { return _p->hist; }

std::string TerminalSession::execute(const std::string& line)
{
    _p->output.clear();
    if (line.empty()) return {};

    auto args = split(line);
    if (args.empty()) return {};

    _p->hist.push_back(line);
    std::string cmd = args[0];

    if (cmd == "quit" || cmd == "exit")
        return "\x04";

    bool asyncMode = false;
    if (cmd == "async" && args.size() > 1) {
        asyncMode = true;
        args.erase(args.begin());
        cmd = args[0];
    }

    auto [builtinNode, builtinWordCount] = resolveFactoryCommand(factory::at("service/repl"), args);
    auto [cmdNode, cmdWordCount] = resolveFactoryCommand(command::factory(), args);
    Node* resolvedNode = builtinNode ? builtinNode : cmdNode;
    size_t resolvedWordCount = builtinNode ? builtinWordCount : cmdWordCount;
    std::string resolvedName;
    for (size_t i = 0; i < resolvedWordCount; ++i) {
        if (i > 0) resolvedName += ".";
        resolvedName += args[i];
    }

    auto fillInput = [&](Node* in) {
        prepareCommandInput(in, this->root, this->current, args, resolvedWordCount);
    };

    if (resolvedNode) {
        if (asyncMode) {
            Pipeline pipe;
            fillInput(pipe.inputNode());
            pipe.contextNode()->set("_session", Var::ptr(static_cast<Session*>(this)));
            pipe.add(Command(resolvedNode));

            auto asyncOut = _p->asyncOutput;
            pipe.onFinished(nullptr, [asyncOut, resolvedName](Pipeline& pipe) {
                std::string text = renderCommandOutput(pipe.outputNode(), pipe.result());
                if (asyncOut && !text.empty()) {
                    if (text.back() != '\n') text.push_back('\n');
                    asyncOut("\x1b[33m[" + resolvedName + "]\x1b[0m " + text);
                }
            });
            pipe.async();
            _p->output += "accepted\n";
            return _p->output;
        }

        Command cmdObj(resolvedNode);
        fillInput(cmdObj.inputNode());
        cmdObj.contextNode()->set("_session", Var::ptr(static_cast<Session*>(this)));
        cmdObj.run();
        updateCurrentFromOut(this->current, cmdObj.outputNode());
        _p->output += renderCommandOutput(cmdObj.outputNode(), cmdObj.result());
        return _p->output;
    }

    _p->output += "unknown: " + cmd + "  (type 'help')\n";
    return _p->output;
}

std::string TerminalSession::prompt() const
{
    bool use_color = _p->opts.prompt_color;
    std::string path_color = "\x1b[36m";
    std::string prompt_color = "\x1b[32m";

    if (_p->opts.prompt_color) {
        if (Node* cfg = this->root->find("ve/server/terminal/repl/config")) {
            use_color = cfg->get("prompt_color").toBool(true);
            auto pc = cfg->get("prompt_path_color").toString();
            if (!pc.empty()) path_color = pc;
            auto sc = cfg->get("prompt_symbol_color").toString();
            if (!sc.empty()) prompt_color = sc;
        }
    }

    if (!use_color) {
        if (current == root) return "/> ";
        return "/" + current->path(root) + "> ";
    }

    if (current == root) {
        return path_color + "/" + "\x1b[0m" + prompt_color + "> \x1b[0m";
    }
    return path_color + "/" + current->path(root) + "\x1b[0m" + prompt_color + "> \x1b[0m";
}

std::vector<std::string> TerminalSession::complete(const std::string& partial)
{
    auto tokens = split(partial);
    bool endsWithSpace = !partial.empty() && std::isspace(static_cast<unsigned char>(partial.back()));

    auto [builtinNode, builtinWords] = resolveFactoryCommand(factory::at("service/repl"), tokens);
    auto [cmdNode, cmdWords] = resolveFactoryCommand(command::factory(), tokens);
    size_t matchedWords = builtinNode ? builtinWords : cmdWords;

    bool pastCmd = ((builtinNode || cmdNode) && (endsWithSpace || tokens.size() > matchedWords));

    if (pastCmd) {
        size_t cmdWordCount = (builtinNode || cmdNode) ? matchedWords : 1;
        std::string prefix;
        if (endsWithSpace)
            prefix = "";
        else if (tokens.size() > cmdWordCount)
            prefix = tokens.back();
        return completeNodePath(root, current, prefix);
    }

    std::string typedPrefix;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (i > 0) typedPrefix += ' ';
        typedPrefix += tokens[i];
    }
    if (endsWithSpace && !tokens.empty()) typedPrefix += ' ';

    std::vector<std::string> matches;

    if (typedPrefix.find(' ') == std::string::npos) {
        for (auto& key : factory::at("service/repl").keys())
            if (key.compare(0, typedPrefix.size(), typedPrefix) == 0)
                matches.push_back(key);
        for (auto* extra : {"quit", "exit"})
            if (std::string(extra).compare(0, typedPrefix.size(), typedPrefix) == 0)
                matches.push_back(extra);
    }

    for (auto& key : command::factory().keys()) {
        std::string keySpace = key;
        std::replace(keySpace.begin(), keySpace.end(), '.', ' ');
        if (keySpace.size() < typedPrefix.size()) continue;
        if (keySpace.compare(0, typedPrefix.size(), typedPrefix) != 0) continue;
        size_t next = keySpace.find(' ', typedPrefix.size());
        matches.push_back(next == std::string::npos ? keySpace : keySpace.substr(0, next));
    }

    std::sort(matches.begin(), matches.end());
    matches.erase(std::unique(matches.begin(), matches.end()), matches.end());
    return matches;
}

} // namespace service
} // namespace ve
