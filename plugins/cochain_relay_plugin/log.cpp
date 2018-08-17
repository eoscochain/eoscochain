#include "log.hpp"

#include <unordered_map>

namespace fc {
extern std::unordered_map<std::string, logger>& get_logger_map();
}

namespace cochain {

const fc::string logger_name("cochain_relay_plugin");
fc::logger logger;
std::string peer_log_format;

void configure_log() {
    if (fc::get_logger_map().find(logger_name) != fc::get_logger_map().end())
        logger = fc::get_logger_map()[logger_name];
}

}
