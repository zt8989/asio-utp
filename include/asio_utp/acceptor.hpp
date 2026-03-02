#pragma once

#include <asio/ip/udp.hpp>
#include <asio/post.hpp>
#include <asio_utp/socket.hpp>
#include <memory>

namespace asio_utp {

class acceptor {
public:
    using endpoint_type = asio::ip::udp::endpoint;
    using executor_type = asio::io_context::executor_type;

public:
    explicit acceptor(asio::io_context&);
    explicit acceptor(const asio::any_io_executor&);

    void bind(const endpoint_type&, std::error_code&);
    endpoint_type local_endpoint() const;

    bool is_open() const;
    void close();

    asio::any_io_executor get_executor()
    {
        return _ex;
    }

    template<typename CompletionToken>
    auto async_accept(socket& peer, CompletionToken&& token);

private:
    asio::any_io_executor _ex;
    socket _listener;
    endpoint_type _local_endpoint;
};

template<typename CompletionToken>
inline
auto acceptor::async_accept(socket& peer, CompletionToken&& token)
{
    return asio::async_initiate<CompletionToken, void(asio::error_code)>(
        [this, &peer](auto&& completion_handler) mutable {
            using handler_type = std::decay_t<decltype(completion_handler)>;
            auto h = std::make_shared<handler_type>(std::forward<decltype(completion_handler)>(completion_handler));

            if (!_listener.is_open()) {
                return asio::post(get_executor(), [h] { (*h)(asio::error::bad_descriptor); });
            }

            if (peer.is_open()) {
                return asio::post(get_executor(), [h] { (*h)(asio::error::already_open); });
            }

            _listener.do_accept({ get_executor(), [this, &peer, h](const std::error_code& ec) mutable {
                if (ec) {
                    return (*h)(ec);
                }

                peer = std::move(_listener);

                socket next(_ex);
                std::error_code bind_ec;
                next.bind(_local_endpoint, bind_ec);
                _listener = std::move(next);

                (*h)(bind_ec);
            }});
        },
        token);
}

} // namespace asio_utp
