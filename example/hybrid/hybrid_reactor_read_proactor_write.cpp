#include <asio.hpp>
#include <asio_utp.hpp>

#include <deque>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace ip = asio::ip;
namespace utp = asio_utp;
using sys_ec = std::error_code;

ip::udp::endpoint parse_endpoint(const std::string& s)
{
    auto pos = s.find(':');
    if (pos == std::string::npos) {
        throw std::runtime_error("endpoint format must be <ip>:<port>");
    }

    auto addr = ip::make_address(s.substr(0, pos));
    auto port = static_cast<uint16_t>(std::stoi(s.substr(pos + 1)));
    return { addr, port };
}

struct peer : std::enable_shared_from_this<peer> {
    explicit peer(utp::socket&& s, bool echo_back)
        : socket(std::move(s))
        , read_buf(64 * 1024)
        , echo(echo_back)
    {}

    void start()
    {
        auto self = shared_from_this();
        conn = socket.on_event([self](utp::socket_event ev, const sys_ec& ec) {
            self->on_event(ev, ec);
        });
        if (!read_inflight && socket.is_open()) {
            start_read();
        }
    }

    void send(std::string data)
    {
        auto self = shared_from_this();
        asio::post(socket.get_executor(), [self, data = std::move(data)]() mutable {
            self->pending_writes.push_back(std::move(data));
            if (!self->write_inflight) {
                self->start_write();
            }
        });
    }

private:
    void on_event(utp::socket_event ev, const sys_ec& ec)
    {
        if (ev == utp::socket_event::connected) {
            std::cerr << "connected\n";
            return;
        }

        if (ev == utp::socket_event::writable || ev == utp::socket_event::readable) {
            return;
        }

        if (ev == utp::socket_event::eof || ev == utp::socket_event::closed) {
            std::cerr << "closed: " << ec.message() << "\n";
            if (socket.is_open()) {
                socket.close();
            }
        }
    }

    void start_read()
    {
        read_inflight = true;
        auto self = shared_from_this();
        socket.async_read_some(asio::buffer(read_buf), [self](const sys_ec& ec, std::size_t n) {
            self->read_inflight = false;
            if (ec) {
                if (self->socket.is_open()) {
                    self->socket.close();
                }
                return;
            }

            std::string msg(self->read_buf.data(), self->read_buf.data() + n);
            std::cerr << "recv(" << n << "): " << msg;
            if (self->echo) {
                self->send(msg);
            }
            if (self->socket.is_open() && !self->read_inflight) {
                self->start_read();
            }
        });
    }

    void start_write()
    {
        if (pending_writes.empty() || !socket.is_open()) {
            write_inflight = false;
            return;
        }

        write_inflight = true;
        auto self = shared_from_this();
        socket.async_write_some(asio::buffer(pending_writes.front()), [self](const sys_ec& ec, std::size_t n) {
            if (ec) {
                if (self->socket.is_open()) {
                    self->socket.close();
                }
                return;
            }

            auto& front = self->pending_writes.front();
            if (n >= front.size()) {
                self->pending_writes.pop_front();
            } else {
                front.erase(0, n);
            }

            self->start_write();
        });
    }

public:
    utp::socket socket;
    std::vector<char> read_buf;
    utp::socket::event_connection conn;
    std::deque<std::string> pending_writes;
    bool read_inflight = false;
    bool write_inflight = false;
    bool echo = false;
};

void run_server(asio::io_context& ioc, const ip::udp::endpoint& ep)
{
    auto listener = std::make_shared<utp::socket>(ioc);
    sys_ec ec;
    listener->bind(ep, ec);
    if (ec) {
        throw std::runtime_error("bind failed: " + ec.message());
    }

    std::cerr << "listening on " << listener->local_endpoint() << "\n";

    listener->async_accept([listener](const sys_ec& aec) mutable {
        if (aec) {
            std::cerr << "accept failed: " << aec.message() << "\n";
            return;
        }

        std::cerr << "accepted\n";
        auto p = std::make_shared<peer>(std::move(*listener), true);
        p->start();
    });
}

void run_client(asio::io_context& ioc, const ip::udp::endpoint& remote)
{
    auto p = std::make_shared<peer>(utp::socket(ioc), false);
    sys_ec ec;
    p->socket.bind({ ip::address_v4::loopback(), 0 }, ec);
    if (ec) {
        throw std::runtime_error("bind failed: " + ec.message());
    }

    p->socket.async_connect(remote, [&, p](const sys_ec& cec) {
        if (cec) {
            std::cerr << "connect failed: " << cec.message() << "\n";
            return;
        }

        p->start();

        auto timer = std::make_shared<asio::steady_timer>(ioc);
        auto count = std::make_shared<int>(0);

        std::shared_ptr<std::function<void()>> tick = std::make_shared<std::function<void()>>();
        *tick = [p, timer, count, tick]() {
            if (*count >= 5 || !p->socket.is_open()) {
                if (p->socket.is_open()) {
                    p->socket.close();
                }
                return;
            }

            p->send("ping " + std::to_string(*count) + "\n");
            ++(*count);
            timer->expires_after(std::chrono::seconds(1));
            timer->async_wait([tick](const sys_ec& tec) {
                if (!tec) {
                    (*tick)();
                }
            });
        };

        (*tick)();
    });
}

int main(int argc, const char** argv)
{
    if (argc != 3) {
        std::cerr << "Usage:\n"
                  << "  " << argv[0] << " s <ip:port>\n"
                  << "  " << argv[0] << " c <ip:port>\n";
        return 1;
    }

    asio::io_context ioc;

    try {
        auto ep = parse_endpoint(argv[2]);
        std::string mode = argv[1];
        if (mode == "s") {
            run_server(ioc, ep);
        } else if (mode == "c") {
            run_client(ioc, ep);
        } else {
            std::cerr << "mode must be 's' or 'c'\n";
            return 1;
        }

        ioc.run();
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
