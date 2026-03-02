#pragma once

namespace asio_utp {

enum class socket_event {
    connected,
    writable,
    readable,
    eof,
    closed,
};

} // namespace asio_utp
