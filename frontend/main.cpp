#include "L1Pre.h"
#include "L2Pre.h"
#include "LInfPre.h"

#include <cryptoTools/Common/CLP.h>
#include <spdlog/common.h>
#include <spdlog/spdlog.h>

#include <cstdint>
#include <iostream>

namespace {

// Command line usage
void printUsage() {
  std::cout << "Usage:\n"
            << "  ./main [options]\n\n"
            << "Protocol Selection:\n"
            << "  -m <metric>      Distance metric, default: 0\n"
            << "                   0 = L-infinity pre\n"
            << "                   1 = L1 pre\n"
            << "                   2 = L2 pre\n\n"
            << "Protocol Parameters:\n"
            << "  -n <size>        Set size (logarithm), default: 8\n"
            << "                   Input set size = 2^n\n"
            << "                   Supported values: 8, 12, 16\n"
            << "  -d <dim>         L-infinity/L1: 6 or 10; L2: 2\n"
            << "  -delta <value>   Supported values: 10, 60, 250\n"
            << "  -i <size>        target_matching_points, default: 29\n"
            << "  -trait <num>     Number of trials, default: 1\n\n"
            << "Network Configuration:\n"
            << "  -ip <address>    Server IP address, default: 127.0.0.1\n"
            << "  -port <number>   Server port, default: 1212\n\n"
            << "Test and Debug Options:\n"
            << "  -log <level>     Log level, default: 1\n"
            << "                   0 = off\n"
            << "                   1 = info\n"
            << "                   2 = debug\n\n"
            << "Examples:\n"
            << "  ./build/main -m 0 -n 8  -d 6  -delta 10\n"
            << "  ./build/main -m 1 -n 12 -d 10 -delta 60 -trait 3 -detail\n"
            << "  ./build/main -m 2 -n 16 -d 2  -delta 250 -log 0\n";
}

void set_log_level(osuCrypto::CLP &cmd) {
  const uint64_t log_level = cmd.getOr<uint64_t>("log", 1);

  // Same style as opprf_fpsi: print only the message body.
  spdlog::set_pattern("%v");

  switch (log_level) {
  case 0:
    spdlog::set_level(spdlog::level::off);
    break;
  case 1:
    spdlog::set_level(spdlog::level::info);
    break;
  case 2:
    spdlog::set_level(spdlog::level::debug);
    break;
  default:
    spdlog::set_level(spdlog::level::info);
    break;
  }
}

} // namespace

int main(int argc, char **argv) {
  try {
    osuCrypto::CLP cmd;
    cmd.parse(argc, argv);

    if (cmd.isSet("h") || cmd.isSet("help")) {
      printUsage();
      return 0;
    }

    set_log_level(cmd);

    const uint64_t metric = cmd.getOr<uint64_t>("m", 0);

    switch (metric) {
    case 0:
      return run_linf_pre(cmd);
    case 1:
      return run_l1_pre(cmd);
    case 2:
      return run_l2_pre(cmd);
    default:
      spdlog::error("Unknown metric: {}. Use 0, 1 or 2", metric);
      printUsage();
      return 1;
    }
  } catch (const std::exception &e) {
    spdlog::error("{}", e.what());
    return 1;
  }
}
