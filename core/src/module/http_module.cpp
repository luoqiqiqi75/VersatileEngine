// http_module.cpp - ve::HttpModule (ve.service.http)

#include "ve/core/module.h"
#include "ve/core/log.h"
#include "ve/service/http_server.h"

namespace ve {

class HttpModule : public Module
{
    std::unique_ptr<HttpServer> server_;

public:
    explicit HttpModule(const std::string& name) : Module(name) {}

protected:
    void ready() override
    {
        uint16_t port = static_cast<uint16_t>(
            node()->resolve("config/port")->get<int>(8080));

        server_ = std::make_unique<HttpServer>(node::root(), port);

        std::string staticRoot = node()->getAt<std::string>("config/static_root");
        if (!staticRoot.empty()) {
            server_->setStaticRoot(staticRoot);
            veLogI << "[ve.service.http] static root: " << staticRoot;
        }

        // Must use resolve + hasValue: node()->get<std::string>("config/default_file")
        // passes the string as the Var default when this node has no value, which wrongly
        // set the HTTP index file to the literal "config/default_file".
        if (Node* dfn = node()->resolve("config/default_file")) {
            if (dfn->hasValue()) {
                std::string df = dfn->get<std::string>("");
                if (!df.empty()) {
                    server_->setDefaultFile(df);
                }
            }
        }

        if (server_->start()) {
            veLogI << "[ve.service.http] started on port " << port;
            node()->ensure("runtime/port")->set(Var(static_cast<int64_t>(port)));
            node()->ensure("runtime/listening")->set(Var(true));
            if (!staticRoot.empty()) {
                node()->ensure("runtime/static_root")->set(Var(staticRoot));
            }
        } else {
            veLogE << "[ve.service.http] failed to start on port " << port;
        }
    }

    void deinit() override
    {
        if (server_) {
            server_->stop();
            server_.reset();
        }
        if (Node* ln = node()->resolve("runtime/listening")) {
            ln->set(Var(false));
        }
    }
};

} // namespace ve

VE_REGISTER_PRIORITY_MODULE(ve.service.http, ve::HttpModule, 50, 1)
