// Real sockets verify upload streaming, framing, limits and proxy deadlines.
#include <ve/entry.h>
#include <ve/service/static_service.h>
#include <asio2/asio2.hpp>
#include <array>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

using namespace ve;
using namespace std::chrono_literals;

namespace {
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

uint16_t unusedPort()
{
    asio::io_context io;
    asio::ip::tcp::acceptor acceptor(io, {asio::ip::tcp::v4(), 0});
    return acceptor.local_endpoint().port();
}

uint64_t checksum(std::string_view data, uint64_t hash = 14695981039346656037ull)
{
    for (unsigned char c : data) hash = (hash ^ c) * 1099511628211ull;
    return hash;
}

struct Observations {
    std::mutex mutex;
    std::condition_variable changed;
    std::size_t bytes = 0;
    int longRequests = 0;
};

struct UpstreamConnection : std::enable_shared_from_this<UpstreamConnection> {
    asio::ip::tcp::socket socket;
    asio::steady_timer delay;
    beast::flat_buffer buffer;
    http::request_parser<http::buffer_body> parser;
    std::array<char, 16384> chunk{};
    std::size_t bytes = 0;
    uint64_t hash = 14695981039346656037ull;
    Observations& seen;

    UpstreamConnection(asio::ip::tcp::socket stream, Observations& observations)
        : socket(std::move(stream)), delay(socket.get_executor()), seen(observations)
    {
        parser.body_limit(128ull * 1024 * 1024);
    }

    void start()
    {
        http::async_read_header(socket, buffer, parser,
            [s = shared_from_this()](asio::error_code ec, std::size_t) {
                if (ec) return;
                if (s->parser.get().target() == "/v1/long-delay") {
                    std::lock_guard<std::mutex> lock(s->seen.mutex);
                    ++s->seen.longRequests;
                    s->seen.changed.notify_all();
                }
                s->read();
            });
    }

    void read()
    {
        if (parser.is_done()) return reply();
        parser.get().body().data = chunk.data();
        parser.get().body().size = chunk.size();
        http::async_read_some(socket, buffer, parser,
            [s = shared_from_this()](asio::error_code ec, std::size_t) {
                if (ec == http::error::need_buffer) ec = {};
                if (ec) return;
                auto size = s->chunk.size() - s->parser.get().body().size;
                s->bytes += size;
                s->hash = checksum({s->chunk.data(), size}, s->hash);
                {
                    std::lock_guard<std::mutex> lock(s->seen.mutex);
                    s->seen.bytes += size;
                    s->seen.changed.notify_all();
                }
                s->read();
            });
    }

    void reply()
    {
        if (parser.get().target() == "/v1/delay" || parser.get().target() == "/v1/long-delay") {
            delay.expires_after(parser.get().target() == "/v1/delay" ? 400ms : 31000ms);
            delay.async_wait([s = shared_from_this()](asio::error_code ec) {
                if (!ec) s->send();
            });
        } else send();
    }

    void send()
    {
        auto response = std::make_shared<http::response<http::string_body>>(http::status::ok, 11);
        response->keep_alive(false);
        response->set("X-Upstream-Host", parser.get()[http::field::host]);
        response->set("X-Content-Type", parser.get()[http::field::content_type]);
        response->set("X-Hop-Leaked", parser.get()["X-Private-Hop"].empty() ? "no" : "yes");
        response->insert(http::field::set_cookie, "a=1");
        response->insert(http::field::set_cookie, "b=2");
        if (parser.get().target() == "/v1/large") response->body() = std::string(3 * 1024 * 1024, 'r');
        else response->body() = std::string(parser.get().target()) + " " + std::to_string(bytes) + " " + std::to_string(hash);
        response->prepare_payload();
        if (parser.get().target() == "/v1/chunked") {
            response->erase(http::field::content_length);
            response->chunked(true);
        }
        if (parser.get().target() == "/v1/eof") response->erase(http::field::content_length);
        auto finish = [s = shared_from_this(), response](asio::error_code, std::size_t) {
            asio::error_code ignored;
            s->socket.shutdown(asio::ip::tcp::socket::shutdown_both, ignored);
            s->socket.close(ignored);
        };
        if (parser.get().method() == http::verb::head) {
            auto writer = std::make_shared<http::response_serializer<http::string_body>>(*response);
            http::async_write_header(socket, *writer,
                [finish, writer](asio::error_code ec, std::size_t size) { finish(ec, size); });
        } else http::async_write(socket, *response, std::move(finish));
    }
};

class Upstream {
    asio::io_context io_;
    asio::ip::tcp::acceptor acceptor_{io_, {asio::ip::tcp::v4(), 0}};
    std::thread worker_;
    void accept()
    {
        acceptor_.async_accept([this](asio::error_code ec, asio::ip::tcp::socket socket) {
            if (ec) return;
            std::make_shared<UpstreamConnection>(std::move(socket), seen)->start();
            accept();
        });
    }
public:
    Observations seen;
    Upstream() { accept(); worker_ = std::thread([this] { io_.run(); }); }
    ~Upstream() { io_.stop(); worker_.join(); }
    auto port() const { return acceptor_.local_endpoint().port(); }
};

http::response<http::string_body> request(uint16_t port, std::string target,
                                         std::string body = {}, bool chunked = false,
                                         std::chrono::milliseconds timeout = 10s)
{
    http::request<http::string_body> req{http::verb::post, target, 11};
    req.set(http::field::host, "localhost");
    req.set(http::field::content_type, "application/octet-stream");
    req.set(http::field::connection, "X-Private-Hop, close");
    req.set("X-Private-Hop", "secret");
    req.body() = std::move(body);
    req.prepare_payload();
    if (chunked) {
        req.erase(http::field::content_length);
        req.chunked(true);
    }
    auto rep = asio2::http_client::execute("127.0.0.1", port, req, timeout);
    require(!asio2::get_last_error(), "proxy request failed at transport");
    return rep;
}

void checkStreaming(uint16_t port, Upstream& upstream, bool chunked)
{
    asio::io_context io;
    asio::ip::tcp::socket socket(io);
    socket.connect({asio::ip::make_address("127.0.0.1"), port});
    std::string header = "POST /api/echo?stream=1 HTTP/1.1\r\nHost: localhost\r\n";
    header += chunked ? "Transfer-Encoding: chunked\r\n" : "Content-Length: 131072\r\n";
    header += "Expect: 100-continue\r\n\r\n";
    asio::write(socket, asio::buffer(header));
    beast::flat_buffer buffer;
    http::response_parser<http::empty_body> interim;
    http::read_header(socket, buffer, interim);
    require(interim.get().result() == http::status::continue_, "missing 100 Continue");
    const std::string half(65536, 'u');
    std::size_t before;
    { std::lock_guard<std::mutex> lock(upstream.seen.mutex); before = upstream.seen.bytes; }
    if (chunked) asio::write(socket, asio::buffer(std::string("10000\r\n")));
    asio::write(socket, asio::buffer(half));
    if (chunked) asio::write(socket, asio::buffer(std::string("\r\n")));
    {
        std::unique_lock<std::mutex> lock(upstream.seen.mutex);
        require(upstream.seen.changed.wait_for(lock, 2s, [&] { return upstream.seen.bytes >= before + half.size(); }),
                "upstream did not receive data before upload completed");
    }
    if (chunked) asio::write(socket, asio::buffer(std::string("10000\r\n")));
    asio::write(socket, asio::buffer(half));
    if (chunked) asio::write(socket, asio::buffer(std::string("\r\n0\r\n\r\n")));
    http::response<http::string_body> response;
    http::read(socket, buffer, response);
    require(response.result() == http::status::ok, "streaming upload failed");
    require(response.body().find("131072 " + std::to_string(checksum(half + half))) != std::string::npos,
            "streaming upload bytes changed");
    // The same downstream connection remains usable after a streamed response.
    http::request<http::empty_body> get{http::verb::get, "/api/echo", 11};
    get.set(http::field::host, "localhost");
    http::write(socket, get);
    http::response<http::string_body> second;
    http::read(socket, buffer, second);
    require(second.result() == http::status::ok, "proxy keep-alive failed");
    std::cout << "PASS streaming " << (chunked ? "chunked" : "Content-Length") << " and keep-alive\n";
}
} // namespace

int main(int argc, char** argv)
{
    Upstream upstream;
    auto port = unusedPort();
    auto root = std::filesystem::temp_directory_path() / ("ve_static_proxy_" + std::to_string(port));
    std::filesystem::create_directory(root);
    std::ofstream(root / "probe.txt") << "static-ok";
    Node options;
    options.set("config_file", "__ve_static_proxy_test_absent.json");
    options.set("log/level", "error");
    options.set("modules/ve/core/config/rescue/enabled", false);
    options.set("modules/ve/client/terminal/stdio/enabled", false);
    options.set("modules/ve/client/terminal/tcp/enabled", false);
    for (const char* path : {"node/http", "node/ws", "node/tcp", "node/udp", "bin/tcp", "terminal/repl", "terminal/ai"})
        options.set("modules/ve/server/" + std::string(path) + "/enable", false);
    auto config = options.at("modules/ve/server/static");
    config->set("enable", true);
    config->set("config/port", port);
    config->set("config/max_retry", 0);
    config->set("config/max_request_body_bytes", 4 * 1024 * 1024);
    auto mount = config->at("config/mounts")->append();
    mount->set("prefix", "/");
    mount->set("root", root.string());
    const std::string target = "http://127.0.0.1:" + std::to_string(upstream.port()) + "/v1";
    for (const auto& rule : {std::make_pair("/api", 3000), std::make_pair("/short", 100), std::make_pair("/long", 35000)}) {
        auto proxy = mount->at("proxy")->append();
        proxy->set("prefix", rule.first);
        proxy->set("target", target);
        proxy->set("timeout_ms", rule.second);
    }
    auto proxyOnly = config->at("config/mounts")->append();
    proxyOnly->set("prefix", "/app");
    proxyOnly->set("spa_fallback", true);
    auto nestedRule = proxyOnly->at("proxy")->append();
    nestedRule->set("prefix", "/backend");
    nestedRule->set("target", "http://127.0.0.1:" + std::to_string(upstream.port()) + "/");
    entry::setup(&options);
    entry::init();
    int result = 0;
    try {
        std::string payload(2 * 1024 * 1024, '\0');
        for (std::size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<char>(i % 251);
        auto direct = request(upstream.port(), "/v1/echo?x=1", payload);
        for (bool chunked : {false, true}) {
            auto proxied = request(port, "/api/echo?x=1", payload, chunked);
            if (proxied.result() != http::status::ok || proxied.body() != direct.body())
                std::cerr << "direct: " << direct.body() << "; proxy (chunked=" << chunked
                          << "): " << proxied.result_int() << " " << proxied.body() << '\n';
            require(proxied.result() == http::status::ok && proxied.body() == direct.body(), "2 MiB upload differs from direct");
            require(proxied["X-Hop-Leaked"] == "no", "hop-by-hop field leaked");
            require(proxied["X-Upstream-Host"] == "127.0.0.1:" + std::to_string(upstream.port()), "Host port missing");
            require(proxied["X-Content-Type"] == "application/octet-stream", "content type changed");
            require(proxied.count(http::field::set_cookie) == 2, "repeated response headers lost");
        }
        std::cout << "PASS 2 MiB binary upload (fixed and chunked) matches direct\n";
        checkStreaming(port, upstream, false);
        checkStreaming(port, upstream, true);
        for (bool chunked : {false, true}) {
            auto oversized = request(port, "/api/echo", std::string(4 * 1024 * 1024 + 1, 'x'), chunked);
            require(oversized.result() == http::status::payload_too_large, "oversize upload did not return 413");
        }
        std::cout << "PASS configured body limit returns 413\n";
        auto slow = request(port, "/api/delay");
        require(slow.result() == http::status::ok, "configured longer deadline failed");
        auto timedOut = request(port, "/short/delay");
        require(timedOut.result() == http::status::gateway_timeout, "configured short deadline did not return 504");
        std::cout << "PASS configurable deadlines (success and 504)\n";
        require(request(port, "/api/large").body() == std::string(3 * 1024 * 1024, 'r'), "large response corrupted");
        for (const char* path : {"chunked", "eof"}) {
            auto response = request(port, "/api/" + std::string(path));
            require(response.body().find("/v1/" + std::string(path)) == 0, "response framing failed");
        }
        std::cout << "PASS large, chunked and EOF-delimited responses\n";
        {
            asio::io_context io;
            asio::ip::tcp::socket socket(io);
            socket.connect({asio::ip::make_address("127.0.0.1"), port});
            http::request<http::empty_body> head{http::verb::head, "/api/echo", 11};
            head.set(http::field::host, "localhost");
            http::write(socket, head);
            beast::flat_buffer buffer;
            http::response_parser<http::string_body> response;
            response.skip(true);
            http::read(socket, buffer, response);
            require(response.get().result() == http::status::ok && response.get().body().empty(), "HEAD failed");
        }
        require(request(port, "/probe.txt").body() == "static-ok", "static files regressed");
        auto targetNode = n("ve/server/static/config/mounts/#0/proxy/#0/target");
        targetNode->set("http://127.0.0.1:" + std::to_string(upstream.port()) + "/v2");
        require(request(port, "/api/echo").body().find("/v2/echo") == 0, "dynamic target regressed");
        std::cout << "PASS static files and dynamic proxy target\n";
        require(request(port, "/app/backend/echo?x=1").body().find("/echo?x=1") == 0,
                "proxy-only mount or root target path failed");
        // A mount without a root must not serve the working directory through SPA fallback.
        const bool madeIndex = !std::filesystem::exists("index.html");
        if (madeIndex) std::ofstream("index.html") << "cwd-leak";
        auto missing = request(port, "/app/missing");
        if (madeIndex) std::filesystem::remove("index.html");
        require(missing.result() == http::status::not_found, "rootless SPA mount served working directory");
        std::cout << "PASS rootless SPA mount returns 404\n";
        if (argc == 2 && std::string(argv[1]) == "--long-timeout") {
            require(request(port, "/long/long-delay", {}, false, 40s).result() == http::status::ok,
                    "request lasting more than 30 seconds failed");
            std::cout << "PASS 31-second upstream with configured 35-second deadline\n";
        }
        // Stop with both a partial upload and an upstream request still active.
        asio::io_context io;
        asio::ip::tcp::socket upload(io), waiting(io);
        std::size_t beforeBytes;
        int beforeRequests;
        {
            std::lock_guard<std::mutex> lock(upstream.seen.mutex);
            beforeBytes = upstream.seen.bytes;
            beforeRequests = upstream.seen.longRequests;
        }
        for (auto socket : {&upload, &waiting}) socket->connect({asio::ip::make_address("127.0.0.1"), port});
        asio::write(upload, asio::buffer(std::string("POST /long/echo HTTP/1.1\r\nHost: localhost\r\nContent-Length: 99999\r\n\r\na")));
        asio::write(waiting, asio::buffer(std::string("GET /long/long-delay HTTP/1.1\r\nHost: localhost\r\n\r\n")));
        {
            std::unique_lock<std::mutex> lock(upstream.seen.mutex);
            require(upstream.seen.changed.wait_for(lock, 2s, [&] {
                return upstream.seen.bytes > beforeBytes && upstream.seen.longRequests > beforeRequests;
            }), "shutdown test did not start both proxy operations");
        }
        const auto start = std::chrono::steady_clock::now();
        entry::deinit();
        require(std::chrono::steady_clock::now() - start < 3s, "proxy shutdown waited for upstream deadline");
        std::cout << "PASS shutdown cancels active proxy operations\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        result = 1;
    }
    entry::deinit();
    std::filesystem::remove(root / "probe.txt");
    std::filesystem::remove(root);
    return result;
}
