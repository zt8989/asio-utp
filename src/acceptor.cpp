#include <asio_utp/acceptor.hpp>

using namespace asio_utp;

acceptor::acceptor(asio::io_context& ioc)
    : _ex(ioc.get_executor())
    , _listener(_ex)
{}

acceptor::acceptor(const asio::any_io_executor& ex)
    : _ex(ex)
    , _listener(_ex)
{}

void acceptor::bind(const endpoint_type& ep, std::error_code& ec)
{
    if (_listener.is_open()) {
        ec = asio::error::already_open;
        return;
    }

    _listener.bind(ep, ec);
    if (ec) return;
    _local_endpoint = _listener.local_endpoint();
}

acceptor::endpoint_type acceptor::local_endpoint() const
{
    return _listener.local_endpoint();
}

bool acceptor::is_open() const
{
    return _listener.is_open();
}

void acceptor::close()
{
    _listener.close();
}
