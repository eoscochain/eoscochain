#include "connection.hpp"

#include <boost/asio/read.hpp>

#include <fc/io/raw.hpp>
#include <fc/exception/exception.hpp>

#include "client.hpp"
#include "protocol.hpp"

namespace cochain {

connection::connection(socket_ptr socket, client& client) : socket_(socket), client_(client) {}

connection::~connection() {}

string connection::peer_name() {
    if (not last_handshake_recv_.p2p_address.empty()) return last_handshake_recv_.p2p_address;
    if (not peer_addr_.empty()) return peer_addr_;
    return "connecting client";
}

void connection::start_read_message() {
    if (!socket_) return;

    connection_wptr weak_conn = shared_from_this();

    size_t minimum_read = outstanding_read_bytes_ ? *outstanding_read_bytes_ : message_header_size;

    auto completion_handler = [minimum_read](error_code ec, size_t bytes_transferred) -> size_t {
        if (ec || bytes_transferred >= minimum_read) return 0;
        else return minimum_read - bytes_transferred;
    };

    boost::asio::async_read(socket_, pending_message_buffer_.get_buffer_sequence_for_boost_async_read(), completion_handler, [this, weak_conn](error_code ec, size_t bytes_transferred) {
        auto conn = weak_conn.lock();
        if (!conn) return;

        outstanding_read_bytes_.reset();

        try {
            if (!ec) {
                FC_ASSERT(bytes_transferred <= pending_message_buffer_.bytes_to_write(), "cochain relay connection async_read callback: bytes_transferred ${bt}, buffer bytes to write ${btw}", ("bt", bytes_transferred)("btw", pending_message_buffer_.bytes_to_write()));

                pending_message_buffer_.advance_write_ptr(bytes_transferred);
                auto bytes_in_buffer = pending_message_buffer_.bytes_to_read();
                while (bytes_in_buffer > 0) {
                    if (bytes_in_buffer < message_header_size) {
                        outstanding_read_bytes_.emplace(message_header_size - bytes_in_buffer);
                        break;
                    } else {
                        uint32_t msg_length;
                        auto index = pending_message_buffer_.read_index();
                        pending_message_buffer_.peek(&msg_length, sizeof(msg_length), index);
                        if (msg_length > default_send_buffer_size*2 || msg_length == 0) {
                            elog("cochain relay unexpected incoming message length (${len})", ("len", msg_length));
                            client_.close_connection(shared_from_this());
                            return;
                        }

                        auto total_msg_bytes = msg_length + message_header_size;
                        if (bytes_in_buffer >= total_msg_bytes) {
                            pending_message_buffer_.advance_read_ptr(message_header_size);
                            if (!process_next_message(msg_length)) {
                                return; // invalid message, so closed connection
                            }
                        } else {
                            auto outstanding_msg_bytes = total_msg_bytes - bytes_in_buffer;
                            auto available_buffer_bytes = pending_message_buffer_.bytes_to_write();
                            if (outstanding_msg_bytes > available_buffer_bytes) {
                                pending_message_buffer_.add_space(outstanding_msg_bytes - available_buffer_bytes);
                            }

                            outstanding_read_bytes_.emplace(outstanding_msg_bytes);
                            break;
                        }
                    }

                    bytes_in_buffer = pending_message_buffer_.bytes_to_read();
                }

                start_read_message();
            } else {
                auto pname = peer_name();
                if (ec.value() != boost::asio::error::eof) {
                    elog("cochain relay failed to read message from ${pname}: ${msg}", ("pname", pname)("msg", ec.message()));
                } else {
                    ilog("cochain relay peer ${pname} closed connection", ("pname", pname));
                }
                client_.close_connection(shared_from_this());
            }
        } catch (const fc::exception& ex) {
            elog("cochain relay exception in handling read message from ${pname}: ${msg}", ("pname", peer_name())("msg", ex.to_string()));
            client_.close_connection(shared_from_this());
        } catch (const std::exception& ex) {
            elog("cochain relay exception in handling read message from ${pname}: ${msg}", ("pname", peer_name())("msg", ex.what()));
            client_.close_connection(shared_from_this());
        } catch (...) {
            elog("cochain relay undefined exception in handling read message from ${pname}", ("pname", peer_name()));
            client_.close_connection(shared_from_this());
        }
    });
}

struct msgHandler : public fc::visitor<void> {
    connection_ptr conn_;

    msgHandler(connection_ptr conn) : conn_(conn) {}

    template <typename T>
    void operator()(const T& msg) const {
        conn_->handle_message(msg);
    }
};

bool connection::process_next_message(uint32_t msg_length) {
    try {
        auto stream = pending_message_buffer_.create_datastream();
        net_message msg;
        fc::raw::unpack(stream, msg);
        msgHandler handler(shared_from_this());
        msg.visit(handler);
        return true;
    } catch (const fc::exception& e) {
        elog("cochain relay process next message: ${msg}", ("msg", e.to_detail_string()));
        client_.close_connection(shared_from_this());
        return false;
    }
}

void connection::handle_message(const handshake_message& msg) {
}

}
