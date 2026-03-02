#pragma once

#include <memory>
#include <asio/spawn.hpp>
#include <asio/steady_timer.hpp>

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
    std::shared_ptr<asio::steady_timer> _timer;
    bool _released = false;
};

inline
block::block(const asio::any_io_executor& ex)
    : _ex(ex)
{}

inline
block::~block()
{
    release();
}

inline
void block::release()
{
    _released = true;

    if (!_timer) return;

    asio::post(_ex, [timer = _timer] {
        timer->cancel();
    });
}

inline
void block::wait(asio::yield_context yield)
{
    if (_released) return;

    _timer = std::make_shared<asio::steady_timer>(_ex);
    _timer->expires_at((asio::steady_timer::time_point::max)());

    std::error_code ec;
    _timer->async_wait(yield[ec]);
    _timer.reset();
}
