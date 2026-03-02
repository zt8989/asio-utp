#include <asio_utp/acceptor.hpp>

using namespace asio_utp;

acceptor::acceptor(asio::io_context& ioc)
    : _ex(ioc.get_executor())
{}

acceptor::acceptor(const asio::any_io_executor& ex)
    : _ex(ex)
{}

void acceptor::bind(const endpoint_type& ep, std::error_code& ec)
{
    if (_is_open) {
        ec = asio::error::already_open;
        return;
    }

    socket probe(_ex);
    probe.bind(ep, ec);
    if (ec) return;
    _local_endpoint = probe.local_endpoint();
    probe.close();
    _is_open = true;
}

acceptor::endpoint_type acceptor::local_endpoint() const
{
    return _local_endpoint;
}

bool acceptor::is_open() const
{
    return _is_open;
}

void acceptor::close()
{
    _is_open = false;
    if (_pending_accept_socket) {
        _pending_accept_socket->close();
        _pending_accept_socket = nullptr;
    }
}
