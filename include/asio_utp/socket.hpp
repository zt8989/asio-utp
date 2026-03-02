#pragma once

#include <boost/asio/ip/udp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/buffers_iterator.hpp>
#include <type_traits>
#include "detail/handler.hpp"

namespace asio_utp {

class socket_impl;
class udp_multiplexer;
class acceptor;

class socket {
public:
    using endpoint_type = boost::asio::ip::udp::endpoint;
    using executor_type = boost::asio::ip::udp::socket::executor_type;
    using wait_type = boost::asio::socket_base::wait_type;
    using shutdown_type = boost::asio::socket_base::shutdown_type;

public:
    socket() = default;

    socket(const socket&) = delete;
    socket& operator=(const socket&) = delete;

    socket(socket&&);
    socket& operator=(socket&&);

    socket(const executor_type&);
    socket(boost::asio::io_context&);

    void bind(const endpoint_type&, boost::system::error_code&);

    void bind(const udp_multiplexer&, boost::system::error_code&);

    template<typename CompletionToken>
    auto async_connect(const endpoint_type&, CompletionToken&&);

    template< typename ConstBufferSequence
            , typename CompletionToken>
    auto async_write_some(const ConstBufferSequence&, CompletionToken&&);

    template< typename MutableBufferSequence
            , typename CompletionToken>
    auto async_read_some(const MutableBufferSequence&, CompletionToken&&);

    template<typename CompletionToken>
    auto async_wait(wait_type, CompletionToken&&);

    endpoint_type local_endpoint() const;

    endpoint_type remote_endpoint() const;

    bool is_open() const;

    void shutdown(shutdown_type, boost::system::error_code&);

    template<typename CompletionToken>
    auto async_shutdown(shutdown_type, CompletionToken&&);

    void close();

    executor_type get_executor()
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
    void do_wait   (wait_type, handler<>&&);

    std::vector<boost::asio::const_buffer>* tx_buffers();
    std::vector<boost::asio::mutable_buffer>* rx_buffers();

private:
    friend class acceptor;
    friend class socket_impl;
    executor_type _ex;
    std::shared_ptr<socket_impl> _socket_impl;
};

template<typename CompletionToken>
inline
auto socket::async_connect(const endpoint_type& ep, CompletionToken&& token)
{
    return boost::asio::async_initiate
        <CompletionToken, void(boost::system::error_code)>(
            [this, ep](auto&& completion_handler) mutable {
                do_connect(ep, {get_executor(),
                                std::forward<decltype(completion_handler)>(
                                    completion_handler)});
            },
            token);
}

template< typename ConstBufferSequence
        , typename CompletionToken>
inline
auto socket::async_write_some( const ConstBufferSequence& bufs
                             , CompletionToken&& token)
{
    if (auto txb = tx_buffers()) {
        txb->clear();

        std::copy( boost::asio::buffer_sequence_begin(bufs)
                 , boost::asio::buffer_sequence_end(bufs)
                 , std::back_inserter(*txb));
    }

    return boost::asio::async_initiate
        < CompletionToken
        , void(boost::system::error_code, size_t)
        >([this](auto&& completion_handler) mutable {
                do_write({get_executor(),
                          std::forward<decltype(completion_handler)>(
                              completion_handler)});
          },
          token);
}

template< typename MutableBufferSequence
        , typename CompletionToken>
inline
auto socket::async_read_some( const MutableBufferSequence& bufs
                            , CompletionToken&& token)
{
    if (auto rxb = rx_buffers()) {
        rxb->clear();

        std::copy( boost::asio::buffer_sequence_begin(bufs)
                 , boost::asio::buffer_sequence_end(bufs)
                 , std::back_inserter(*rxb));
    }

    return boost::asio::async_initiate
        < CompletionToken
        , void(boost::system::error_code, size_t)
        >([this](auto&& completion_handler) mutable {
                do_read({get_executor(),
                         std::forward<decltype(completion_handler)>(
                             completion_handler)});
          },
          token);
}

template<typename CompletionToken>
inline
auto socket::async_wait(wait_type w, CompletionToken&& token)
{
    return boost::asio::async_initiate
        <CompletionToken, void(boost::system::error_code)>(
            [this, w](auto&& completion_handler) mutable {
                do_wait(w, {get_executor(),
                            std::forward<decltype(completion_handler)>(
                                completion_handler)});
            },
            token);
}

template<typename CompletionToken>
inline
auto socket::async_shutdown(shutdown_type how, CompletionToken&& token)
{
    return boost::asio::async_initiate
        <CompletionToken, void(boost::system::error_code)>(
            [this, how](auto&& completion_handler) mutable {
                boost::system::error_code ec;
                shutdown(how, ec);
                boost::asio::post(get_executor(),
                    [h = std::forward<decltype(completion_handler)>(completion_handler), ec]() mutable {
                        h(ec);
                    });
            },
            token);
}

} // namespace
