#include "./FuzzyLInf/FuzzyLInfPre.h"

#include "coproto/Socket/LocalAsyncSock.h"
#include "cryptoTools/Common/CLP.h"
#include "cryptoTools/Common/block.h"
#include "cryptoTools/Crypto/AES.h"
#include "cryptoTools/Crypto/PRNG.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

using sparse_comp::point;

using coproto::LocalAsyncSocket;

using PRNG = osuCrypto::PRNG;
using AES = osuCrypto::AES;
using osuCrypto::block;

using macoro::sync_wait;
using macoro::when_all_ready;

// -----------------------------------------------------------------------------
// Command line usage / 命令行参数说明
// -----------------------------------------------------------------------------
static void printUsage() {
  std::cout
      << "Usage:\n"
      << "  ./main [options]\n\n"
      << "Finf Parameters:\n"
      << "  -n <size>        Set size (logarithm), default: 8\n"
      << "                   Input set size = 2^n\n"
      << "                   Supported values: 8, 12, 16\n"
      << "  -d <dim>         Point dimension, default: 6\n"
      << "                   Supported values: 6, 10\n"
      << "  -delta <value>   L-infinity threshold, default: 10\n"
      << "                   Supported values: 10, 60, 250\n"
      << "  -i <size>        Number of generated matching points, default: 29\n"
      << "  -trait <num>     Number of trials, default: 1\n"
      << "  -detail          Print every trial result\n\n"
      << "Examples:\n"
      << "  ./build/main -n 8  -d 6  -delta 10\n"
      << "  ./build/main -n 12 -d 10 -delta 60 -i 29 -trait 3 -detail\n"
      << "  ./build/main -n 16 -d 6  -delta 250 -trait 1\n";
}

// Generate a random receiver point away from uint32 boundaries.
// 生成接收方随机点，并主动避开 uint32 边界，方便后续在 [-delta, delta]
// 范围内扰动而不发生上溢或下溢。
template <size_t D, uint8_t DELTA> static point genReceiverPoint(PRNG &prng) {
  static_assert(D <= point::MAX_DIM);

  constexpr uint64_t MAX_U32 =
      static_cast<uint64_t>(std::numeric_limits<uint32_t>::max());
  constexpr uint64_t LB = 2ULL * DELTA;
  constexpr uint64_t UB = MAX_U32 - 2ULL * DELTA;
  constexpr uint64_t RANGE = UB - LB + 1;

  uint32_t coords[D];
  for (size_t j = 0; j < D; ++j) {
    coords[j] = static_cast<uint32_t>(
        LB + (static_cast<uint64_t>(prng.get<uint32_t>()) % RANGE));
  }

  return point(D, coords);
}

// Generate a completely random sender point.
// 生成普通的发送方随机点。
template <size_t D> static point genSenderPoint(PRNG &prng) {
  static_assert(D <= point::MAX_DIM);

  uint32_t coords[D];
  for (size_t j = 0; j < D; ++j) {
    coords[j] = prng.get<uint32_t>();
  }

  return point(D, coords);
}

// Move one point inside its L-infinity matching ball.
// 将一个点的每个坐标独立扰动到 [-DELTA, DELTA] 内，从而保证该点
// 与原点之间的 L_inf 距离不超过 DELTA。
template <size_t D, uint8_t DELTA>
static void moveInsideLinfBall(PRNG &prng, point &pt) {
  constexpr uint32_t WIDTH = 2U * DELTA + 1U;

  for (size_t j = 0; j < D; ++j) {
    const int64_t offset = static_cast<int64_t>(prng.get<uint32_t>() % WIDTH) -
                           static_cast<int64_t>(DELTA);

    pt.coords[j] =
        static_cast<uint32_t>(static_cast<int64_t>(pt.coords[j]) + offset);
  }
}

// Fisher-Yates style shuffle using the project's PRNG.
// 使用项目已有 PRNG 打乱数组，避免额外引入随机数实现。
template <typename T, size_t N>
static void shuffleArray(PRNG &prng, std::array<T, N> &arr) {
  for (size_t i = 0; i < N; ++i) {
    const size_t j = prng.get<size_t>() % N;
    std::swap(arr[i], arr[j]);
  }
}

// Generate one pair of Finf inputs.
// 生成一组 Finf 测试输入：
// 1. receiver 全部随机生成；
// 2. sender 前 target_matching_points 个点从 receiver 中复制并做
//    L_inf 范围内扰动；
// 3. 其余 sender 点随机生成；
// 4. 最后打乱 sender 集合。
template <size_t TR, size_t TS, size_t D, uint8_t DELTA>
static void genInputs(block seed, size_t target_matching_points,
                      std::array<point, TR> &receiver_points,
                      std::array<point, TS> &sender_points) {
  if (target_matching_points > std::min(TR, TS)) {
    throw std::invalid_argument(
        "Intersection size must not exceed the input set size.");
  }

  PRNG prng(seed);

  for (size_t i = 0; i < TR; ++i) {
    receiver_points[i] = genReceiverPoint<D, DELTA>(prng);
  }

  // The selected receiver points are distinct because we directly use
  // receiver_points[0 ... target_matching_points-1].
  // 这里直接取 receiver 的前 i 个不同位置，不再额外做随机索引抽样，
  // 对性能测试结果没有影响，同时保持 main 简单。
  for (size_t i = 0; i < target_matching_points; ++i) {
    sender_points[i] = receiver_points[i];
    moveInsideLinfBall<D, DELTA>(prng, sender_points[i]);
  }

  for (size_t i = target_matching_points; i < TS; ++i) {
    sender_points[i] = genSenderPoint<D>(prng);
  }

  shuffleArray(prng, sender_points);
}

// Run one compile-time Finf parameter combination.
// 执行一个固定模板参数组合。Finf 当前把集合大小、维度和 delta 都作为
// 模板参数，因此 main 只能在运行时参数和已编译模板实例之间做映射。
template <size_t TS, size_t TR, size_t D, uint8_t DELTA>
static int runFinf(size_t target_matching_points, size_t trials, bool detail) {
  constexpr uint8_t SSP = 40;

  if (target_matching_points > std::min(TS, TR)) {
    std::cerr << "Error: -i must be <= " << std::min(TS, TR) << ".\n";
    return 1;
  }

  if (trials == 0) {
    std::cerr << "Error: -trait must be greater than 0.\n";
    return 1;
  }

  double total_time_ms = 0.0;
  double total_comm_mib = 0.0;

  std::cout << "Finf benchmark\n"
            << "  sender size   : " << TS << "\n"
            << "  receiver size : " << TR << "\n"
            << "  dimension     : " << D << "\n"
            << "  delta         : " << static_cast<uint32_t>(DELTA) << "\n"
            << "  target matches: " << target_matching_points << "\n"
            << "  trials        : " << trials << "\n\n";

  for (size_t trial = 0; trial < trials; ++trial) {
    // Fixed seeds make benchmark runs reproducible.
    // 使用固定种子保证实验可复现；trial 只对输入种子做轻微变化。
    block input_seed(9536629026107651350ULL + trial,
                     2724119864341290560ULL + trial);

    PRNG sender_prng(
        block(15914074867899273501ULL + trial, 6004108516319388444ULL));
    PRNG receiver_prng(
        block(6427781726132732903ULL + trial, 8471345356057289138ULL));
    AES aes(block(14034463513942181890ULL, 16276202269246990858ULL));

    // Keep large std::array objects on the heap.
    // 大规模参数下 std::array 很大，因此放到堆上，避免栈空间不足。
    auto *sender_points = new std::array<point, TS>();
    auto *receiver_points = new std::array<point, TR>();

    genInputs<TR, TS, D, DELTA>(input_seed, target_matching_points,
                                *receiver_points, *sender_points);

    auto socks = LocalAsyncSocket::makePair();
    std::vector<point> intersection;

    sparse_comp::fuzzy_linf::Sender<TR, TS, D, DELTA, SSP> sender(sender_prng,
                                                                  aes);
    sparse_comp::fuzzy_linf::Receiver<TS, TR, D, DELTA, SSP> receiver(
        receiver_prng, aes);

    auto sender_protocol = sender.send(socks[0], *sender_points);
    auto receiver_protocol =
        receiver.receive(socks[1], *receiver_points, intersection);

    // Only measure the protocol execution itself.
    // 这里只统计协议执行时间；输入生成、对象构造不计入协议时间。
    const auto start = std::chrono::steady_clock::now();

    sync_wait(when_all_ready(std::move(sender_protocol),
                             std::move(receiver_protocol)));

    const auto end = std::chrono::steady_clock::now();

    const double time_ms =
        std::chrono::duration<double, std::milli>(end - start).count();

    // One endpoint's sent + received bytes equals the total data exchanged
    // between the two local parties. Summing both endpoints would double count.
    // 对一个端点统计 sent + received
    // 即为双方总通信量；两个端点再相加会重复计算。
    const double comm_mib =
        static_cast<double>(socks[0].bytesSent() + socks[0].bytesReceived()) /
        1024.0 / 1024.0;

    total_time_ms += time_ms;
    total_comm_mib += comm_mib;

    if (detail) {
      std::cout << "Trial " << (trial + 1) << ": "
                << "time = " << std::fixed << std::setprecision(3) << time_ms
                << " ms, communication = " << comm_mib
                << " MiB, output size = " << intersection.size() << "\n";
    }

    delete sender_points;
    delete receiver_points;
  }

  std::cout << std::fixed << std::setprecision(3)
            << "Average time          : " << total_time_ms / trials << " ms\n"
            << "Average communication : " << total_comm_mib / trials
            << " MiB\n";

  return 0;
}

// The existing benchmark file instantiates d={6,10} and delta={10,60,250}.
// 这里保持同样的参数范围，不为了运行时任意参数再引入额外抽象。
template <size_t N>
static int dispatchFinf(size_t d, uint64_t delta, size_t intersection_size,
                        size_t trials, bool detail) {
  if (d == 6) {
    switch (delta) {
    case 10:
      return runFinf<N, N, 6, 10>(intersection_size, trials, detail);
    case 60:
      return runFinf<N, N, 6, 60>(intersection_size, trials, detail);
    case 250:
      return runFinf<N, N, 6, 250>(intersection_size, trials, detail);
    default:
      break;
    }
  }

  if (d == 10) {
    switch (delta) {
    case 10:
      return runFinf<N, N, 10, 10>(intersection_size, trials, detail);
    case 60:
      return runFinf<N, N, 10, 60>(intersection_size, trials, detail);
    case 250:
      return runFinf<N, N, 10, 250>(intersection_size, trials, detail);
    default:
      break;
    }
  }

  std::cerr << "Error: unsupported (d, delta) = (" << d << ", " << delta
            << ").\n"
            << "Supported d: 6, 10; supported delta: 10, 60, 250.\n";
  return 1;
}

int main(int argc, char **argv) {
  try {
    osuCrypto::CLP cmd;
    cmd.parse(argc, argv);

    if (cmd.isSet("h") || cmd.isSet("help")) {
      printUsage();
      return 0;
    }

    // Same convention as opprf_fpsi:
    // -n means log2(set size), not the actual number of elements.
    // 与 opprf_fpsi 保持一致：-n 表示集合大小的 log2，而不是实际元素个数。
    const uint64_t n = cmd.getOr<uint64_t>("n", 8);
    const uint64_t d = cmd.getOr<uint64_t>("d", 6);
    const uint64_t delta = cmd.getOr<uint64_t>("delta", 10);
    const uint64_t intersection_size = cmd.getOr<uint64_t>("i", 29);
    const uint64_t trials = cmd.getOr<uint64_t>("trait", 1);
    const bool detail = cmd.isSet("detail");

    switch (n) {
    case 8:
      return dispatchFinf<256>(d, delta, intersection_size, trials, detail);
    case 12:
      return dispatchFinf<4096>(d, delta, intersection_size, trials, detail);
    case 16:
      return dispatchFinf<65536>(d, delta, intersection_size, trials, detail);
    default:
      std::cerr << "Error: unsupported -n " << n
                << ". Supported values: 8, 12, 16.\n\n";
      printUsage();
      return 1;
    }
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
}
