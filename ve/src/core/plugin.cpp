#include "plugin.h"
#include "ve/core/log.h"

namespace ve::plugin {
namespace {

struct Info {
    std::string path;
    std::string name;
    void* handle = nullptr;
};

Vector<Info>& plugins() { static Vector<Info> list; return list; }

std::string defaultName(const std::string& path)
{
    auto slash = path.find_last_of("/\\");
    std::string name = path.substr(slash == std::string::npos ? 0 : slash + 1);
    auto dot = name.rfind('.');
    if (dot != std::string::npos) name.resize(dot);
    if (name.size() > 3 && name.compare(0, 3, "lib") == 0) name.erase(0, 3);
    return name;
}

} // namespace

// Entries are independent: a failed one is logged and the rest still load.
// Returns false if any entry failed; error gets the first message.
bool load(Node* specs, Node* out, bool verbose, std::string* error)
{
    std::string first_error;
    auto fail = [&](const std::string& message) {
        if (first_error.empty()) first_error = message;
        veLogE << "[ve/plugin] " << message;
    };
    Node* results = out ? out->at("plugins") : nullptr;
    if (results) { results->clear(); results->set(Var::ListV{}); }
    auto load_one = [&](Node* spec) {
        std::string path = spec->get("path").toString();
        std::string name = spec->get("name").toString();
        if (name.empty()) name = defaultName(path);
        bool loaded = false;
        if (!spec->get("enabled").toBool(true)) {
        } else if (name.empty()) {
            fail("plugin name or path required");
        } else if (std::any_of(plugins().begin(), plugins().end(), [&](const Info& info) { return info.name == name; })) {
            fail("plugin already loaded: " + name);
        } else {
            Strings candidates;
            if (!path.empty()) candidates = {path};
            else candidates = {"lib" + name + impl::suffix(), name + impl::suffix()};
            std::string errors;
            for (const auto& candidate : candidates) {
                std::string open_error;
                if (void* handle = impl::open(candidate, open_error)) {
                    path = candidate;
                    plugins().push_back({path, name, handle});
                    loaded = true;
                    if (verbose) veLogI << "[ve/plugin] Loaded: " << name << " (" << path << ")";
                    break;
                }
                errors += (errors.empty() ? "" : "; ") + open_error;
            }
            if (!loaded) fail(errors);
        }
        if (results) {
            Node* item = results->append();
            item->set("name", name);
            item->set("path", path);
            item->set("loaded", loaded);
        }
    };
    if (!specs) {
        fail("plugin specification required");
    } else if (specs->count() && specs->child(0)->name().empty()) {
        for (Node* spec : *specs) {
            if (spec->name().empty()) load_one(spec);
            else fail("plugin list entries must be anonymous");
        }
    } else if (specs->count()) {
        load_one(specs);
    }
    if (error) *error = first_error;
    return first_error.empty();
}

} // namespace ve::plugin
