#include "ve/ros/core/common.h"

#include <filesystem>

#ifndef _WIN32
#include <dlfcn.h>
#endif

VE_AUTO_RUN(ve::data::create("_p.global_module_key", VE_UNDEFINED_OBJECT_NAME);)

namespace hemera::engine {

    ve::Object g_obj;
    ve::Dict<ve::Module*> g_modules;

    ve::Module* module(const std::string& key) { return g_modules.value(key); }

    ve::Vector<ve::Module*> modules() { return g_modules.values(); }

    ve::Vector<ve::Module*> setup(const std::string& prefix) {
        ve::Vector<ve::Module*> modules;
        for (const auto& [k, c] : ve::globalModuleFactory()) {
            if (g_modules.has(k)) continue;
            if (!hemera::util::startsWith(k, prefix)) continue;
            std::cout << TAG_BLUE;
            tag("<ve::engine>", "create module", k);
            std::cout << TAG_RESET;
            if (ve::Module* m = c()) {
                modules.append(m);
                g_modules[k] = m;
                m->connect(ve::Object::OBJECT_DELETED, &g_obj, [=] {
                    std::cout << TAG_PURPLE;
                    tag("<ve::engine>", "delete module", k);
                    std::cout << TAG_RESET;
                    g_modules.erase(k);
                });
            } else {
                etag("<ve::engine>", "create module", k, "failed!");
            }
        }
        return modules;
    }

    template<ve::Module::State S>
    inline void step(const ve::Vector<ve::Module*>& modules) {
        for (const auto& m : modules) {
            if (m->state() >= S) {
                etag("<ve::engine>", "ERROR", "module", m->name(), "with state", m->state(), " run invalid step", S);
                continue;
            }
            wtag("<ve::engine>", "module", m->name(), "step", S);
            m->template exeState<S>();
        }
    }

    bool loadApp(const std::string& name, const std::string& prefix, ve::Module::State exe_step) {
        auto ms = setup("");
        return true;
    }

    bool autoLoadAllApps(const std::string& prefix) {
        try {
            for (const auto& it : std::filesystem::directory_iterator(prefix)) {
                std::string lib_path = it.path().string();
                if (lib_path.find("libveros") == std::string::npos &&
                    lib_path.find("libhemera") == std::string::npos) continue;
#ifndef _WIN32
                void* handle = dlopen(lib_path.c_str(), RTLD_NOW);
                if (!handle) {
                    etag("<ve::engine>", "ERROR", "load app", lib_path, "failed:", dlerror());
                    continue;
                }
#endif
                itag("<ve::engine>", "load app", lib_path, "successfully");
            }
        } catch (const std::exception&) {
        }

        step<ve::Module::INIT>(setup(""));
        return true;
    }

    void start(bool block) {
        step<ve::Module::READY>(modules());
        if (block) {
            char c;
            std::cin >> c;
        }
    }

    void stop() {
        auto ms = modules();
        step<ve::Module::DEINIT>(ms);
        for (const auto& m : ms) {
            delete m;
        }
        g_modules.clear();
    }

}
