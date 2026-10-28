// Streaming HTTP transport used only by StaticServer. Node HTTP keeps its own
// message-based transport; upload bodies never enter postHttpRequest().
#pragma once

#include "ve/core/log.h"
#include <asio2/http/http_server.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <limits>
#if defined(ASIO2_ENABLE_SSL) || defined(ASIO2_USE_SSL)
#include <asio/ssl.hpp>
#endif

namespace ve::service::detail {

struct StaticProxyTarget {
    std::string host, port, target;
    bool https = false;
    std::chrono::milliseconds timeout{30000};
};

using StaticRequestParser = http::request_parser<http::buffer_body>;

// Connection can nominate additional hop-by-hop fields. Strip those as well as
// the standard fields, then reconstruct framing for the next connection.
inline void copyProxyHeaders(const http::fields& source, http::fields& target)
{
    for (const auto& field : source) {
        const auto name = field.name_string();
        bool skip = false;
        for (const char* hop : {"host", "connection", "keep-alive", "transfer-encoding",
                               "te", "trailer", "upgrade", "proxy-authorization",
                               "proxy-authenticate", "expect"}) {
            if (asio2::iequals(name, hop)) { skip = true; break; }
        }
        for (auto token : http::token_list(source[http::field::connection])) {
            if (asio2::iequals(name, token)) { skip = true; break; }
        }
        if (!skip) target.insert(name, field.value());
    }
}

class StaticHttpSession : public asio2::http_session_t<StaticHttpSession>
{
    using Super = asio2::http_session_t<StaticHttpSession>;
public:
    using Super::Super;
    uint64_t maxRequestBodyBytes = 64ull * 1024 * 1024;
    std::function<std::optional<StaticProxyTarget>(std::string_view)> resolveProxy;
    std::function<void()> cancelActive;

    void readRequest(std::shared_ptr<StaticRequestParser> parser,
                     std::function<void(asio::error_code, bool)> complete);

    template<typename C>
    void _http_session_post_recv(std::shared_ptr<StaticHttpSession> self,
                                std::shared_ptr<asio2::detail::ecs_t<C>> ecs)
    {
        reading_ = true;
        req_.reset();
        auto parser = std::make_shared<StaticRequestParser>();
        parser->body_limit(maxRequestBodyBytes);
        readRequest(parser, [self, ecs](asio::error_code ec, bool proxied) {
            self->reading_ = false;
            if (ec || !proxied) {
                self->_handle_recv(ec, 0, self, ecs);
            } else if (self->is_started()) {
                self->update_alive_time();
                self->_post_recv(self, ecs);
            }
        });
    }

    template<typename DeferEvent>
    void _post_shutdown(const asio::error_code& ec,
                        std::shared_ptr<StaticHttpSession> self, DeferEvent chain)
    {
        auto cancel = std::move(cancelActive);
        if (cancel) cancel();
        Super::_post_shutdown(ec, std::move(self), std::move(chain));
    }
};

template<typename Stream>
class StaticProxyExchange : public std::enable_shared_from_this<StaticProxyExchange<Stream>>
{
    using Self = StaticProxyExchange<Stream>;
    std::shared_ptr<StaticHttpSession> session_;
    std::shared_ptr<StaticRequestParser> request_;
    StaticProxyTarget target_;
    std::function<void(asio::error_code, bool)> complete_;
    // The SSL context must outlive the stream.
    std::shared_ptr<void> context_;
    Stream upstream_;
    asio::ip::tcp::resolver resolver_;
    asio::steady_timer deadline_;
    beast::flat_buffer upstreamBuffer_{64 * 1024};
    std::array<char, 64 * 1024> chunk_{};
    http::request<http::buffer_body> outgoing_;
    std::optional<http::request_serializer<http::buffer_body>> requestWriter_;
    std::optional<http::response_parser<http::buffer_body>> response_;
    http::response<http::buffer_body> incoming_;
    std::optional<http::response_serializer<http::buffer_body>> responseWriter_;
    bool finished_ = false;
    bool responseStarted_ = false;
    bool keepAlive_ = false;
    std::chrono::steady_clock::duration silenceTimeout_;

    auto self() { return this->shared_from_this(); }

    void closeUpstream()
    {
        asio::error_code ignored;
        resolver_.cancel();
        deadline_.cancel();
        beast::get_lowest_layer(upstream_).close(ignored);
    }

    void done(asio::error_code ec = {})
    {
        closeUpstream();
        auto callback = std::move(complete_);
        session_->cancelActive = {};
        session_->set_silence_timeout(silenceTimeout_);
        if (callback) callback(ec, true);
    }

    void fail(http::status status)
    {
        if (finished_) return;
        finished_ = true;
        closeUpstream();
        asio::error_code ignored;
        session_->socket().cancel(ignored);
        if (responseStarted_ || !session_->is_started()) {
            done(asio::error::operation_aborted);
            return;
        }
        auto reply = std::make_shared<http::response<http::string_body>>(status, 11);
        reply->keep_alive(false);
        reply->set(http::field::content_type, "text/plain");
        reply->body() = std::string(http::obsolete_reason(status));
        reply->prepare_payload();
        http::async_write(session_->stream(), *reply,
            [s = self(), reply](asio::error_code, std::size_t) {
                s->done(asio::error::operation_aborted);
            });
    }

    void writeHeader()
    {
        outgoing_.method(request_->get().method());
        outgoing_.target(target_.target);
        outgoing_.version(11);
        copyProxyHeaders(request_->get().base(), outgoing_.base());
        std::string host = target_.host;
        if (host.find(':') != std::string::npos && host.front() != '[') host = "[" + host + "]";
        host += ":" + target_.port;
        outgoing_.set(http::field::host, host);
        outgoing_.keep_alive(false);
        outgoing_.erase(http::field::content_length);
        if (request_->chunked()) {
            outgoing_.chunked(true);
        } else if (auto length = request_->content_length()) outgoing_.content_length(*length);
        outgoing_.body().more = !request_->is_done();
        requestWriter_.emplace(outgoing_);
        http::async_write_header(upstream_, *requestWriter_,
            [s = self()](asio::error_code ec, std::size_t) {
                if (s->finished_) return;
                if (ec) return s->fail(http::status::bad_gateway);
                if (asio2::iequals(s->request_->get()[http::field::expect], "100-continue")) {
                    static constexpr char reply[] = "HTTP/1.1 100 Continue\r\n\r\n";
                    asio::async_write(s->session_->stream(), asio::buffer(reply, sizeof(reply) - 1),
                        [s](asio::error_code error, std::size_t) {
                            if (s->finished_) return;
                            if (error) return s->fail(http::status::bad_request);
                            s->readBody();
                        });
                } else {
                    s->readBody();
                }
            });
    }

    void readBody()
    {
        if (request_->is_done()) return writeBody(0);
        auto& body = request_->get().body();
        body.data = chunk_.data();
        body.size = chunk_.size();
        http::async_read_some(session_->stream(), session_->buffer().base(), *request_,
            [s = self()](asio::error_code ec, std::size_t) {
                if (s->finished_) return;
                if (ec == http::error::need_buffer) ec = {};
                if (ec) return s->fail(ec == http::error::body_limit
                    ? http::status::payload_too_large : http::status::bad_request);
                s->session_->update_alive_time();
                s->writeBody(s->chunk_.size() - s->request_->get().body().size);
            });
    }

    void writeBody(std::size_t size)
    {
        // Parsing a chunk header/trailer can consume input without producing
        // body bytes. An empty data buffer would serialize a terminal chunk.
        if (size == 0 && !request_->is_done()) return readBody();
        if (requestWriter_->is_done()) return readResponseHeader();
        auto& body = outgoing_.body();
        body.data = size ? chunk_.data() : nullptr;
        body.size = size;
        body.more = !request_->is_done();
        http::async_write(upstream_, *requestWriter_,
            [s = self()](asio::error_code ec, std::size_t) {
                if (s->finished_) return;
                if (ec == http::error::need_buffer) ec = {};
                if (ec) {
                    veLogWs("[static proxy] upstream write:", ec.message());
                    return s->fail(http::status::bad_gateway);
                }
                if (s->requestWriter_->is_done()) s->readResponseHeader();
                else s->readBody();
            });
    }

    void readResponseHeader()
    {
        response_.emplace();
        response_->body_limit((std::numeric_limits<uint64_t>::max)());
        response_->skip(request_->get().method() == http::verb::head);
        http::async_read_header(upstream_, upstreamBuffer_, *response_,
            [s = self()](asio::error_code ec, std::size_t) {
                if (s->finished_) return;
                if (ec) {
                    veLogWs("[static proxy] upstream response:", ec.message());
                    return s->fail(http::status::bad_gateway);
                }
                const auto status = s->response_->get().result_int();
                if (status == 101) return s->fail(http::status::bad_gateway);
                if (status < 200) return s->readResponseHeader();
                s->writeResponseHeader();
            });
    }

    void writeResponseHeader()
    {
        incoming_.result(response_->get().result());
        incoming_.version(request_->get().version());
        copyProxyHeaders(response_->get().base(), incoming_.base());
        incoming_.erase(http::field::content_length);
        if (auto length = response_->content_length()) incoming_.content_length(*length);
        keepAlive_ = request_->get().keep_alive();
        if (!response_->is_done() && !response_->content_length()) {
            if (incoming_.version() >= 11) incoming_.chunked(true);
            else keepAlive_ = false;
        }
        incoming_.keep_alive(keepAlive_);
        incoming_.body().more = !response_->is_done();
        responseWriter_.emplace(incoming_);
        // Once the write starts, another HTTP status cannot safely be sent.
        responseStarted_ = true;
        http::async_write_header(session_->stream(), *responseWriter_,
            [s = self()](asio::error_code ec, std::size_t) {
                if (s->finished_) return;
                if (ec) return s->fail(http::status::bad_gateway);
                if (s->response_->is_done()) return s->finishResponse();
                s->readResponseBody();
            });
    }

    void readResponseBody()
    {
        auto& body = response_->get().body();
        body.data = chunk_.data();
        body.size = chunk_.size();
        http::async_read_some(upstream_, upstreamBuffer_, *response_,
            [s = self()](asio::error_code ec, std::size_t) {
                if (s->finished_) return;
                if (ec == http::error::need_buffer) ec = {};
                if (ec) return s->fail(http::status::bad_gateway);
                s->session_->update_alive_time();
                auto& body = s->incoming_.body();
                body.size = s->chunk_.size() - s->response_->get().body().size;
                if (body.size == 0 && !s->response_->is_done()) return s->readResponseBody();
                body.data = body.size ? s->chunk_.data() : nullptr;
                body.more = !s->response_->is_done();
                http::async_write(s->session_->stream(), *s->responseWriter_,
                    [s](asio::error_code error, std::size_t) {
                        if (s->finished_) return;
                        if (error == http::error::need_buffer) error = {};
                        if (error) return s->fail(http::status::bad_gateway);
                        if (s->responseWriter_->is_done()) s->finishResponse();
                        else s->readResponseBody();
                    });
            });
    }

    void finishResponse()
    {
        finished_ = true;
        done(keepAlive_ ? asio::error_code{} : asio::error::operation_aborted);
    }

public:
    StaticProxyExchange(std::shared_ptr<StaticHttpSession> session,
                        std::shared_ptr<StaticRequestParser> request, StaticProxyTarget target,
                        std::function<void(asio::error_code, bool)> complete,
                        Stream stream, std::shared_ptr<void> context = {})
        : session_(std::move(session)), request_(std::move(request)), target_(std::move(target)),
          complete_(std::move(complete)), context_(std::move(context)), upstream_(std::move(stream)),
          resolver_(session_->io().context()), deadline_(session_->io().context()) {}

    void start()
    {
        // A reused keep-alive connection may have been idle; the silence timeout
        // is measured from last_alive_time, so refresh it for the new exchange.
        session_->update_alive_time();
        silenceTimeout_ = session_->get_silence_timeout();
        // The transport's default idle timeout must not cut off a configured
        // long-running upgrade before the proxy deadline.
        session_->set_silence_timeout((std::max)(silenceTimeout_,
            std::chrono::steady_clock::duration(target_.timeout + std::chrono::seconds(1))));
        std::weak_ptr<Self> weak = self();
        session_->cancelActive = [weak] {
            if (auto s = weak.lock()) {
                if (s->finished_) return;
                s->finished_ = true;
                asio::error_code ignored;
                s->session_->socket().cancel(ignored);
                s->done(asio::error::operation_aborted);
            }
        };
        deadline_.expires_after(target_.timeout);
        deadline_.async_wait([s = self()](asio::error_code ec) {
            if (!ec) s->fail(http::status::gateway_timeout);
        });
        resolver_.async_resolve(target_.host, target_.port,
            [s = self()](asio::error_code ec, auto endpoints) {
                if (s->finished_) return;
                if (ec) return s->fail(http::status::bad_gateway);
                asio::async_connect(beast::get_lowest_layer(s->upstream_), endpoints,
                    [s](asio::error_code error, auto) {
                        if (s->finished_) return;
                        if (error) return s->fail(http::status::bad_gateway);
                        s->handshake();
                    });
            });
    }

    void handshake()
    {
#if defined(ASIO2_ENABLE_SSL) || defined(ASIO2_USE_SSL)
        if constexpr (!std::is_same_v<Stream, asio::ip::tcp::socket>) {
            if (!SSL_set_tlsext_host_name(upstream_.native_handle(), target_.host.c_str()))
                return fail(http::status::bad_gateway);
            upstream_.async_handshake(asio::ssl::stream_base::client,
                [s = self()](asio::error_code ec) {
                    if (s->finished_) return;
                    if (ec) return s->fail(http::status::bad_gateway);
                    s->writeHeader();
                });
        } else
#endif
        {
            writeHeader();
        }
    }
};

// Used for header errors and for static routes, whose request bodies can be
// discarded in bounded chunks rather than materialized as strings.
class StaticRequestRead : public std::enable_shared_from_this<StaticRequestRead>
{
    std::shared_ptr<StaticHttpSession> session_;
    std::shared_ptr<StaticRequestParser> parser_;
    std::function<void(asio::error_code, bool)> complete_;
    std::array<char, 64 * 1024> chunk_{};
public:
    StaticRequestRead(std::shared_ptr<StaticHttpSession> session,
                      std::shared_ptr<StaticRequestParser> parser,
                      std::function<void(asio::error_code, bool)> complete)
        : session_(std::move(session)), parser_(std::move(parser)), complete_(std::move(complete)) {}

    void error(asio::error_code ec)
    {
        if (ec != http::error::body_limit) return complete_(ec, false);
        reply(http::status::payload_too_large, "Request body exceeds max_request_body_bytes");
    }

    void reply(http::status status, std::string text)
    {
        auto reply = std::make_shared<http::response<http::string_body>>(
            status, 11);
        reply->keep_alive(false);
        reply->set(http::field::content_type, "text/plain");
        reply->body() = std::move(text);
        reply->prepare_payload();
        http::async_write(session_->stream(), *reply,
            [s = shared_from_this(), reply](asio::error_code, std::size_t) {
                s->complete_(asio::error::operation_aborted, false);
            });
    }

    void discardBody()
    {
        if (parser_->is_done()) {
            session_->request().base().base() = parser_->get().base();
            return complete_({}, false);
        }
        parser_->get().body().data = chunk_.data();
        parser_->get().body().size = chunk_.size();
        http::async_read_some(session_->stream(), session_->buffer().base(), *parser_,
            [s = shared_from_this()](asio::error_code ec, std::size_t) {
                if (ec == http::error::need_buffer) ec = {};
                if (ec) return s->error(ec);
                s->session_->update_alive_time();
                s->discardBody();
            });
    }

    void start()
    {
        http::async_read_header(session_->stream(), session_->buffer().base(), *parser_,
            [s = shared_from_this()](asio::error_code ec, std::size_t) {
                if (ec) return s->error(ec);
                const auto expect = s->parser_->get()[http::field::expect];
                if (!expect.empty() && !asio2::iequals(expect, "100-continue"))
                    return s->reply(http::status::expectation_failed, "Unsupported Expect header");
                auto target = s->session_->resolveProxy(std::string_view(s->parser_->get().target()));
                if (!target) {
                    if (expect.empty()) return s->discardBody();
                    static constexpr char reply[] = "HTTP/1.1 100 Continue\r\n\r\n";
                    asio::async_write(s->session_->stream(), asio::buffer(reply, sizeof(reply) - 1),
                        [s](asio::error_code error, std::size_t) {
                            if (error) return s->error(error);
                            s->discardBody();
                        });
                    return;
                }
                if (target->https) {
#if defined(ASIO2_ENABLE_SSL) || defined(ASIO2_USE_SSL)
                    auto context = std::make_shared<asio::ssl::context>(asio::ssl::context::tls_client);
                    using Stream = asio::ssl::stream<asio::ip::tcp::socket>;
                    auto exchange = std::make_shared<StaticProxyExchange<Stream>>(
                        s->session_, s->parser_, std::move(*target), std::move(s->complete_),
                        Stream(s->session_->io().context(), *context), context);
                    exchange->start();
#else
                    s->reply(http::status::not_implemented, "HTTPS proxy not supported in this build");
#endif
                } else {
                    auto exchange = std::make_shared<StaticProxyExchange<asio::ip::tcp::socket>>(
                        s->session_, s->parser_, std::move(*target), std::move(s->complete_),
                        asio::ip::tcp::socket(s->session_->io().context()));
                    exchange->start();
                }
            });
    }
};

inline void StaticHttpSession::readRequest(std::shared_ptr<StaticRequestParser> parser,
    std::function<void(asio::error_code, bool)> complete)
{
    std::make_shared<StaticRequestRead>(selfptr(), std::move(parser), std::move(complete))->start();
}

} // namespace ve::service::detail
