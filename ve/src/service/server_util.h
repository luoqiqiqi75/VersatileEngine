// server_util.h — Unified port binding utility for asio2 servers
#pragma once

#include "ve/core/log.h"
#include <string>
#include <cstdint>

#ifdef _WIN32
#include <asio2/asio2.hpp>
#endif

namespace ve {
namespace service {

namespace detail {
    template <typename T, typename = void>
    struct has_acceptor : std::false_type {};

    template <typename T>
    struct has_acceptor<T, std::void_t<decltype(std::declval<T>().acceptor())>> : std::true_type {};
}

template <typename Server>
bool startServerWithPortFallback(Server& server, const std::string& serverName, uint16_t& port) {
#ifdef _WIN32
    // Windows: Disable port reuse to ensure bind() fails if port is already in use by another instance
    server.bind_init([&server]() {
        asio::error_code ec;
        if constexpr (detail::has_acceptor<Server>::value) {
            server.acceptor().set_option(asio::socket_base::reuse_address(false), ec);
        } else {
            server.socket().set_option(asio::socket_base::reuse_address(false), ec);
        }
    });
#endif

    uint16_t startPort = port;
    uint16_t endPort = port;
    
    // If port ends in 00, we try up to 99
    if ((port % 100) == 0) {
        endPort = port + 99;
    }

    for (uint16_t p = startPort; p <= endPort; ++p) {
        if (server.start("0.0.0.0", p)) {
            if (p != startPort) {
                veLogWs("{} started on fallback port {} (default {} failed)", serverName, p, startPort);
            } else {
                veLogIs("{} started on port", serverName, p);
            }
            port = p; // Update the port to the actually bound one
            return true;
        }
    }

    if (startPort == endPort) {
        veLogEs("{} failed to start on port", serverName, startPort);
    } else {
        veLogEs("{} failed to start on any port between {} and {}", serverName, startPort, endPort);
    }
    return false;
}

} // namespace service
} // namespace ve
