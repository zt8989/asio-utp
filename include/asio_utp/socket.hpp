#pragma once

#include <asio/ip/udp.hpp>
#include <asio/buffers_iterator.hpp>
#include "detail/handler.hpp"

namespace asio_utp {

class socket_impl;
class udp_multiplexer;

class socket {
public:
    using endpoint_type = asio::ip::udp::endpoint;
    using executor_type = asio::io_context::executor_type;

public:
    socket() = default;

    socket(const socket&) = delete;
    socket& operator=(const socket&) = delete;

    socket(socket&&);
    socket& operator=(socket&&);

    socket(const asio::any_io_executor&);
    socket(asio::io_context&);

    void bind(const endpoint_type&, std::error_code&);

    void bind(const udp_multiplexer&, std::error_code&);

    template<typename CompletionToken>
    void async_connect(const endpoint_type&, CompletionToken&&);

    template<typename CompletionToken>
    void async_accept(CompletionToken&&);

    template< typename ConstBufferSequence
            , typename CompletionToken>
    auto async_write_some(const ConstBufferSequence&, CompletionToken&&);

    template< typename MutableBufferSequence
            , typename CompletionToken>
    auto async_read_some(const MutableBufferSequence&, CompletionToken&&);

    endpoint_type local_endpoint() const;

    endpoint_type remote_endpoint() const;

    bool is_open() const;

    void close();

    asio::any_io_executor get_executor()
    {
        return _ex;
    }

    // For debugging only
    void* pimpl() const { return _socket_impl.get(); }

    ~socket();

private:
    void do_connect(const endpoint_type&, handler<>&&);
    void do_accept (handler<>&&);
    void do_write  (handler<size_t>&&);
    void do_read   (handler<size_t>&&);

    std::vector<asio::const_buffer>* tx_buffers();
    std::vector<asio::mutable_buffer>* rx_buffers();

private:
    friend class socket_impl;
    asio::any_io_executor _ex;
    std::shared_ptr<socket_impl> _socket_impl;
};

template<typename CompletionToken>
inline
void socket::async_connect(const endpoint_type& ep, CompletionToken&& token)
{
    asio::async_completion
        <CompletionToken, void(std::error_code)> c(token);

    do_connect(ep, {get_executor(), std::move(c.completion_handler)});

    return c.result.get();
}

template<typename CompletionToken>
inline
void socket::async_accept(CompletionToken&& token)
{
    asio::async_completion
        <CompletionToken, void(std::error_code)> c(token);

    do_accept({get_executor(), std::move(c.completion_handler)});

    return c.result.get();
}

template< typename ConstBufferSequence
        , typename CompletionToken>
inline
auto socket::async_write_some( const ConstBufferSequence& bufs
                             , CompletionToken&& token)
{
    if (auto txb = tx_buffers()) {
        txb->clear();

        std::copy( asio::buffer_sequence_begin(bufs)
                 , asio::buffer_sequence_end(bufs)
                 , std::back_inserter(*txb));
    }

    asio::async_completion
        < CompletionToken
        , void(std::error_code, size_t)
        > c(token);

    do_write({get_executor(), std::move(c.completion_handler)});

    return c.result.get();
}

template< typename MutableBufferSequence
        , typename CompletionToken>
inline
auto socket::async_read_some( const MutableBufferSequence& bufs
                            , CompletionToken&& token)
{
    if (auto rxb = rx_buffers()) {
        rxb->clear();

        std::copy( asio::buffer_sequence_begin(bufs)
                 , asio::buffer_sequence_end(bufs)
                 , std::back_inserter(*rxb));
    }

    asio::async_completion
        < CompletionToken
        , void(std::error_code, size_t)
        > c(token);

    do_read({get_executor(), std::move(c.completion_handler)});

    return c.result.get();
}

} // namespace
