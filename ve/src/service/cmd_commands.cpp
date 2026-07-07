#include "cmd_commands.h"
#include "node_commands.h"
#include "ve/core/command.h"
#include "ve/core/schema.h"
#include "ve/core/impl/bin.h"

#include <fstream>

namespace ve {
namespace service {

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
    std::string pattern = in->get("pattern").toString();
    if (pattern.empty()) return Result::fail(ERR_INVALID, "pattern required");
    (void)ctx; (void)out;
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
