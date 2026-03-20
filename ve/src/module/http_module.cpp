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
            node()->at("config/port")->getInt(8080));

        server_ = std::make_unique<HttpServer>(node::root(), port);

        if (auto* srn = node()->find("config/static_root")) {
            std::string staticRoot = srn->getString();
            if (!staticRoot.empty()) {
                server_->setStaticRoot(staticRoot);
                veLogI << "[ve.service.http] static root: " << staticRoot;
            }
        }

        if (Node* dfn = node()->find("config/default_file")) {
            if (dfn->hasValue()) {
                std::string df = dfn->getString("");
                if (!df.empty()) {
                    server_->setDefaultFile(df);
                }
            }
        }

        if (server_->start()) {
            veLogI << "[ve.service.http] started on port " << port;
            node()->at("runtime/port")->set(Var(static_cast<int64_t>(port)));
            node()->at("runtime/listening")->set(Var(true));
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
        if (Node* ln = node()->find("runtime/listening")) {
            ln->set(Var(false));
        }
    }
};

} // namespace ve

VE_REGISTER_PRIORITY_MODULE(ve.service.http, ve::HttpModule, 50, 1)
