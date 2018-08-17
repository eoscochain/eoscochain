#pragma once

#include <memory>

#include <boost/asio/ip/tcp.hpp>
#include <fc/network/message_buffer.hpp>

#include "protocol.hpp"

namespace cochain {

using namespace std;

using boost::asio::ip::tcp;

using socket_ptr = shared_ptr<tcp::socket>;

constexpr auto message_header_size = sizeof(uint32_t); // 4
constexpr auto default_send_buffer_size = 1024*1024*4; // 4MB

class client;

class connection : public enable_shared_from_this<connection> {
public:
    connection(socket_ptr socket, client& client);
    ~connection();

    string peer_name();
    void close();

    void start_read_message();
    bool process_next_message(uint32_t msg_length);

    client& client_;
    socket_ptr socket_;
    string peer_addr_;
    bool connecting_;

    fc::message_buffer<1024 * 1024> pending_message_buffer_;
    fc::optional<std::size_t> outstanding_read_bytes_;

private:
    void handle_message(const handshake_message& msg);

    handshake_message last_handshake_recv_;
};

using connection_ptr = shared_ptr<connection>;
using connection_wptr = weak_ptr<connection>;

}
