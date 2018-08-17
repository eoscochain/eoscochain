#include "client.hpp"
#include "log.hpp"

namespace cochain {

void client::init(const string& listen_address) {
    resolver_ = make_shared<tcp::resolver>(app().get_io_service());

    auto host = listen_address.substr(0, listen_address.find(':'));
    auto port = listen_address.substr(host.size()+1, listen_address.size());
    idump((host)(port));
    tcp::resolver::query query(tcp::v4(), host.c_str(), port.c_str());
    listen_endpoint_ = *resolver_->resolve(query);

    if (listen_endpoint_.address().to_v4() == address_v4::any()) {
        error_code ec;
        host = host_name(ec);
        if (ec.value() != boost::system::errc::success) {
            FC_THROW_EXCEPTION(fc::invalid_arg_exception, "Unable to retrieve host name: ${msg}", ("msg", ec.message()));
        }
    }
    p2p_address_ = host + ":" + port;

    acceptor_ = make_unique<tcp::acceptor>(app().get_io_service());
}

void client::start() {
    if (!acceptor_) return;

    acceptor_->open(listen_endpoint_.protocol());
    acceptor_->set_option(tcp::acceptor::reuse_address(true));
    acceptor_->bind(listen_endpoint_);
    acceptor_->listen();
    ilog("cochain relay starting listener");
}

void client::stop() {
    try {
        ilog("cochain relay stopping listener and peer connections");
        if (acceptor_) {
            ilog("cochain relay close acceptor");
            acceptor_->close();

            ilog("cochain relay close ${s} connections", ("s", connections_.size()));
            for (auto conn: connections_) {

            }

            acceptor_ = nullptr;
        }
        ilog("cochain relay stopped listener and peer connections");
    } FC_CAPTURE_AND_RETHROW()
}

void client::start_listen_loop() {
    auto socket = make_shared<tcp::socket>(app().get_io_service());
    acceptor_->async_accept(*socket, [this, socket](error_code ec) {
        if (!ec) {
            auto addr = socket->remote_endpoint(ec).address();
            if (ec) {
                elog("cochain relay failed to get remote endpoint: ${msg}", ("msg", ec.message()));
            } else {
                uint32_t visitors = 0;
                uint32_t from_addr = 0;
                for (auto& conn: connections_) {
                    if (conn->socket_->is_open()) {
                        if (conn->peer_addr_.empty()) {
                            ++visitors;
                            if (addr == conn->socket_->remote_endpoint(ec).address()) {
                                ++from_addr;
                            }
                        }
                    }
                }
                if (num_clients_ != visitors) {
                    ilog("cochain relay checking max clients, visitors ${v}", ("v", visitors));
                    num_clients_ = visitors;
                }
                if (from_addr < max_clients_per_host_ && (max_clients_ == 0 || num_clients_ < max_clients_)) {
                    ++num_clients_;
                    auto conn = make_shared<connection>(socket, *this);
                    connections_.insert(conn);
                    start_session(conn);
                } else {
                    socket->close();
                    if (from_addr >= max_clients_per_host_) {
                        elog("cochain relay number of connections (${n}) from ${addr} exceeds limit",
                             ("n", from_addr + 1)("addr", addr.to_string()));
                    } else {
                        elog("cochain relay number of connections (${n}) exceeds limit", ("n", max_clients_));
                    }
                }
            }
        } else {
            elog("cochain relay failed to accept connection: ${msg}", ("msg", ec.message()));
            switch (ec.value()) {
                case ECONNABORTED:
                case EMFILE:
                case ENFILE:
                case ENOBUFS:
                case ENOMEM:
                case EPROTO:
                    break;
                default: // do not listen again for error other than the listed above
                    return;
            }
        }

        start_listen_loop();
    });
}

bool client::start_session(connection_ptr conn) {
    error_code ec;
    conn->socket_->set_option(tcp::no_delay(true), ec);
    if (ec) {
        elog("cochain relay failed to connect ${peer}: ${msg}", ("peer", conn->peer_name())("msg", ec.message()));
        conn->connecting_ = false;
        close_connection(conn);
        return false;
    }

    conn->start_read_message();
    return true;
}

void client::close_connection(connection_ptr conn) {
    if (conn->peer_addr_.empty() && conn->socket_->is_open()) {
        if (num_clients_ > 0) --num_clients_;
        else wlog("cochain relay close connection: num_clients already at 0");
    }
    conn->close();
}

void client::connect(connection_ptr conn) {
    auto colon = conn->peer_addr_.find(':');
    if (colon == std::string::npos || colon == 0) {
        elog("cochain relay invalid peer address: ${p}", ("p", conn->peer_addr_));
        for (auto iter: connections_) {
            if (iter->peer_addr_ == conn->peer_addr_) {
                iter->reset();
                close_connection(conn);
                connections_.erase(iter);
                break;
            }
        }
        return;
    }

    auto host = conn->peer_addr_.substr(0, colon);
    auto port = conn->peer_addr_.substr(colon + 1);
    tcp::resolver::query query(tcp::v4(), host.c_str(), port.c_str());
    connection_wptr weak_conn = conn;
    resolver_->async_resolve(query, [this, weak_conn](const error_code& err, tcp::resolver::iterator endpoint_iter) {
        auto conn = weak_conn.lock();
        if (!conn) return;
        if (!err) connect(conn, endpoint_iter);
        else elog("cochain relay unable to resolve ${p}: ${msg}", ("p", conn->peer_name())("msg", err.message()));
    });
}

void client::connect(connection_ptr conn, tcp::resolver::iterator endpoint_iter ) {
    connection_wptr weak_conn = conn;
    conn->socket_->async_connect(*endpoint_iter, [this, weak_conn, endpoint_iter](const error_code& err) {
        auto conn = weak_conn.lock();
        if (!conn) return;
        if (!err && conn->socket_->is_open()) {
            if (start_session(conn)) {
                conn->send_handshake();
            }
        } else {
            if (++endpoint_iter != tcp::resolver::iterator()) {
                close_connection(conn);
                connect(conn, endpoint_iter);
            } else {
                elog("cochain relay failed to connect ${p}: ${msg}", ("p", conn->peer_name())("msg", err.message()));
                conn->connecting_ = false;
                close_connection(conn);
            }
        }
    });
}

string client::connect(const string& host) {
    for (const auto& conn: connections_) {
        if (conn->peer_addr == host) {
            return "already connected";
        }
    }

    auto conn = make_shared<connection>(host);
    connections_.insert(conn);
}

string client::disconnect(const string& host) {

}

optional<connection_status> client::status(const string& host) const {

}

vector<connection_status> client::connections() const {

}

}
