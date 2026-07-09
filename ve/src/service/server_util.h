// server_util.h — internal helpers for asio2-based ve::service servers
#pragma once

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <asio2/asio2.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace ve {
namespace service {

// Shared iopool for every asio2 server ve owns. Each server used to build its
// own iopool (hw_conc*2 threads apiece); with 6-8 servers that meant hundreds
// of io threads and a compound spin-wait on shutdown (~1.5-2s baseline just to
// join them). One shared pool means one set of threads and one stop wait.
//
// Lifetime: first call starts the pool; process shutdown drops the singleton
// and asio2::iopool::~iopool() stops it. All servers must be stopped before
// then (which is what ServerModule::deinit does).
//
// Return type is asio2::iopool& so asio2 server constructors that take a
// `Scheduler&&` overload can consume it directly:
//     asio2::tcp_server server(bufsz, maxbuf, sharedIopool());
inline asio2::iopool& sharedIopool()
{
    struct Holder {
        asio2::iopool pool;
        Holder() { pool.start(); }
        ~Holder() { pool.stop(); }
    };
    static Holder h;
    return h.pool;
}

namespace detail {
    template <typename T, typename = void>
    struct has_acceptor : std::false_type {};

    template <typename T>
    struct has_acceptor<T, std::void_t<decltype(std::declval<T>().acceptor())>> : std::true_type {};
}

template <typename AsioServer>
void disableWindowsPortReuse(AsioServer& server) {
#ifdef _WIN32
    // Windows: disable port reuse so bind() fails if the port is taken by
    // another process (default Windows behavior would silently accept).
    server.bind_init([&server]() {
        asio::error_code ec;
        if constexpr (detail::has_acceptor<AsioServer>::value) {
            server.acceptor().set_option(asio::socket_base::reuse_address(false), ec);
        } else {
            server.socket().set_option(asio::socket_base::reuse_address(false), ec);
        }
    });
#endif
}

} // namespace service
} // namespace ve
