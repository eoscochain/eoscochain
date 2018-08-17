#pragma once

#include <string>

#include <fc/log/logger.hpp>

namespace cochain {
    extern fc::logger logger;
    extern std::string peer_log_format;

    void configure_log();
}

#define peer_dlog( PEER, FORMAT, ... ) \
  FC_MULTILINE_MACRO_BEGIN \
   if( cochain::logger.is_enabled( fc::log_level::debug ) ) \
      cochain::logger.log( FC_LOG_MESSAGE( debug, cochain::peer_log_format + FORMAT, __VA_ARGS__ (PEER->get_logger_variant()) ) ); \
  FC_MULTILINE_MACRO_END

#define peer_ilog( PEER, FORMAT, ... ) \
  FC_MULTILINE_MACRO_BEGIN \
   if( cochain::logger.is_enabled( fc::log_level::info ) ) \
      cochain::logger.log( FC_LOG_MESSAGE( info, cochain::peer_log_format + FORMAT, __VA_ARGS__ (PEER->get_logger_variant()) ) ); \
  FC_MULTILINE_MACRO_END

#define peer_wlog( PEER, FORMAT, ... ) \
  FC_MULTILINE_MACRO_BEGIN \
   if( cochain::logger.is_enabled( fc::log_level::warn ) ) \
      cochain::logger.log( FC_LOG_MESSAGE( warn, cochain::peer_log_format + FORMAT, __VA_ARGS__ (PEER->get_logger_variant()) ) ); \
  FC_MULTILINE_MACRO_END

#define peer_elog( PEER, FORMAT, ... ) \
  FC_MULTILINE_MACRO_BEGIN \
   if( cochain::logger.is_enabled( fc::log_level::error ) ) \
      cochain::logger.log( FC_LOG_MESSAGE( error, cochain::peer_log_format + FORMAT, __VA_ARGS__ (PEER->get_logger_variant())) ); \
  FC_MULTILINE_MACRO_END
