#include <boost/asio.hpp>
#include <asio_utp.hpp>

#include <deque>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace asio = boost::asio;
namespace ip = asio::ip;
namespace utp = asio_utp;
using sys_ec = boost::system::error_code;

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
        wait_read();
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
    void wait_read()
    {
        auto self = shared_from_this();
        socket.async_wait(asio::socket_base::wait_read, [self](const sys_ec& ec) {
            if (ec) {
                std::cerr << "closed: " << ec.message() << "\n";
                if (self->socket.is_open()) {
                    self->socket.close();
                }
                return;
            }
            if (!self->read_inflight && self->socket.is_open()) {
                self->start_read();
            }
        });
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
            if (self->socket.is_open()) {
                self->wait_read();
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
    std::deque<std::string> pending_writes;
    bool read_inflight = false;
    bool write_inflight = false;
    bool echo = false;
};

void run_server(asio::io_context& ioc, const ip::udp::endpoint& ep)
{
    struct server_state {
        explicit server_state(asio::io_context& ioc_)
            : acceptor(ioc_)
            , pending_socket(ioc_)
        {}

        utp::acceptor acceptor;
        utp::socket pending_socket;
        std::shared_ptr<peer> active_peer;
    };

    auto st = std::make_shared<server_state>(ioc);
    sys_ec ec;
    st->acceptor.bind(ep, ec);
    if (ec) {
        throw std::runtime_error("bind failed: " + ec.message());
    }

    std::cerr << "listening on " << st->acceptor.local_endpoint() << "\n";

    st->acceptor.async_accept(st->pending_socket, [st](const sys_ec& aec) mutable {
        if (aec) {
            std::cerr << "accept failed: " << aec.message() << "\n";
            return;
        }

        std::cerr << "accepted\n";
        st->active_peer = std::make_shared<peer>(std::move(st->pending_socket), true);
        st->active_peer->start();
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
