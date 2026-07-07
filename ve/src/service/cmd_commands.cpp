#include "cmd_commands.h"
#include "node_commands.h"
#include "ve/core/command.h"
#include "ve/core/schema.h"
#include "ve/core/impl/bin.h"

#include <fstream>

namespace ve {
namespace service {

namespace {

struct SearchOpts {
    std::string pattern;
    std::string root;
    enum Mode { Contains, Glob, Exact };
    enum Target { Key, Value };
    Mode mode = Contains;
    Target target = Key;
    bool case_sensitive = false;
    int top = 100;
};

static char lc(char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; }

static bool containsMatch(std::string_view needle, std::string_view hay, bool cs)
{
    if (needle.empty()) return true;
    if (needle.size() > hay.size()) return false;
    const auto n = needle.size(), m = hay.size();
    for (std::size_t i = 0; i + n <= m; ++i) {
        std::size_t k = 0;
        for (; k < n; ++k) {
            char a = hay[i + k], b = needle[k];
            if (!cs) { a = lc(a); b = lc(b); }
            if (a != b) break;
        }
        if (k == n) return true;
    }
    return false;
}

static bool globMatch(std::string_view pat, std::string_view text, bool cs)
{
    // Iterative backtracking: on mismatch, jump back to last '*'.
    std::size_t p = 0, t = 0, star = std::string_view::npos, ts = 0;
    const auto pn = pat.size(), tn = text.size();
    auto eq = [&](char a, char b) {
        if (!cs) { a = lc(a); b = lc(b); }
        return a == b;
    };
    while (t < tn) {
        if (p < pn && (pat[p] == '?' || eq(pat[p], text[t]))) { ++p; ++t; }
        else if (p < pn && pat[p] == '*')                     { star = p++; ts = t; }
        else if (star != std::string_view::npos)              { p = star + 1; t = ++ts; }
        else                                                   return false;
    }
    while (p < pn && pat[p] == '*') ++p;
    return p == pn;
}

static bool exactMatch(std::string_view pat, std::string_view text, bool cs)
{
    if (pat.size() != text.size()) return false;
    for (std::size_t i = 0; i < pat.size(); ++i) {
        char a = pat[i], b = text[i];
        if (!cs) { a = lc(a); b = lc(b); }
        if (a != b) return false;
    }
    return true;
}

static bool matchStr(const SearchOpts& o, std::string_view s)
{
    switch (o.mode) {
        case SearchOpts::Contains: return containsMatch(o.pattern, s, o.case_sensitive);
        case SearchOpts::Glob:     return globMatch(o.pattern, s, o.case_sensitive);
        case SearchOpts::Exact:    return exactMatch(o.pattern, s, o.case_sensitive);
    }
    return false;
}

// Read named fields only. When called from network with `in.args` positional
// tokens, we first defer to command::bind — same schema-driven flag+positional
// mapping the REPL uses, so both entrypoints share one parser.
static Result parseSearchOpts(Node* in, SearchOpts& o)
{
    if (Node* args = in->find("args")) {
        Strings tokens;
        for (auto* c : args->children()) tokens.push_back(c->getString());
        command::bind(command::factory(), "search", tokens, in);
    }

    o.pattern        = in->get("pattern").toString();
    o.root           = in->get("root").toString();
    o.case_sensitive = in->get("case_sensitive").toBool(false);
    o.top            = in->get("top").toInt(100);

    std::string mode = in->get("mode").toString();
    if      (mode.empty() || mode == "contains") o.mode = SearchOpts::Contains;
    else if (mode == "glob")                     o.mode = SearchOpts::Glob;
    else if (mode == "exact")                    o.mode = SearchOpts::Exact;
    else return Result::fail(ve::service::ERR_INVALID, "unknown mode: " + mode);

    std::string tgt = in->get("target").toString();
    if      (tgt.empty() || tgt == "key") o.target = SearchOpts::Key;
    else if (tgt == "value")              o.target = SearchOpts::Value;
    else return Result::fail(ve::service::ERR_INVALID, "unknown target: " + tgt);

    if (o.pattern.empty()) return Result::fail(ve::service::ERR_INVALID, "pattern required");
    if (o.top <= 0)        return Result::fail(ve::service::ERR_INVALID, "top must be > 0");

    if (!o.case_sensitive) {
        for (auto& c : o.pattern) c = lc(c);
    }
    return Result::ok();
}

// Iterative DFS from `start` (excluded), collect matches into `out`.
// Stops early when out.size() == o.top.
static void collect(Node* start, const SearchOpts& o, std::vector<std::string>& out)
{
    struct Frame { Node* n; Node::ChildIterator it, end; };
    std::vector<Frame> stack;
    stack.reserve(16);
    stack.push_back({start, start->begin(), start->end()});

    while (!stack.empty() && (int)out.size() < o.top) {
        auto& top = stack.back();
        if (top.it == top.end) { stack.pop_back(); continue; }
        Node* child = *top.it;
        ++top.it;

        bool hit = false;
        if (o.target == SearchOpts::Key) {
            hit = matchStr(o, child->name());
        } else {
            auto v = child->get();
            if (!v.isNull()) hit = matchStr(o, v.toString());
        }
        if (hit) out.push_back(child->path(start));

        if ((int)out.size() >= o.top) break;
        stack.push_back({child, child->begin(), child->end()});
    }
}

} // anonymous namespace

namespace cmd {

static std::string detectFormat(const std::string& file)
{
    auto dot = file.rfind('.');
    if (dot == std::string::npos) return "json";
    auto ext = file.substr(dot + 1);
    if (ext == "bin") return "bin";
    if (ext == "xml") return "xml";
    return "json";
}

static Result save(Node* ctx, Node* in, Node* out)
{
    auto* s = ctx->get("_session").as<Session*>();
    std::string path   = in->get("path").toString();
    std::string file   = in->get("file").toString();
    std::string format = in->get("format").toString();
    if (path.empty() || file.empty())
        return Result::fail("path and file required");
    Node* target = s->root->find(path);
    if (!target) return Result::fail("not found: " + path);
    if (format.empty()) format = detectFormat(file);

    if (format == "bin") {
        auto bytes = impl::bin::exportTree(target);
        std::ofstream ofs(file, std::ios::binary);
        if (!ofs) return Result::fail("cannot write: " + file);
        ofs.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        out->set("file", file);
        out->set("size", static_cast<int64_t>(bytes.size()));
    } else {
        std::string data = schema::schemaFromNode(format, target);
        if (data.empty()) return Result::fail("unknown format: " + format);
        std::ofstream ofs(file);
        if (!ofs) return Result::fail("cannot write: " + file);
        ofs << data;
        out->set("file", file);
        out->set("size", static_cast<int64_t>(data.size()));
    }
    return Result::ok();
}

static Result load(Node* ctx, Node* in, Node* out)
{
    auto* s = ctx->get("_session").as<Session*>();
    std::string file   = in->get("file").toString();
    std::string path   = in->get("path").toString();
    std::string format = in->get("format").toString();
    if (file.empty() || path.empty())
        return Result::fail("file and path required");
    if (format.empty()) format = detectFormat(file);

    bool binary = (format == "bin");
    std::ifstream ifs(file, binary ? std::ios::binary : std::ios::in);
    if (!ifs) return Result::fail("cannot read: " + file);
    std::string data((std::istreambuf_iterator<char>(ifs)),
                      std::istreambuf_iterator<char>());

    Node* target = s->root->at(path);
    if (!schema::schemaToNode(format, target, data))
        return Result::fail("import failed (" + format + ")");

    out->set("path", target->path(s->root));
    return Result::ok();
}

static Result search(Node* ctx, Node* in, Node* out)
{
    SearchOpts o;
    if (auto r = parseSearchOpts(in, o); r.isError()) return r;

    auto* s = ctx->get("_session").as<Session*>();
    Node* start = o.root.empty() ? s->root : s->root->find(o.root);
    if (!start) return Result::fail(ERR_NOT_FOUND, "not found: " + o.root);

    std::vector<std::string> paths;
    paths.reserve(std::min(o.top, 64));
    collect(start, o, paths);

    Node* matches = out->at("matches");
    for (auto& p : paths) matches->append()->set(p);
    out->set("count", static_cast<int64_t>(paths.size()));
    return Result::ok();
}

} // namespace cmd

void registerCmdCommands(Factory& f)
{
    f.reg("save", Var::callable(Proc(cmd::save)));
    f.reg("load", Var::callable(Proc(cmd::load)));
    f.reg("search", Var::callable(Proc(cmd::search)));
}

} // namespace service
} // namespace ve
