#pragma once

#include <memory>
#include <asio/spawn.hpp>

class block {
public:
    block(const asio::any_io_executor&);
    block(const block&) = delete;
    block& operator=(const block&) = delete;

    ~block();

    void release();
    void wait(asio::yield_context yield);

private:
    asio::any_io_executor _ex;
    std::function<void(std::error_code)> _on_notify;
    bool _released = false;
};

inline
block::block(const asio::any_io_executor& ex)
    : _ex(ex)
{}

inline
block::~block()
{
    if (!_on_notify) return;

    asio::post(_ex, [h = std::move(_on_notify)] {
            h(asio::error::operation_aborted);
        });
}

inline
void block::release()
{
    _released = true;

    if (!_on_notify) return;

    asio::post(_ex, [h = std::move(_on_notify)] {
            h(std::error_code());
        });
}

inline
void block::wait(asio::yield_context yield)
{
    namespace system = std;

    if (_released) return;

    asio::async_completion<decltype(yield), void(system::error_code)> c(yield);

    _on_notify = [ h = std::move(c.completion_handler)
                 , w = asio::make_work_guard(_ex)
                 ] (const system::error_code& ec) mutable {
                     h(ec);
                 };

    return c.result.get();
}
