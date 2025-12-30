#include "../sparseComp/FuzzyL2/FuzzyL2.h"
#include "../sparseComp/Common/Common.h"
#include "../sparseComp/Common/HashUtils.h"
#include "catch2/benchmark/catch_benchmark.hpp"
#include "catch2/catch_test_macros.hpp"
#include "coproto/Socket/LocalAsyncSock.h"
#include "cryptoTools/Common/block.h"
#include "cryptoTools/Crypto/AES.h"
#include "cryptoTools/Crypto/PRNG.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

using sparse_comp::point;

using coproto::LocalAsyncSocket;

using PRNG = osuCrypto::PRNG;
using osuCrypto::block;
using AES = osuCrypto::AES;

using sparse_comp::hash_point;
using std::chrono::high_resolution_clock;
using std::chrono::milliseconds;

using std::abs;
using std::array;
using std::max;
using std::min;
using std::set;
using std::unordered_map;

using macoro::sync_wait;
using macoro::when_all_ready;

static const int64_t MAX_8_BIT_VAL = 255;
static const int64_t MAX_U32_BIT_VAL = 4294967295;

template <typename T, size_t n>
static void shuffle_array(PRNG &prng, array<T, n> &arr) {

  for (size_t i = 0; i < n; i++) {
    size_t j = prng.get<size_t>() % n;
    std::swap(arr[i], arr[j]);
  }
}

template <typename T> static void shuffle_vector(PRNG &prng, vector<T> &vec) {

  for (size_t i = 0; i < vec.size(); i++) {
    size_t j = prng.get<size_t>() % vec.size();
    std::swap(vec[i], vec[j]);
  }
}

template <typename T1, typename T2, size_t n>
static void shuffle_array_pair(PRNG &prng, array<T1, n> &arr1,
                               array<T2, n> &arr2) {

  for (size_t i = 0; i < n; i++) {
    size_t j = prng.get<size_t>() % n;
    std::swap(arr1[i], arr1[j]);
    std::swap(arr2[i], arr2[j]);
  }
}

template <size_t d> static point gen_rand_point(PRNG &prng) {
  assert(d <= point::MAX_DIM);

  uint32_t c[d];
  for (size_t i = 0; i < d; i++) {
    c[i] = prng.get<uint32_t>();
  }

  return point(d, c);
}

template <size_t d>
static point gen_rand_rcvr_point(PRNG &prng, uint32_t delta) {
  assert(d <= point::MAX_DIM);

  uint32_t lb = 2 * delta;
  uint32_t ub = MAX_U32_BIT_VAL - 2 * delta;

  uint32_t c[d];
  for (size_t i = 0; i < d; i++) {
    c[i] = (prng.get<uint32_t>() % (ub - lb + 1)) + lb;
  }

  return point(d, c);
}

template <size_t tr>
static void samp_rcvr_sparse_pts(AES &aes, PRNG &prng,
                                 array<point, tr> &rcvr_sparse_points,
                                 uint32_t delta) {

  set<block> existing_pts;

  for (size_t i = 0; i < tr; i++) {
    point pt = gen_rand_rcvr_point<2>(prng, delta);
    block hash = hash_point(aes, pt);

    while (existing_pts.contains(hash)) {
      pt = gen_rand_rcvr_point<2>(prng, delta);
      hash = hash_point(aes, pt);
    }

    rcvr_sparse_points[i] = pt;
    existing_pts.insert(hash);
  }
}

template <size_t tr>
static void samp_rcvr_in_vals(PRNG &prng,
                              array<array<uint32_t, 2>, tr> &rcvr_in_vals) {

  for (size_t i = 0; i < tr; i++) {
    for (size_t j = 0; j < 2; j++) {
      rcvr_in_vals[i][j] = prng.get<uint8_t>();
    }
  }
}

// Picks n random non repeating element in the range [start,end].
static void pick_rand_from_seq(PRNG &prng, size_t start, size_t end, size_t n,
                               vector<size_t> &out) {
  assert(start <= end);
  assert(n <= (end - start + 1));

  std::vector<size_t> seq;

  for (size_t i = start; i <= end; i++) {
    seq.push_back(i);
  }

  shuffle_vector(prng, seq);

  out.resize(n);

  for (size_t i = 0; i < n; i++)
    out[i] = seq[i];
}

/* Picks n random non repeating receiver points and values and puts them
   in the beginning of the respective sender output arrays*/
template <size_t tr, size_t ts, uint8_t delta>
static void pick_rand_rcvr_pts(AES &aes, PRNG &prng,
                               array<point, tr> &rcvr_sparse_points,
                               array<point, ts> &sndr_sparse_points, size_t n) {
  vector<size_t> rand_idxs(n);
  pick_rand_from_seq(prng, 0, tr - 1, n, rand_idxs);

  for (size_t i = 0; i < n; i++) {
    sndr_sparse_points[i] = rcvr_sparse_points[rand_idxs[i]];
  }
}

template <uint8_t delta>
static array<uint32_t, 2> smpl_sndr_rand_val(PRNG &prng) {
  array<uint32_t, 2> pt;

  for (size_t i = 0; i < 2; i++) {
    pt[i] = prng.get<uint8_t>();
  }

  return pt;
}

/*
static point samp_rand_l2_matching_ball_for_delta_10(PRNG &prng, point &pt) {
  int64_t max_d = 3;

  array<uint32_t, 2> ball;

  int64_t d0_lb = max((int64_t)0, (int64_t)pt[0] - (int64_t)max_d);
  int64_t d0_ub =
      min((int64_t)MAX_U32_BIT_VAL, (int64_t)pt[0] + (int64_t)max_d);

  ball[0] = prng.get<uint32_t>() % (d0_ub - d0_lb + 1) + d0_lb;
  int64_t d0_abs = abs((int64_t)ball[0] - (int64_t)pt[0]);
  int64_t d1_max_mag = max_d - d0_abs;

  int64_t d1_lb = max((int64_t)0, (int64_t)pt[1] - d1_max_mag);
  int64_t d1_ub = min((int64_t)MAX_U32_BIT_VAL, (int64_t)pt[1] + d1_max_mag);

  ball[1] = prng.get<uint32_t>() % (d1_ub - d1_lb + 1) + d1_lb;

  return point((uint32_t)ball[0], (uint32_t)ball[1]);
}
*/

/*
static point samp_rand_l2_matching_ball_for_delta_30(PRNG &prng, point &pt) {
  int64_t max_d = 5;

  array<uint32_t, 2> ball;

  int64_t d0_lb = max((int64_t)0, (int64_t)pt[0] - (int64_t)max_d);
  int64_t d0_ub =
      min((int64_t)MAX_U32_BIT_VAL, (int64_t)pt[0] + (int64_t)max_d);

  ball[0] = prng.get<uint32_t>() % (d0_ub - d0_lb + 1) + d0_lb;
  int64_t d0_abs = abs((int64_t)ball[0] - (int64_t)pt[0]);
  int64_t d1_max_mag = max_d - d0_abs;

  int64_t d1_lb = max((int64_t)0, (int64_t)pt[1] - d1_max_mag);
  int64_t d1_ub = min((int64_t)MAX_U32_BIT_VAL, (int64_t)pt[1] + d1_max_mag);

  ball[1] = prng.get<uint32_t>() % (d1_ub - d1_lb + 1) + d1_lb;

  return point((uint32_t)ball[0], (uint32_t)ball[1]);
}
*/

template <uint8_t delta>
static point samp_rand_l2_matching_ball(PRNG &prng, point &pt) {
  constexpr int64_t max_d = static_cast<int64_t>(std::floor(std::sqrt(delta)));
  array<uint32_t, 2> ball;

  int64_t d0_lb = max((int64_t)0, (int64_t)pt[0] - (int64_t)max_d);
  int64_t d0_ub =
      min((int64_t)MAX_U32_BIT_VAL, (int64_t)pt[0] + (int64_t)max_d);

  ball[0] = prng.get<uint32_t>() % (d0_ub - d0_lb + 1) + d0_lb;
  int64_t d0_abs = abs((int64_t)ball[0] - (int64_t)pt[0]);
  int64_t d1_max_mag = max_d - d0_abs;

  int64_t d1_lb = max((int64_t)0, (int64_t)pt[1] - d1_max_mag);
  int64_t d1_ub = min((int64_t)MAX_U32_BIT_VAL, (int64_t)pt[1] + d1_max_mag);

  ball[1] = prng.get<uint32_t>() % (d1_ub - d1_lb + 1) + d1_lb;

  return point((uint32_t)ball[0], (uint32_t)ball[1]);
}

template <size_t d> static point gen_rand_sndr_point(PRNG &prng) {
  assert(d <= point::MAX_DIM);

  uint32_t c[d];
  for (size_t i = 0; i < d; i++) {
    c[i] = prng.get<uint32_t>();
  }

  return point(d, c);
}

template <size_t tr, size_t ts, uint8_t delta>
static void smpl_sndr_rand_pts_and_vals(AES &aes, PRNG &prng,
                                        size_t target_num_matching_pts,
                                        array<point, tr> &rcvr_sparse_points,
                                        array<point, ts> &sndr_sparse_points) {
  // Copies min_num_matching_random bins from rcvr_sparse_points to beginning
  // of sndr_sparse_points.
  pick_rand_rcvr_pts<tr, ts, delta>(aes, prng, rcvr_sparse_points,
                                    sndr_sparse_points,
                                    target_num_matching_pts);

  // Randomizes min_num_matching_pts first points in sndr_in_values such that
  // they are within delta of the corresponding points in rcvr_in_values.
  for (size_t i = 0; i < target_num_matching_pts; i++) {
    // std::cout << "sndr_sparse_points[" << i << "] = ("
    //           << sndr_sparse_points[i][0] << ", "
    //           << sndr_sparse_points[i][1] << ")" << std::endl;

    sndr_sparse_points[i] =
        samp_rand_l2_matching_ball<delta>(prng, sndr_sparse_points[i]);

    // std::cout << "sndr_sparse_points[" << i << "] = ("
    //           << sndr_sparse_points[i][0] << ", "
    //           << sndr_sparse_points[i][1] << ")" << std::endl;
  }

  // Randomizes the remaining points in sndr_in_values copied picked randomly
  // from rcvr_in_values.
  for (size_t i = target_num_matching_pts; i < ts; i++) {
    sndr_sparse_points[i] = gen_rand_sndr_point<2>(prng);
  }

  shuffle_array<point, ts>(prng, sndr_sparse_points);
}

template <size_t tr, size_t ts, uint8_t delta>
static void gen_constrained_rand_inputs(block seed,
                                        size_t target_num_matching_pts,
                                        array<point, tr> &rcvr_points,
                                        array<point, ts> &sndr_points) {
  assert(target_num_matching_pts <= ts && target_num_matching_pts <= tr);

  PRNG prng(seed);
  AES aes(prng.get<block>());

  samp_rcvr_sparse_pts<tr>(aes, prng, rcvr_points, delta);

  smpl_sndr_rand_pts_and_vals<tr, ts, delta>(aes, prng, target_num_matching_pts,
                                             rcvr_points, sndr_points);
}

static bool is_l2_close(point &pt, point &ball_center, uint8_t delta) {
  int64_t delta_sq = ((int64_t)delta) * ((int64_t)delta);
  int64_t d_abs[2] = {(int64_t)pt[0] - (int64_t)ball_center[0],
                      (int64_t)pt[1] - (int64_t)ball_center[1]};

  return (d_abs[0] * d_abs[0] + d_abs[1] * d_abs[1]) <= delta_sq;
}

template <size_t tr, size_t ts, uint8_t delta>
static void expected_l2_intersect(AES &aes, array<point, tr> &rcvr_points,
                                  array<point, ts> &sndr_points,
                                  vector<point> &intersec) {
  constexpr const size_t d = 2;
  constexpr const size_t twotod = ((size_t)pow(2, d));
  constexpr const size_t recvr_cell_count = ((size_t)pow(2, d)) * tr;

  unordered_map<block, size_t> rcvr_pts_hashes;

  std::vector<block> rcvr_stcell_hashes;
  std::vector<block> sndr_st_hashes;

  sparse_comp::spatial_cell_hash<tr, d, recvr_cell_count>(
      aes, rcvr_points, rcvr_stcell_hashes, delta);
  sparse_comp::spatial_hash<ts>(aes, sndr_points, sndr_st_hashes, d, delta);

  for (size_t i = 0; i < tr; i++) {
    for (size_t j = 0; j < twotod; j++) {
      rcvr_pts_hashes.insert(
          std::make_pair(rcvr_stcell_hashes[twotod * i + j], i));
    }
  }

  for (size_t i = 0; i < ts; i++) {
    block hash = sndr_st_hashes[i];
    if (rcvr_pts_hashes.contains(hash)) {
      size_t r_idx = rcvr_pts_hashes[hash];

      if (is_l2_close(rcvr_points[r_idx], sndr_points[i], delta)) {
        intersec.push_back(sndr_points[i]);
      }
    }
  }
}

template <size_t tr, size_t ts>
static void intersec_from_z_shares(array<array<block, 1>, tr> &rcvr_z_shares,
                                   array<array<block, 1>, ts> &sndr_z_shares,
                                   set<size_t> &intersec_idxs) {

  unordered_map<block, size_t> share_to_idx_map;

  for (size_t i = 0; i < ts; i++) {
    share_to_idx_map.insert(std::make_pair(sndr_z_shares[i][0], i));
  }

  for (size_t i = 0; i < tr; i++) {
    if (share_to_idx_map.contains(rcvr_z_shares[i][0])) {
      intersec_idxs.insert(i);
    }
  }
}

bool is_intersec_correct(AES &aes, std::vector<point> &intersec,
                         std::vector<point> &expected_intersec) {

  if (intersec.size() != expected_intersec.size()) {
    return false;
  }

  unordered_map<block, bool> intersec_map;

  for (size_t i = 0; i < intersec.size(); i++) {
    intersec_map.insert(
        std::make_pair(sparse_comp::hash_point(aes, intersec[i]), true));
  }

  for (size_t i = 0; i < expected_intersec.size(); i++) {

    if (!intersec_map.contains(
            sparse_comp::hash_point(aes, expected_intersec[i]))) {

      return false;
    }
  }

  return true;
}

#define GENERATE_FUZZYL2_TEST(n, m, d, delta)                                  \
  TEST_CASE("fuzzyl2 (n=m=" #n " d=" #d " delta=" #delta ")",                  \
            "[fuzzyl2][n=m=" #n "]") {                                         \
    BENCHMARK_ADVANCED("n=m=" #n " d=" #d " delta=" #delta)(                   \
        Catch::Benchmark::Chronometer meter) {                                 \
      constexpr size_t TS = n;                                                 \
      constexpr size_t TR = m;                                                 \
      constexpr size_t D = d;                                                  \
      constexpr size_t DELTA = delta;                                          \
      constexpr size_t ssp = 40;                                               \
      size_t target_matching_points = 29;                                      \
                                                                               \
      auto socks = LocalAsyncSocket::makePair();                               \
      block seed = block(9536629026107651350ULL, 2724119864341290560ULL);      \
      PRNG senderPRNG =                                                        \
          PRNG(block(15914074867899273501ULL, 6004108516319388444ULL));        \
      PRNG receiverPRNG =                                                      \
          PRNG(block(6427781726132732903ULL, 8471345356057289138ULL));         \
      AES aes = AES(block(14034463513942181890ULL, 16276202269246990858ULL));  \
                                                                               \
      std::array<point, TS> *senderPoints = new std::array<point, TS>();       \
      std::array<point, TR> *receiverPoints = new std::array<point, TR>();     \
      std::vector<point> intersec;                                             \
                                                                               \
      gen_constrained_rand_inputs<TR, TS, DELTA>(                              \
          seed, target_matching_points, *receiverPoints, *senderPoints);       \
                                                                               \
      sparse_comp::fuzzy_l2::Sender<TR, TS, D, DELTA, ssp> fuzzyL2Sender(      \
          senderPRNG, aes);                                                    \
      sparse_comp::fuzzy_l2::Receiver<TS, TR, D, DELTA, ssp> fuzzyL2Recvr(     \
          receiverPRNG, aes);                                                  \
                                                                               \
      auto sender_proto = fuzzyL2Sender.send(socks[0], *senderPoints);         \
      auto receiver_proto =                                                    \
          fuzzyL2Recvr.receive(socks[1], *receiverPoints, intersec);           \
                                                                               \
      meter.measure([&sender_proto, &receiver_proto]() {                       \
        sync_wait(when_all_ready(std::move(sender_proto),                      \
                                 std::move(receiver_proto)));                  \
      });                                                                      \
                                                                               \
      std::vector<point> expected_intersec;                                    \
                                                                               \
      expected_l2_intersect<TR, TS, DELTA>(aes, *receiverPoints,               \
                                           *senderPoints, expected_intersec);  \
                                                                               \
      delete senderPoints;                                                     \
      delete receiverPoints;                                                   \
                                                                               \
      const double nMBsExchanged =                                             \
          ((double)(socks[0].bytesSent() + socks[0].bytesReceived())) /        \
          1024.0 / 1024.0;                                                     \
                                                                               \
      static bool communication_printed = false;                               \
      if (!communication_printed) {                                            \
        SUCCEED("Number of MBs exchanged: " << nMBsExchanged << " MB");        \
        communication_printed = true;                                          \
      }                                                                        \
    };                                                                         \
  }

GENERATE_FUZZYL2_TEST(256, 256, 2, 10)
GENERATE_FUZZYL2_TEST(256, 256, 2, 60)
GENERATE_FUZZYL2_TEST(256, 256, 2, 250)

GENERATE_FUZZYL2_TEST(4096, 4096, 2, 10)
GENERATE_FUZZYL2_TEST(4096, 4096, 2, 60)
GENERATE_FUZZYL2_TEST(4096, 4096, 2, 250)

GENERATE_FUZZYL2_TEST(65536, 65536, 2, 10)
GENERATE_FUZZYL2_TEST(65536, 65536, 2, 60)
GENERATE_FUZZYL2_TEST(65536, 65536, 2, 250)