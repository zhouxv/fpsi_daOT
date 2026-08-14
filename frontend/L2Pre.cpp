#include "L2Pre.h"

#include "../sparseComp/Common/HashUtils.h"
#include "../sparseComp/FuzzyL2/FuzzyL2Pre.h"

#include <coproto/Socket/AsioSocket.h>
#include <cryptoTools/Common/block.h>
#include <cryptoTools/Crypto/AES.h>
#include <cryptoTools/Crypto/PRNG.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <format>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

using sparse_comp::point;
using PRNG = osuCrypto::PRNG;
using AES = osuCrypto::AES;
using osuCrypto::block;

namespace {

constexpr int64_t MAX_U32_BIT_VAL = 4294967295LL;
constexpr uint8_t ssp = 40;

// Initialize an Asio socket pair. The receiver listens and the sender connects.
// 初始化 Asio socket：receiver 端监听，sender 端连接；不使用 LocalAsyncSocket。
void init_socks(const std::string &ip, uint64_t port,
                vector<coproto::Socket> &receiver_socks,
                vector<coproto::Socket> &sender_socks) {
  const std::string addr = ip + ":" + std::to_string(port);

  std::thread receiver_thread(
      [&]() { receiver_socks.push_back(coproto::asioConnect(addr, true)); });
  std::thread sender_thread(
      [&]() { sender_socks.push_back(coproto::asioConnect(addr, false)); });

  receiver_thread.join();
  sender_thread.join();
}

// Run sender and receiver coroutines together and propagate their exceptions.
// 同时执行 sender/receiver 协程，并把协程中的异常传播到当前线程。
void eval(coproto::task<void> sender_proto,
          coproto::task<void> receiver_proto) {
  auto result = macoro::sync_wait(macoro::when_all_ready(
      std::move(sender_proto), std::move(receiver_proto)));
  std::get<0>(result).result();
  std::get<1>(result).result();
}

template <typename T, size_t n>
void shuffle_array(PRNG &prng, array<T, n> &arr) {
  for (size_t i = 0; i < n; i++) {
    const size_t j = prng.get<size_t>() % n;
    std::swap(arr[i], arr[j]);
  }
}

template <typename T> void shuffle_vector(PRNG &prng, vector<T> &vec) {
  for (size_t i = 0; i < vec.size(); i++) {
    const size_t j = prng.get<size_t>() % vec.size();
    std::swap(vec[i], vec[j]);
  }
}

// Picks n random non-repeating elements in [start, end].
// 从 [start, end] 中随机选择 n 个不重复的位置。
void pick_rand_from_seq(PRNG &prng, size_t start, size_t end, size_t n,
                        vector<size_t> &out) {
  if (start > end || n > end - start + 1) {
    throw std::invalid_argument("pick_rand_from_seq: invalid range");
  }

  vector<size_t> seq;
  seq.reserve(end - start + 1);
  for (size_t i = start; i <= end; i++) {
    seq.push_back(i);
  }

  shuffle_vector(prng, seq);
  out.assign(seq.begin(), seq.begin() + n);
}

template <size_t d> point gen_rand_rcvr_point(PRNG &prng, uint32_t delta) {
  static_assert(d <= point::MAX_DIM);

  const uint32_t lb = 2 * delta;
  const uint32_t ub =
      static_cast<uint32_t>(MAX_U32_BIT_VAL - 2 * static_cast<int64_t>(delta));

  uint32_t c[d];
  for (size_t i = 0; i < d; i++) {
    c[i] = (prng.get<uint32_t>() % (ub - lb + 1)) + lb;
  }
  return point(d, c);
}

template <size_t d> point gen_rand_sndr_point(PRNG &prng) {
  static_assert(d <= point::MAX_DIM);

  uint32_t c[d];
  for (size_t i = 0; i < d; i++) {
    c[i] = prng.get<uint32_t>();
  }
  return point(d, c);
}

// FuzzyL2.bench.cpp keeps receiver points unique by point hash.
// 与 FuzzyL2.bench.cpp 一致，receiver 点按 hash 去重。
template <size_t tr>
void samp_rcvr_sparse_pts(AES &aes, PRNG &prng,
                          array<point, tr> &rcvr_sparse_points,
                          uint32_t delta) {
  std::set<block> existing_pts;

  for (size_t i = 0; i < tr; i++) {
    point pt = gen_rand_rcvr_point<2>(prng, delta);
    block hash = sparse_comp::hash_point(aes, pt);

    while (existing_pts.contains(hash)) {
      pt = gen_rand_rcvr_point<2>(prng, delta);
      hash = sparse_comp::hash_point(aes, pt);
    }

    rcvr_sparse_points[i] = pt;
    existing_pts.insert(hash);
  }
}

template <size_t tr, size_t ts, uint8_t delta>
void pick_rand_rcvr_pts(AES &, PRNG &prng, array<point, tr> &rcvr_sparse_points,
                        array<point, ts> &sndr_sparse_points, size_t n) {
  vector<size_t> rand_idxs(n);
  pick_rand_from_seq(prng, 0, tr - 1, n, rand_idxs);

  for (size_t i = 0; i < n; i++) {
    sndr_sparse_points[i] = rcvr_sparse_points[rand_idxs[i]];
  }
}

// Same L2 matching-point sampling rule as FuzzyL2.bench.cpp.
// 与 FuzzyL2.bench.cpp 保持相同的二维 L2 匹配点采样规则。
template <uint8_t delta>
point samp_rand_l2_matching_ball(PRNG &prng, point &pt) {
  const int64_t max_d =
      static_cast<int64_t>(std::floor(std::sqrt(static_cast<double>(delta))));

  array<uint32_t, 2> ball;

  const int64_t d0_lb =
      std::max<int64_t>(0, static_cast<int64_t>(pt[0]) - max_d);
  const int64_t d0_ub =
      std::min<int64_t>(MAX_U32_BIT_VAL, static_cast<int64_t>(pt[0]) + max_d);
  ball[0] =
      static_cast<uint32_t>(prng.get<uint32_t>() % (d0_ub - d0_lb + 1) + d0_lb);

  const int64_t d0_abs =
      std::abs(static_cast<int64_t>(ball[0]) - static_cast<int64_t>(pt[0]));
  const int64_t d1_max_mag = max_d - d0_abs;

  const int64_t d1_lb =
      std::max<int64_t>(0, static_cast<int64_t>(pt[1]) - d1_max_mag);
  const int64_t d1_ub = std::min<int64_t>(
      MAX_U32_BIT_VAL, static_cast<int64_t>(pt[1]) + d1_max_mag);
  ball[1] =
      static_cast<uint32_t>(prng.get<uint32_t>() % (d1_ub - d1_lb + 1) + d1_lb);

  uint32_t c[2] = {ball[0], ball[1]};
  return point(2, c);
}

template <size_t tr, size_t ts, uint8_t delta>
void smpl_sndr_rand_pts_and_vals(AES &aes, PRNG &prng,
                                 size_t target_matching_points,
                                 array<point, tr> &rcvr_sparse_points,
                                 array<point, ts> &sndr_sparse_points) {
  pick_rand_rcvr_pts<tr, ts, delta>(aes, prng, rcvr_sparse_points,
                                    sndr_sparse_points, target_matching_points);

  for (size_t i = 0; i < target_matching_points; i++) {
    sndr_sparse_points[i] =
        samp_rand_l2_matching_ball<delta>(prng, sndr_sparse_points[i]);
  }

  for (size_t i = target_matching_points; i < ts; i++) {
    sndr_sparse_points[i] = gen_rand_sndr_point<2>(prng);
  }

  shuffle_array<point, ts>(prng, sndr_sparse_points);
}

template <size_t tr, size_t ts, uint8_t delta>
void gen_constrained_rand_inputs(block seed, size_t target_matching_points,
                                 array<point, tr> &rcvr_points,
                                 array<point, ts> &sndr_points) {
  if (target_matching_points > std::min(tr, ts)) {
    throw std::invalid_argument(
        "target_matching_points must not exceed the set size");
  }

  PRNG prng(seed);
  AES aes(prng.get<block>());
  samp_rcvr_sparse_pts<tr>(aes, prng, rcvr_points, delta);
  smpl_sndr_rand_pts_and_vals<tr, ts, delta>(aes, prng, target_matching_points,
                                             rcvr_points, sndr_points);
}

template <size_t TS, size_t TR, size_t D, uint8_t DELTA>
int run_fuzzyl2_pre(size_t target_matching_points, size_t trait,
                    const std::string &ip, uint64_t port, bool detail) {
  static_assert(D == 2, "FuzzyL2 benchmark in this repository uses d=2");

  if (trait == 0) {
    spdlog::error("-trait must be greater than 0");
    return 1;
  }
  if (target_matching_points > std::min(TS, TR)) {
    spdlog::error("-i must not exceed the input set size");
    return 1;
  }

  double offline_com_sum = 0.0;
  double offline_time_sum = 0.0;
  double online_com_sum = 0.0;
  double online_time_sum = 0.0;
  double total_com_sum = 0.0;
  double total_time_sum = 0.0;

  for (size_t trial = 0; trial < trait; trial++) {
    block seed(9536629026107651350ULL + trial, 2724119864341290560ULL + trial);
    PRNG senderPRNG(
        block(15914074867899273501ULL + trial, 6004108516319388444ULL));
    PRNG receiverPRNG(
        block(6427781726132732903ULL + trial, 8471345356057289138ULL));
    AES aes(block(14034463513942181890ULL, 16276202269246990858ULL));

    auto *senderPoints = new std::array<point, TS>();
    auto *receiverPoints = new std::array<point, TR>();
    std::vector<point> intersec;

    gen_constrained_rand_inputs<TR, TS, DELTA>(seed, target_matching_points,
                                               *receiverPoints, *senderPoints);

    vector<coproto::Socket> receiver_socks;
    vector<coproto::Socket> sender_socks;
    init_socks(ip, port, receiver_socks, sender_socks);

    sparse_comp::fuzzy_l2_pre::Sender<TR, TS, D, DELTA, ssp> fuzzyL2Sender(
        senderPRNG, aes);
    sparse_comp::fuzzy_l2_pre::Receiver<TS, TR, D, DELTA, ssp> fuzzyL2Recvr(
        receiverPRNG, aes);

    // Offline phase / 离线阶段
    const auto offline_start = std::chrono::steady_clock::now();
    eval(fuzzyL2Sender.offline(sender_socks[0], *senderPoints),
         fuzzyL2Recvr.offline(receiver_socks[0], *receiverPoints));
    const auto offline_end = std::chrono::steady_clock::now();

    const double offline_time =
        std::chrono::duration<double>(offline_end - offline_start).count();
    const uint64_t offline_bytes =
        receiver_socks[0].bytesReceived() + sender_socks[0].bytesReceived();
    const double offline_com =
        static_cast<double>(offline_bytes) / 1024.0 / 1024.0;

    // Online phase / 在线阶段
    const auto online_start = std::chrono::steady_clock::now();
    eval(fuzzyL2Sender.online(sender_socks[0], *senderPoints),
         fuzzyL2Recvr.online(receiver_socks[0], *receiverPoints, intersec));
    const auto online_end = std::chrono::steady_clock::now();

    const double online_time =
        std::chrono::duration<double>(online_end - online_start).count();
    const uint64_t total_bytes =
        receiver_socks[0].bytesReceived() + sender_socks[0].bytesReceived();
    const uint64_t online_bytes = total_bytes - offline_bytes;
    const double online_com =
        static_cast<double>(online_bytes) / 1024.0 / 1024.0;
    const double total_com = static_cast<double>(total_bytes) / 1024.0 / 1024.0;
    const double total_time = offline_time + online_time;

    offline_com_sum += offline_com;
    offline_time_sum += offline_time;
    online_com_sum += online_com;
    online_time_sum += online_time;
    total_com_sum += total_com;
    total_time_sum += total_time;

    spdlog::info("*********************** Result ****************************");
    spdlog::info("trait                      : {}", trial + 1);
    spdlog::info("Offline communication (MB) : {:.3f}", offline_com);
    spdlog::info("Offline time (s)           : {:.3f}", offline_time);
    spdlog::info("Online communication (MB)  : {:.3f}", online_com);
    spdlog::info("Online time (s)            : {:.3f}", online_time);
    spdlog::info("Total communication (MB)   : {:.3f}", total_com);
    spdlog::info("Total time (s)             : {:.3f}", total_time);
    if (detail) {
      spdlog::info("Intersection size          : {}", intersec.size());
    }
    spdlog::info("***********************************************************");

    delete senderPoints;
    delete receiverPoints;
  }

  const double avg_offline_com = offline_com_sum / trait;
  const double avg_offline_time = offline_time_sum / trait;
  const double avg_online_com = online_com_sum / trait;
  const double avg_online_time = online_time_sum / trait;
  const double avg_total_com = total_com_sum / trait;
  const double avg_total_time = total_time_sum / trait;

  const std::string mertric_str = "2";
  const size_t set_size = TS;
  const size_t dim = D;
  const uint64_t delta = DELTA;

  // Final benchmark row: all six values are averages over trait runs.
  std::cout << fmt::format("[daot_fpsi]  {:^5}  𝐿{}  {:^5}  {:^5}  "
                           "{:^10.3f}  {:^10.3f}  {:^10.3f}  {:^10.3f}  "
                           "{:^10.3f}  {:^10.3f}",
                           set_size, mertric_str, dim, delta, avg_offline_com,
                           avg_offline_time, avg_online_com, avg_online_time,
                           avg_total_com, avg_total_time)
            << std::endl;

  return 0;
}

template <size_t N>
int dispatch_fuzzyl2_pre(size_t d, uint64_t delta,
                         size_t target_matching_points, size_t trait,
                         const std::string &ip, uint64_t port, bool detail) {
  if (d != 2) {
    spdlog::error("L2Pre only supports d=2 in the existing benchmark");
    return 1;
  }

  switch (delta) {
  case 10:
    return run_fuzzyl2_pre<N, N, 2, 10>(target_matching_points, trait, ip, port,
                                        detail);
  case 60:
    return run_fuzzyl2_pre<N, N, 2, 60>(target_matching_points, trait, ip, port,
                                        detail);
  case 250:
    return run_fuzzyl2_pre<N, N, 2, 250>(target_matching_points, trait, ip,
                                         port, detail);
  default:
    spdlog::error("Unsupported L2Pre delta={}", delta);
    return 1;
  }
}

} // namespace

int run_l2_pre(osuCrypto::CLP &cmd) {
  const uint64_t n = cmd.getOr<uint64_t>("n", 8);
  const uint64_t d = cmd.getOr<uint64_t>("d", 2);
  const uint64_t delta = cmd.getOr<uint64_t>("delta", 10);
  const uint64_t target_matching_points = cmd.getOr<uint64_t>("i", 29);
  const uint64_t trait = cmd.getOr<uint64_t>("trait", 1);
  const std::string ip = cmd.getOr<std::string>("ip", "127.0.0.1");
  const uint64_t port = cmd.getOr<uint64_t>("port", 1212);
  const bool detail = cmd.isSet("detail");

  const uint64_t set_size = 1ULL << n;
  spdlog::info("*********************** setting ****************************");
  spdlog::info("set_size          : {}", set_size);
  spdlog::info("dimension         : {}", d);
  spdlog::info("metric            : l_2");
  spdlog::info("delta             : {}", delta);
  spdlog::info("intersection_size : {}", target_matching_points);
  spdlog::info("trait             : {}", trait);
  spdlog::info("protocol          : L2Pre");
  spdlog::info("***********************************************************");

  switch (n) {
  case 8:
    return dispatch_fuzzyl2_pre<256>(d, delta, target_matching_points, trait,
                                     ip, port, detail);
  case 12:
    return dispatch_fuzzyl2_pre<4096>(d, delta, target_matching_points, trait,
                                      ip, port, detail);
  case 16:
    return dispatch_fuzzyl2_pre<65536>(d, delta, target_matching_points, trait,
                                       ip, port, detail);
  default:
    spdlog::error("Unsupported -n {}. Supported values: 8, 12, 16", n);
    return 1;
  }
}
