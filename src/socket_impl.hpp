#pragma once

#include <boost/intrusive/list.hpp>
#include <asio_utp/detail/handler.hpp>
#include <asio_utp/detail/signal.hpp>
#include <asio_utp/event.hpp>
#include "intrusive_list.hpp"

namespace asio_utp {
    
class context;
class socket;
class service;
class udp_multiplexer;

class socket_impl : public std::enable_shared_from_this<socket_impl> {
public:
    using endpoint_type = asio::ip::udp::endpoint;
    using on_event_handler = void(socket_event, const sys::error_code&);
    using on_event_connection = Signal<on_event_handler>::Connection;

public:
    socket_impl(const socket_impl&) = delete;
    socket_impl& operator=(const socket_impl&) = delete;

    socket_impl(socket_impl&&) = delete;
    socket_impl& operator=(socket_impl&&) = delete;

    socket_impl(socket*);

    void bind(const endpoint_type&, sys::error_code&);
    void bind(const udp_multiplexer&);

    endpoint_type local_endpoint() const;
    endpoint_type remote_endpoint() const;

    void close();
    on_event_connection on_event(std::function<on_event_handler>);

    bool is_open() const { return _context && !_closed; }

    asio::any_io_executor get_executor()
    {
        return _ex;
    }

    ~socket_impl();

private:
    friend class ::asio_utp::context;
    friend class ::asio_utp::socket;

    void on_connect();
    void on_writable();
    void on_eof();
    void on_destroy();
    void on_accept(void* usocket);
    void on_receive(const unsigned char*, size_t);

    intrusive::list_hook _register_hook;
    intrusive::list_hook _accept_hook;

    void do_write(handler<size_t>);
    void do_read(handler<size_t>);
    void do_connect(const endpoint_type&, handler<>);
    void do_accept(handler<>);

    void close_with_error(const std::error_code&);

    bool is_active() const;

    template<class Handler>
    void setup_op(Handler&, Handler&&, const char* dbg);

    template<class Handler, class... Args>
    void post_op(Handler&, const char* dbg, const sys::error_code&, Args...);

    template<class Handler, class... Args>
    void dispatch_op(Handler&, const char* dbg, const sys::error_code&, Args...);
    void notify_event(socket_event, const sys::error_code& = {});

private:
    asio::any_io_executor _ex;
    service& _service;

    void* _utp_socket = nullptr;
    socket* _owner = nullptr;
    bool _closed = false;
    bool _got_eof = false;

    std::shared_ptr<context> _context;

    handler<> _connect_handler;
    handler<> _accept_handler;
    handler<size_t> _send_handler;
    handler<size_t> _recv_handler;
    Signal<on_event_handler> _event_signal;

    size_t _bytes_sent = 0;
    std::vector<asio::const_buffer> _tx_buffers;

    struct buf_t : public std::vector<unsigned char> {
        using std::vector<unsigned char>::vector;

        size_t consumed = 0;

        operator asio::const_buffer() const {
            assert(consumed <= this->size());
            return asio::const_buffer( this->data() + consumed
                                            , this->size() - consumed);
        }
    };

    // TODO: std::queue is not iterable (required by BufferSequences).
    // Perhaps use something like this?
    // https://stackoverflow.com/a/5984198/273348
    std::vector<buf_t> _rx_buffer_queue;
    std::vector<asio::mutable_buffer> _rx_buffers;

    // This prevents `this` from being destroyed after `socket` is destroyed
    // until libutp destroys `this->_utp_socket` (there is some IO that is done
    // in the mean time, like sending FIN packets and such).
    std::shared_ptr<socket_impl> _self;

#if ASIO_UTP_DEBUG_LOGGING
    bool _debug = true;
#else
    bool _debug = false;
#endif
    uint32_t _debug_id = 0;
};

} // namespace
