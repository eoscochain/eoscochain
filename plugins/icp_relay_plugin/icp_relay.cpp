#include "icp_relay.hpp"

namespace icp {

listener::listener(boost::asio::io_context& ioc, tcp::endpoint endpoint, relay_ptr relay)
   : acceptor_(ioc), socket_(ioc), relay_(relay) {

   boost::system::error_code ec;

   acceptor_.open(endpoint.protocol(), ec);
   if (ec) {
      log_error(ec, "open");
      return;
   }

   acceptor_.set_option(boost::asio::socket_base::reuse_address(true));

   acceptor_.bind(endpoint, ec);
   if (ec) {
      log_error(ec, "bind");
      return;
   }

   acceptor_.listen(boost::asio::socket_base::max_listen_connections, ec);
   if (ec) {
      log_error(ec, "listen");
      return;
   }

   do_accept();
}

void listener::do_accept() {
   acceptor_.async_accept(socket_, [self=shared_from_this()](auto ec){
      self->on_accept(ec);
   });
}

void listener::on_accept(boost::system::error_code ec) {
   if (ec) {
      if (ec == boost::system::errc::too_many_files_open) {
         do_accept();
      } else {
         log_error(ec, "accept");
      }
      return;
   }

   try {
      auto s = std::make_shared<session>(move(socket_), relay_);
      relay_->async_add_session(s);
   } catch (std::exception& e) {
      socket_.close();
   }

   do_accept();
}

void relay::start() {
   ioc_ = std::make_unique<boost::asio::io_context>(num_threads_);

   start_reconnect_timer();

   auto address = boost::asio::ip::make_address(endpoint_address_);
   listener_ = std::make_shared<listener>(*ioc_, tcp::endpoint{address, endpoint_port_}, shared_from_this());

   socket_threads_.reserve(num_threads_);
   for (auto i = 0; i < num_threads_; ++i) {
      socket_threads_.emplace_back([this, i] {
         wlog("start thread ${i}", ("i", i));
         ioc_->run();
         wlog("stop thread ${i}", ("i", i));
      });
   }

   for (const auto& peer: connect_to_peers_) {
      auto s = std::make_shared<session>(peer, *ioc_, shared_from_this());
      sessions_[s.get()] = s;
   }
}

void relay::stop() {
   try {
      timer_->cancel();
      timer_.reset();
   } catch (...) {
      elog("exception thrown on timer shutdown");
   }

   for_each_session([](auto session) {
      session->close();
   });

   listener_.reset();

   ioc_->stop();

   wlog("joining icp relay threads");
   for (auto& t: socket_threads_) {
      t.join();
   }
   wlog("joined icp relay threads");

   for_each_session([](auto session) {
      EOS_ASSERT(false, plugin_exception, "session ${s} still active", ("s", session->session_id_));
   });
}

void relay::start_reconnect_timer() {
   // add some random delay so that all my peers don't attempt to reconnect to me
   // at the same time after shutting down..
   timer_->expires_from_now(boost::posix_time::microseconds(1000000 * (10 + rand() % 5))); // 10+-5 seconds
   timer_->async_wait([=](const boost::system::error_code& ec) {
      if(ec) {
         log_error(ec, "timer wait");
         return;
      }

      verify_strand_in_this_thread(app().get_io_service().get_executor(), __func__, __LINE__);

      for (const auto& peer: connect_to_peers_) {
         bool found = false;
         for (const auto& s: sessions_ ) {
            auto ses = s.second.lock();
            if( ses && (ses->peer_ == peer) ) {
               found = true;
               break;
            }
         }

         if (not found) {
            wlog("attempt to connect to ${p}", ("p", peer));
            auto s = std::make_shared<session>(peer, *ioc_, shared_from_this());
            sessions_[s.get()] = s;
         }
      }

      start_reconnect_timer();
   });
}

void relay::async_add_session(std::weak_ptr<session> s) {
   app().get_io_service().post([s, this]{
      if (auto l = s.lock()) {
         sessions_[l.get()] = s;
      }
   });
}

void relay::on_session_close(const session* s) {
   verify_strand_in_this_thread(app().get_io_service().get_executor(), __func__, __LINE__);
   auto itr = sessions_.find(s);
   if (itr != sessions_.end()) {
      sessions_.erase(itr);
   }
}

void relay::for_each_session(std::function<void (session_ptr)> callback) {
   app().get_io_service().post([this, callback = callback] {
      for (const auto& item : sessions_) {
         if (auto ses = item.second.lock()) {
            ses->post([ses, callback = callback]() {
               callback(ses);
            });
         }
      }
   });
}

}
