#include "ve/core/plugin.h"
#include "ve/core/factory.h"
#include "ve/core/log.h"

namespace ve::plugin {
namespace {

Vector<Info>& plugins() { static Vector<Info> list; return list; }

std::string defaultName(const std::string& path)
{
    auto slash = path.find_last_of("/\\");
    std::string name = path.substr(slash == std::string::npos ? 0 : slash + 1);
    auto dot = name.rfind('.');
    if (dot != std::string::npos) name.resize(dot);
    if (name.compare(0, 3, "lib") == 0) name.erase(0, 3);
    return name;
}

bool process(Node* specs, Node* out, bool loading, bool verbose, std::string* error_out)
{
    if (error_out) error_out->clear();
    auto fail = [&](const std::string& error) {
        if (error_out) *error_out = error;
        veLogE << "[ve/plugin] " << error;
        return false;
    };
    if (!specs) return fail("plugin specification required");
    Node* results = out ? out->at("plugins") : nullptr;
    if (results) { results->clear(); results->set(Var::ListV{}); }
    int count = 0;
    auto process_one = [&](Node* spec) {
        std::string path = spec->get("path").toString();
        std::string name = spec->get("name").toString();
        if (name.empty()) name = defaultName(path);
        bool enabled = spec->get("enabled").toBool(true);
        if (enabled) {
            if (name.empty() || (loading && path.empty())) {
                return fail(loading ? "plugin path required" : "plugin name or path required");
            }
            auto& list = plugins();
            auto found = std::find_if(list.begin(), list.end(), [&](const Info& info) { return info.name == name; });
            std::string error;
            if (loading) {
                if (found != list.end()) return fail("plugin is already loaded: " + name);
                int min_api = spec->get("min_api").toInt(0);
                if (min_api < 0) return fail("min_api must be nonnegative");
                void* handle = impl::open(path, error);
                if (!handle) return fail(error);
                int api = version::number(name);
                if (api < min_api) {
                    std::string close_error;
                    if (!impl::close(handle, close_error)) list.push_back({path, name, api, handle});
                    return fail("plugin version check failed: " + name
                        + (close_error.empty() ? "" : "; " + close_error));
                }
                list.push_back({path, name, api, handle});
            } else {
                if (found == list.end()) return fail("plugin is not loaded: " + name);
                path = found->path;
                if (!impl::close(found->handle, error)) return fail(error);
                list.erase(found);
            }
            ++count;
            if (verbose) veLogI << "[ve/plugin] " << (loading ? "Loaded: " : "Unloaded: ") << name;
        }
        if (results) {
            Node* item = results->append();
            item->set("name", name);
            item->set("path", path);
            item->set("skipped", !enabled);
            item->set("loaded", std::any_of(plugins().begin(), plugins().end(),
                [&](const Info& info) { return info.name == name; }));
        }
        return true;
    };
    if (specs->count() && specs->child(0)->name().empty()) {
        for (Node* spec : *specs) {
            if (!spec->name().empty()) return fail("plugin list entries must be anonymous");
            if (!process_one(spec)) return false;
        }
    } else if (specs->count() || (!specs->get().isNull() && !specs->get().isList())) {
        if (!process_one(specs)) return false;
    }
    if (out) out->set("count", count);
    return true;
}

} // namespace

bool load(Node* specs, Node* out, bool verbose, std::string* error)
{ return process(specs, out, true, verbose, error); }

bool unload(Node* specs, Node* out, bool verbose, std::string* error)
{ return process(specs, out, false, verbose, error); }

const Vector<Info>& loaded() { return plugins(); }

} // namespace ve::plugin
