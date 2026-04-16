// static_service.h — StaticServer: static file hosting + HTTP proxy
#pragma once

#include "ve/global.h"
#include <cstdint>
#include <string>

namespace ve {
namespace service {

class VE_API StaticServer
{
public:
    explicit StaticServer(uint16_t port);
    ~StaticServer();

    // 添加挂载点，按最长前缀匹配。prefix="/" 为根挂载。
    void addMount(const std::string& prefix, const std::string& root,
                  const std::string& defaultFile = "index.html",
                  bool spaFallback = false);

    // 为指定挂载点添加代理规则（proxyPrefix 相对于 mountPrefix）
    void addMountProxy(const std::string& mountPrefix,
                       const std::string& proxyPrefix,
                       const std::string& target);

    bool start();
    void stop();
    bool isRunning() const;

private:
    VE_DECLARE_UNIQUE_PRIVATE
};

} // namespace service
} // namespace ve
