#pragma once

#include <boost/asio/ip/udp.hpp>
#include <boost/asio/post.hpp>
#include <asio_utp/socket.hpp>
#include <memory>
#include <type_traits>

namespace asio_utp {

class acceptor {
public:
    using endpoint_type = boost::asio::ip::udp::endpoint;
    using executor_type = socket::executor_type;

public:
    explicit acceptor(boost::asio::io_context&);
    explicit acceptor(const executor_type&);

    void bind(const endpoint_type&, boost::system::error_code&);
    endpoint_type local_endpoint() const;

    bool is_open() const;
    void close();

    executor_type get_executor()
    {
        return _ex;
    }

    template<typename CompletionToken>
    auto async_accept(socket& peer, CompletionToken&& token);

private:
    executor_type _ex;
    endpoint_type _local_endpoint;
    socket* _pending_accept_socket = nullptr;
    bool _is_open = false;
};

template<typename CompletionToken>
inline
auto acceptor::async_accept(socket& peer, CompletionToken&& token)
{
    return boost::asio::async_initiate<CompletionToken, void(boost::system::error_code)>(
        [this, &peer](auto&& completion_handler) mutable {
            using handler_type = std::decay_t<decltype(completion_handler)>;
            auto h = std::make_shared<handler_type>(
                std::forward<decltype(completion_handler)>(completion_handler));

            if (!_is_open) {
                return boost::asio::post(get_executor(), [h] { (*h)(boost::asio::error::bad_descriptor); });
            }

            if (peer.is_open()) {
                return boost::asio::post(get_executor(), [h] { (*h)(boost::asio::error::already_open); });
            }

            boost::system::error_code bind_ec;
            peer.bind(_local_endpoint, bind_ec);
            if (bind_ec) {
                return boost::asio::post(get_executor(), [h, bind_ec] { (*h)(bind_ec); });
            }

            _pending_accept_socket = &peer;
            peer.do_accept({ get_executor(), [this, h](const boost::system::error_code& ec) mutable {
                _pending_accept_socket = nullptr;
                (*h)(ec);
            }});
        },
        token);
}

} // namespace asio_utp
