#pragma once

#include <memory>
#include <boost/asio/spawn.hpp>

class block {
public:
    using executor_type = boost::asio::any_io_executor;

    block(const executor_type&);
    block(const block&) = delete;
    block& operator=(const block&) = delete;

    ~block();

    void release();
    void wait(boost::asio::yield_context yield);

private:
    executor_type _ex;
    std::function<void(boost::system::error_code)> _on_notify;
    bool _released = false;
};

inline
block::block(const executor_type& ex)
    : _ex(ex)
{}

inline
block::~block()
{
    if (!_on_notify) return;

    boost::asio::post(_ex, [h = std::move(_on_notify)] {
            h(boost::asio::error::operation_aborted);
        });
}

inline
void block::release()
{
    _released = true;

    if (!_on_notify) return;

    boost::asio::post(_ex, [h = std::move(_on_notify)] {
            h(boost::system::error_code());
        });
}

inline
void block::wait(boost::asio::yield_context yield)
{
    namespace asio   = boost::asio;
    namespace system = boost::system;

    if (_released) return;

    return asio::async_initiate<asio::yield_context, void(system::error_code)>(
        [this](auto&& completion_handler) mutable {
            auto h = std::make_shared<std::decay_t<decltype(completion_handler)>>(
                std::forward<decltype(completion_handler)>(completion_handler));
            _on_notify = [ h = std::move(h)
                         , w = asio::make_work_guard(_ex)
                         ] (const system::error_code& ec) mutable {
                             (*h)(ec);
                         };
        },
        yield);
}
