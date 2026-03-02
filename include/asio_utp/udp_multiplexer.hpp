#pragma once

#include <asio/ip/udp.hpp>
#include <asio_utp/detail/handler.hpp>
#include <asio_utp/detail/signal.hpp>

namespace asio_utp {

class udp_multiplexer_impl;
class socket_impl;

class udp_multiplexer {
private:
    struct state;

public:
    using endpoint_type = asio::ip::udp::endpoint;

    using on_send_to_handler = void(
        const std::vector<asio::const_buffer>&,
        size_t,
        const endpoint_type&,
        std::error_code
    );
    using on_send_to_connection = Signal<on_send_to_handler>::Connection;

public:
    udp_multiplexer() = default;

    udp_multiplexer(const udp_multiplexer&) = delete;
    udp_multiplexer& operator=(const udp_multiplexer&) = delete;

    udp_multiplexer(udp_multiplexer&&) = default;
    udp_multiplexer& operator=(udp_multiplexer&&) = default;

    udp_multiplexer(asio::io_context&);
    udp_multiplexer(const asio::any_io_executor&);

    void bind(const endpoint_type& local_endpoint, std::error_code&);
    void bind(const udp_multiplexer&, std::error_code&);

    template< typename MutableBufferSequence
            , typename CompletionToken>
    auto async_receive_from( const MutableBufferSequence&
                           , endpoint_type&
                           , CompletionToken&&);

    template< typename ConstBufferSequence
            , typename CompletionToken>
    auto async_send_to( const ConstBufferSequence&
                      , const endpoint_type& destination
                      , CompletionToken&&);

    on_send_to_connection on_send_to(std::function<on_send_to_handler> handler);

    asio::any_io_executor get_executor()
    {
        return _ex;
    }

    endpoint_type local_endpoint() const;

    bool is_open() const;

    void close(std::error_code&);

    ~udp_multiplexer();

private:
    void do_receive(endpoint_type& ep, handler<size_t>&&);
    void do_send(const endpoint_type& ep, handler<size_t>&&);

    std::vector<asio::mutable_buffer>* rx_buffers();
    std::vector<asio::const_buffer>*   tx_buffers();

    friend class socket_impl;
    std::shared_ptr<udp_multiplexer_impl> impl() const;

private:
    asio::any_io_executor _ex;
    std::shared_ptr<state> _state;
};

template< typename MutableBufferSequence
        , typename CompletionToken>
inline
auto udp_multiplexer::async_receive_from( const MutableBufferSequence& bufs
                                        , endpoint_type& ep
                                        , CompletionToken&& token)
{
    return asio::async_initiate<CompletionToken, void(asio::error_code, size_t)>(
        [this, &bufs, &ep](auto&& completion_handler) mutable {
            if (auto rx_bufs = rx_buffers()) {
                rx_bufs->clear();

                std::copy( asio::buffer_sequence_begin(bufs)
                         , asio::buffer_sequence_end(bufs)
                         , std::back_inserter(*rx_bufs));
            }

            do_receive(ep, { get_executor(), std::forward<decltype(completion_handler)>(completion_handler) });
        },
        token);
}

template< typename ConstBufferSequence
        , typename CompletionToken>
inline
auto udp_multiplexer::async_send_to( const ConstBufferSequence& bufs
                                   , const endpoint_type& destination
                                   , CompletionToken&& token)
{
    return asio::async_initiate<CompletionToken, void(asio::error_code, size_t)>(
        [this, &bufs, destination](auto&& completion_handler) mutable {
            if (auto tx_bufs = tx_buffers()) {
                tx_bufs->clear();

                std::copy( asio::buffer_sequence_begin(bufs)
                         , asio::buffer_sequence_end(bufs)
                         , std::back_inserter(*tx_bufs));
            }

            do_send(destination, { get_executor(), std::forward<decltype(completion_handler)>(completion_handler) });
        },
        token);
}

} // asio_utp
