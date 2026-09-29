// Real transports exercise connection isolation, ordering and command affinity.
#include <ve/entry.h>
#include <ve/core/command.h>
#include <ve/core/pipeline.h>
#include <ve/core/schema.h>
#include <ve/service/bin_service.h>
#include <asio2/asio2.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>

using namespace ve;
using namespace std::chrono_literals;

namespace {

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

struct Gate {
    std::mutex mutex;
    std::condition_variable changed;
    bool open = true;
    int started = 0;
    int finished = 0;
    std::set<Loop*> loops;

    void reset()
    {
        std::lock_guard<std::mutex> lock(mutex);
        open = false;
        started = 0;
        finished = 0;
        loops.clear();
    }

    void release()
    {
        std::lock_guard<std::mutex> lock(mutex);
        open = true;
        changed.notify_all();
    }

    void enter()
    {
        std::unique_lock<std::mutex> lock(mutex);
        ++started;
        loops.insert(loop::current());
        changed.notify_all();
        changed.wait(lock, [this] { return open; });
        ++finished;
        changed.notify_all();
    }

    void waitStarted(int count)
    {
        std::unique_lock<std::mutex> lock(mutex);
        require(changed.wait_for(lock, 3s, [=] { return started >= count; }),
                "commands did not start on independent execution loops");
        require(loops.size() >= static_cast<std::size_t>(count),
                "foreground connections shared an execution loop");
    }

    void waitFinished(int count)
    {
        std::unique_lock<std::mutex> lock(mutex);
        require(changed.wait_for(lock, 3s, [=] { return finished >= count; }),
                "commands did not finish");
    }
};

struct ReleaseGate {
    Gate& gate;
    ~ReleaseGate() { gate.release(); }
};

enum class Wire { Json, Binary, Repl };

template<typename Client, Wire Format = Wire::Json>
class Peer
{
    Client _client;
    std::mutex _mutex;
    std::condition_variable _changed;
    std::string _received;
    Bytes _frames;

    void receive(std::string_view data)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if constexpr (Format == Wire::Binary) {
            _frames.insert(_frames.end(), data.begin(), data.end());
            uint8_t flag;
            Var value;
            while (service::bin::tryPopFrame(_frames, flag, value)) {
                Node node;
                schema::toNode<schema::VarS>(&node, value);
                _received += schema::fromNode<schema::JsonS>(&node, schema::JsonS::compact()) + "\n";
            }
        } else {
            _received.append(data);
        }
        _changed.notify_all();
    }

public:
    explicit Peer(uint16_t port)
    {
        _client.set_connect_timeout(2s);
        _client.set_disconnect_timeout(200ms);
        if constexpr (std::is_same_v<Client, asio2::http_client>) {
            _client.bind_recv([this](http::web_request&, http::web_response& response) {
                receive(response.body().text());
            });
        } else {
            _client.bind_recv([this](std::string_view data) { receive(data); });
        }
        require(_client.start("127.0.0.1", port), "client failed to connect");
    }

    ~Peer() { _client.stop(); }

    void send(const std::string& json)
    {
        if constexpr (std::is_same_v<Client, asio2::http_client>) {
            http::web_request request;
            request.method(http::verb::post);
            request.target("/ve");
            request.set(http::field::host, "localhost");
            request.keep_alive(true);
            request.body() = json;
            request.prepare_payload();
            _client.async_send(std::move(request));
        } else if constexpr (Format == Wire::Binary) {
            Node node;
            schema::toNode<schema::JsonS>(&node, json);
            auto frame = service::bin::encodeFrame(service::bin::FLAG_REQUEST,
                                                   schema::fromNode<schema::VarS>(&node));
            _client.async_send(std::string(frame.begin(), frame.end()));
        } else if constexpr (Format == Wire::Repl) {
            Node node;
            schema::toNode<schema::JsonS>(&node, json);
            auto command = node.get("cmd").toString();
            if (command.empty()) command = "get /probe";
            if (node.get("async").toBool(false)) command = "async " + command;
            _client.async_send(command + "\n");
        } else {
            _client.async_send(json + "\n");
        }
    }

    void expect(const std::string& text)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        if (!_changed.wait_for(lock, 2s, [&] { return _received.find(text) != std::string::npos; }))
            throw std::runtime_error("missing response '" + text + "': " + _received);
        _received.erase(0, _received.find(text) + text.size());
    }

    std::string received()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _received;
    }

    template<typename T = Client>
    std::enable_if_t<std::is_same_v<T, asio2::http_client>> request(
        const std::string& target, const std::string& body = "{}", http::verb method = http::verb::post)
    {
        http::web_request request;
        request.method(method);
        request.target(target);
        request.set(http::field::host, "localhost");
        request.keep_alive(true);
        request.body() = body;
        request.prepare_payload();
        _client.async_send(std::move(request));
    }
};

const std::string slow = R"({"cmd":"_test.service.slow","id":1})";
const std::string get = R"({"op":"get","params":{"path":"probe"},"id":2})";
const std::string background = R"({"cmd":"_test.service.bound","async":true,"id":3})";

template<typename Client, Wire Format = Wire::Json>
void checkTransport(const char* name, uint16_t port, Gate& gate)
{
    std::cout << "CHECK " << name << std::endl;
    // Hold enough foreground commands to cover every shared session worker.
    std::vector<std::unique_ptr<Peer<Client, Format>>> busy;
    gate.reset();
    ReleaseGate release{gate};
    for (int i = 0; i < 3; ++i) {
        busy.push_back(std::make_unique<Peer<Client, Format>>(port));
        busy.back()->send(slow);
    }
    gate.waitStarted(3);
    {
        Peer<Client, Format> probe(port);
        probe.send(get);
        probe.expect("424242");
    }
    // Foreground commands on the same connection must remain ordered.
    busy.front()->send(get);
    gate.release();
    busy.front()->expect("finished");
    busy.front()->expect("424242");
    for (std::size_t i = 1; i < busy.size(); ++i) busy[i]->expect("finished");
    busy.clear();

    // A bound command can run asynchronously while this connection continues.
    gate.reset();
    Peer<Client, Format> async_peer(port);
    async_peer.send(background);
    async_peer.expect("accepted");
    gate.waitStarted(1);
    async_peer.send(get);
    async_peer.expect("424242");
    gate.release();
    gate.waitFinished(1);
    if constexpr (!std::is_same_v<Client, asio2::http_client>) async_peer.expect("finished");

    // An unbound async command uses this connection's execution queue.
    gate.reset();
    async_peer.send(R"({"cmd":"_test.service.slow","async":true,"id":4})");
    async_peer.expect("accepted");
    gate.waitStarted(1);
    {
        Peer<Client, Format> probe(port);
        probe.send(get);
        probe.expect("424242");
    }
    gate.release();
    gate.waitFinished(1);
    if constexpr (!std::is_same_v<Client, asio2::http_client>) async_peer.expect("finished");
    async_peer.send(get);
    async_peer.expect("424242");

    // Disconnect cannot join a long-running command on a transport worker.
    gate.reset();
    {
        Peer<Client, Format> retiring(port);
        retiring.send(slow);
        gate.waitStarted(1);
    }
    {
        Peer<Client, Format> probe(port);
        probe.send(get);
        probe.expect("424242");
    }
    gate.release();
    gate.waitFinished(1);
    std::cout << "PASS " << name << std::endl;
}

void checkStatic(uint16_t port, Gate& gate)
{
    std::cout << "CHECK static proxy" << std::endl;
    std::vector<std::unique_ptr<Peer<asio2::http_client>>> busy;
    gate.reset();
    ReleaseGate release{gate};
    for (int i = 0; i < 3; ++i) {
        busy.push_back(std::make_unique<Peer<asio2::http_client>>(port));
        busy.back()->request("/proxy");
    }
    try {
        gate.waitStarted(3);
    } catch (...) {
        for (auto& peer : busy) std::cerr << "proxy response: " << peer->received() << std::endl;
        throw;
    }
    Peer<asio2::http_client> probe(port);
    probe.request("/probe.txt");
    probe.expect("static-probe-424242");
    gate.release();
    for (auto& peer : busy) peer->expect("finished");
    std::cout << "PASS static proxy" << std::endl;
}

void checkHttpRoutes(uint16_t port, Gate& gate)
{
    std::cout << "CHECK HTTP routes" << std::endl;
    gate.reset();
    ReleaseGate release{gate};
    Peer<asio2::http_client> peer(port);
    peer.request("/cmd/_test.service.bound?async=1", R"({"name":"abc","value":123})");
    peer.expect("accepted");
    gate.waitStarted(1);
    peer.request("/at/probe", "", http::verb::get);
    peer.expect("424242");
    gate.release();
    gate.waitFinished(1);
    std::cout << "PASS HTTP routes" << std::endl;
}

uint16_t unusedPort()
{
    asio::io_context io;
    asio::ip::tcp::acceptor socket(io, {asio::ip::tcp::v4(), 0});
    return socket.local_endpoint().port();
}

} // namespace

int main()
{
    Gate gate;
    AsioLoop command_loop("test.service.command");
    command_loop.start();
    Node options;
    options.set("config_file", "__ve_service_test_absent.json");
    options.set("log/level", "error");
    options.set("modules/ve/core/config/rescue/enabled", false);
    options.set("modules/ve/client/terminal/stdio/enabled", false);
    options.set("modules/ve/client/terminal/tcp/enabled", false);
    options.set("modules/ve/server/terminal/repl/enable", false);
    const char* paths[] = {"node/http", "node/ws", "node/tcp", "node/udp", "bin/tcp", "terminal/ai"};
    std::vector<uint16_t> ports;
    for (const char* path : paths) {
        ports.push_back(unusedPort());
        auto server = options.at("modules/ve/server/" + std::string(path));
        server->set("enable", true);
        server->set("config/port", ports.back());
        server->set("config/max_retry", 0);
    }
    auto static_port = unusedPort();
    auto static_root = std::filesystem::temp_directory_path()
        / ("ve_service_test_" + std::to_string(static_port));
    std::filesystem::create_directory(static_root);
    std::ofstream(static_root / "probe.txt") << "static-probe-424242";
    auto static_config = options.at("modules/ve/server/static");
    static_config->set("enable", true);
    static_config->set("config/port", static_port);
    static_config->set("config/max_retry", 0);
    auto mount = static_config->at("config/mounts")->append();
    mount->set("prefix", "/");
    mount->set("root", static_root.string());
    auto proxy = mount->at("proxy")->append();
    proxy->set("prefix", "/proxy");
    proxy->set("target", "http://127.0.0.1:" + std::to_string(ports[0]) + "/cmd/_test.service.slow");
    entry::setup(&options);
    entry::init();
    n("probe")->set(424242);
    std::atomic<bool> wrong_loop{false};
    auto execute = [&](Node* out) {
        gate.enter();
        out->set("finished", true);
        return Result::ok();
    };
    command::reg("_test.service.slow", [&](Node*, Node*, Node* out) {
        if (!loop::current() || loop::current() == &command_loop) wrong_loop = true;
        return execute(out);
    });
    auto bound = command::reg("_test.service.bound", [&](Node*, Node*, Node* out) {
        if (loop::current() != &command_loop) wrong_loop = true;
        return execute(out);
    });
    bound->set("loop", Var::ptr(static_cast<Loop*>(&command_loop)));

    int result = 0;
    try {
        checkTransport<asio2::http_client>("HTTP", ports[0], gate);
        checkTransport<asio2::ws_client>("WS", ports[1], gate);
        checkTransport<asio2::tcp_client>("TCP", ports[2], gate);
        checkTransport<asio2::udp_client>("UDP peers", ports[3], gate);
        checkTransport<asio2::tcp_client, Wire::Binary>("Bin TCP", ports[4], gate);
        checkTransport<asio2::tcp_client, Wire::Repl>("REPL", ports[5], gate);
        checkHttpRoutes(ports[0], gate);
        checkStatic(static_port, gate);
        require(!wrong_loop.load(), "unbound command executed outside a VE loop");
    } catch (const std::exception& e) {
        std::cerr << "FAIL " << e.what() << std::endl;
        result = 1;
    }
    gate.release();
    entry::deinit();
    bound->remove("loop");
    command_loop.stop();
    std::filesystem::remove_all(static_root);
    return result;
}
