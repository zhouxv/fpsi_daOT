#include "../sparseComp/FuzzyLInf/FuzzyLInf.h"
#include "../sparseComp/Common/Common.h"
#include "../sparseComp/Common/HashUtils.h"
#include "catch2/benchmark/catch_benchmark.hpp"
#include "catch2/catch_test_macros.hpp"
#include "coproto/Socket/LocalAsyncSock.h"
#include "cryptoTools/Common/block.h"
#include "cryptoTools/Crypto/AES.h"
#include "cryptoTools/Crypto/PRNG.h"
#include <array>
#include <catch2/catch_message.hpp>
#include <cmath>
#include <cstdint>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <coproto/Socket/AsioSocket.h>
#include <coproto/Socket/LocalAsyncSock.h>

#include <thread>

using sparse_comp::point;

using coproto::LocalAsyncSocket;

using PRNG = osuCrypto::PRNG;
using osuCrypto::block;
using AES = osuCrypto::AES;

using std::set;
using std::unordered_map;

using macoro::sync_wait;
using macoro::when_all_ready;

using sparse_comp::spatial_hash;

static const int64_t MAX_U8_BIT_VAL = 255;
static const int64_t MAX_U32_BIT_VAL = 4294967295;

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

template <size_t d> static point gen_rand_sndr_point(PRNG &prng) {
  assert(d <= point::MAX_DIM);

  uint32_t c[d];
  for (size_t i = 0; i < d; i++) {
    c[i] = prng.get<uint32_t>();
  }

  return point(d, c);
}

template <size_t tr, size_t d>
static void samp_rcvr_pts(AES &aes, PRNG &prng, array<point, tr> &rcvr_points,
                          uint8_t delta) {
  assert(d <= point::MAX_DIM);

  for (size_t i = 0; i < tr; i++) {
    rcvr_points[i] = gen_rand_rcvr_point<d>(prng, delta);
  }
}

template <typename T> static void shuffle_vector(PRNG &prng, vector<T> &vec) {

  for (size_t i = 0; i < vec.size(); i++) {
    size_t j = prng.get<size_t>() % vec.size();
    std::swap(vec[i], vec[j]);
  }
}

template <typename T, size_t n>
static void shuffle_array(PRNG &prng, array<T, n> &arr) {

  for (size_t i = 0; i < n; i++) {
    size_t j = prng.get<size_t>() % n;
    std::swap(arr[i], arr[j]);
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
template <size_t tr, size_t ts, size_t d, uint8_t delta>
static void pick_rand_rcvr_pts(AES &aes, PRNG &prng,
                               array<point, tr> &rcvr_points,
                               array<point, ts> &sndr_points, size_t n) {
  vector<size_t> rand_idxs(n);
  pick_rand_from_seq(prng, 0, tr - 1, n, rand_idxs);

  for (size_t i = 0; i < n; i++) {
    sndr_points[i] = rcvr_points[rand_idxs[i]];
  }
}

template <size_t d, uint8_t delta>
static void samp_rand_linf_matching_ball(PRNG &prng, point &pt) {

  for (size_t i = 0; i < d; i++) {
    uint32_t lb = std::max((int64_t)0, ((int64_t)pt[i]) - ((int64_t)delta));
    uint32_t ub =
        std::min(MAX_U32_BIT_VAL, ((int64_t)pt[i]) + ((int64_t)delta));

    pt.coords[i] = (prng.get<uint32_t>() % (ub - lb + 1)) + lb;
  }
}

template <size_t d, uint8_t delta>
static void make_ball_almost_matching(PRNG &prng, point &pt) {
  int64_t int64_delta = (int64_t)delta;

  for (size_t i = 0; i < d; i++) {
    int64_t mod256_pt_i = ((int64_t)pt[i]) % (MAX_U8_BIT_VAL + 1);

    if (mod256_pt_i - delta - 1 < 0) {
      pt.coords[i] = MAX_U8_BIT_VAL;
    } else {
      pt.coords[i] = 0;
    }
  }
}

template <size_t tr, size_t ts, size_t d, uint8_t delta>
static void smpl_sndr_rand_pts(AES &aes, PRNG &prng,
                               size_t target_num_matching_pts,
                               array<point, tr> &rcvr_points,
                               array<point, ts> &sndr_points) {
  assert(target_num_matching_pts <= ts && target_num_matching_pts <= tr);
  assert(d <= point::MAX_DIM);

  // Copies target_num_matching_pts from rcvr_points to beginning of
  // sndr_points.
  pick_rand_rcvr_pts<tr, ts, d, delta>(aes, prng, rcvr_points, sndr_points,
                                       target_num_matching_pts);

  // Randomizes target_num_matching_pts first points in sndr_points such that
  // they are within delta of the corresponding points in rcvr_points.
  for (size_t i = 0; i < target_num_matching_pts; i++) {
    samp_rand_linf_matching_ball<d, delta>(prng, sndr_points[i]);
  }

  // Samples the rest of the points in sndr_points such that they are not within
  // delta of the corresponding points in rcvr_points.
  for (size_t i = target_num_matching_pts; i < ts; i++) {
    point pt = gen_rand_sndr_point<d>(prng);
    sndr_points[i] = pt;
  }

  shuffle_array<point, ts>(prng, sndr_points);
}

template <size_t tr, size_t ts, size_t d, uint8_t delta>
static void gen_constrained_rand_inputs(block seed,
                                        size_t target_num_matching_pts,
                                        array<point, tr> &rcvr_points,
                                        array<point, ts> &sndr_points) {
  assert(target_num_matching_pts <= ts && target_num_matching_pts <= tr);

  PRNG prng(seed);
  AES aes(prng.get<block>());

  samp_rcvr_pts<tr, d>(aes, prng, rcvr_points, delta);

  smpl_sndr_rand_pts<tr, ts, d, delta>(aes, prng, target_num_matching_pts,
                                       rcvr_points, sndr_points);
}

template <size_t d>
inline static bool is_linf_close(point &pt, point &ball_center, int64_t delta) {

  for (size_t i = 0; i < d; i++) {
    int64_t dist = std::abs((int64_t)pt[i] - (int64_t)ball_center[i]);

    if (dist > delta) {
      return false;
    }
  }

  return true;
}

template <size_t tr, size_t ts, size_t d, uint8_t delta>
static void expected_linf_intersect(AES &aes, array<point, tr> &rcvr_points,
                                    array<point, ts> &sndr_points,
                                    vector<point> &intersec) {
  constexpr const size_t twotod = ((size_t)pow(2, d));
  constexpr const size_t recvr_cell_count = ((size_t)pow(2, d)) * tr;

  unordered_map<block, size_t> rcvr_pts_hashes;

  vector<block> rcvr_stcell_hashes(recvr_cell_count);
  vector<block> sndr_st_hashes(ts);

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

      if (is_linf_close<d>(rcvr_points[r_idx], sndr_points[i], delta)) {
        intersec.push_back(sndr_points[i]);
      }
    }
  }
}

template <size_t tr, size_t ts, size_t d, uint8_t delta>
static void safer_expected_linf_intersect(AES &aes,
                                          array<point, tr> &rcvr_points,
                                          array<point, ts> &sndr_points,
                                          vector<point> &intersec) {
  int64_t int64_delta = (int64_t)delta;

  for (size_t i = 0; i < ts; i++) {
    for (size_t j = 0; j < tr; j++) {
      bool close = true;

      for (size_t k = 0; k < d; k++) {
        int64_t dist =
            std::abs((int64_t)sndr_points[i][k] - (int64_t)rcvr_points[j][k]);

        if (dist > int64_delta) {
          close = false;
          break;
        }
      }

      if (close) {
        intersec.push_back(sndr_points[i]);
        break;
      }
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

#define GENERATE_FUZZYLINF_TEST(n, m, d, delta)                                \
  TEST_CASE("fuzzylinf(n=" #n " m=" #m " d=" #d " delta=" #delta ")",          \
            "[fuzzylinf][n" #n ",m" #m ",d" #d ",delta" #delta "]") {          \
    BENCHMARK_ADVANCED("[fuzzylinf][n" #n ",m" #m ",d" #d ",delta" #delta "]") \
    (Catch::Benchmark::Chronometer meter) {                                    \
      constexpr size_t TS = n;                                                 \
      constexpr size_t TR = m;                                                 \
      constexpr size_t D = d;                                                  \
      constexpr size_t DELTA = delta;                                          \
      constexpr size_t ssp = 40;                                               \
      size_t target_matching_points = 29;                                      \
      auto socks = LocalAsyncSocket::makePair();                               \
      block seed = block(9536629026107651350ULL, 2724119864341290560ULL);      \
      PRNG senderPRNG =                                                        \
          PRNG(block(15914074867899273501ULL, 6004108516319388444ULL));        \
      PRNG receiverPRNG =                                                      \
          PRNG(block(6427781726132732903ULL, 8471345356057289138ULL));         \
      AES aes = AES(block(14034463513942181890ULL, 16276202269246990858ULL));  \
      std::array<point, TS> *senderPoints = new std::array<point, TS>();       \
      std::array<point, TR> *receiverPoints = new std::array<point, TR>();     \
      std::vector<point> intersec;                                             \
      gen_constrained_rand_inputs<TR, TS, D, DELTA>(                           \
          seed, target_matching_points, *receiverPoints, *senderPoints);       \
      sparse_comp::fuzzy_linf::Sender<TR, TS, D, DELTA, ssp> fuzzyLinfSender(  \
          senderPRNG, aes);                                                    \
      sparse_comp::fuzzy_linf::Receiver<TS, TR, D, DELTA, ssp> fuzzyLinfRecvr( \
          receiverPRNG, aes);                                                  \
      auto sender_proto = fuzzyLinfSender.send(socks[0], *senderPoints);       \
      auto receiver_proto =                                                    \
          fuzzyLinfRecvr.receive(socks[1], *receiverPoints, intersec);         \
      meter.measure([&sender_proto, &receiver_proto]() {                       \
        sync_wait(when_all_ready(std::move(sender_proto),                      \
                                 std::move(receiver_proto)));                  \
      });                                                                      \
      std::vector<point> expected_intersec;                                    \
      expected_linf_intersect<TR, TS, D, DELTA>(                               \
          aes, *receiverPoints, *senderPoints, expected_intersec);             \
      delete senderPoints;                                                     \
      delete receiverPoints;                                                   \
      const double nMBsExchanged =                                             \
          ((double)(socks[0].bytesSent() + socks[0].bytesReceived())) /        \
          1024.0 / 1024.0;                                                     \
      static bool communication_printed = false;                               \
      if (!communication_printed) {                                            \
        SUCCEED("Number of MBs exchanged: " << nMBsExchanged << " MB");        \
        communication_printed = true;                                          \
      }                                                                        \
    };                                                                         \
  }

GENERATE_FUZZYLINF_TEST(256, 256, 6, 10)
GENERATE_FUZZYLINF_TEST(256, 256, 6, 60)
GENERATE_FUZZYLINF_TEST(256, 256, 6, 250)

GENERATE_FUZZYLINF_TEST(256, 256, 10, 10)
GENERATE_FUZZYLINF_TEST(256, 256, 10, 60)
GENERATE_FUZZYLINF_TEST(256, 256, 10, 250)

GENERATE_FUZZYLINF_TEST(4096, 4096, 6, 10)
GENERATE_FUZZYLINF_TEST(4096, 4096, 6, 60)
GENERATE_FUZZYLINF_TEST(4096, 4096, 6, 250)

GENERATE_FUZZYLINF_TEST(4096, 4096, 10, 10)
GENERATE_FUZZYLINF_TEST(4096, 4096, 10, 60)
GENERATE_FUZZYLINF_TEST(4096, 4096, 10, 250)

GENERATE_FUZZYLINF_TEST(65536, 65536, 6, 10)
GENERATE_FUZZYLINF_TEST(65536, 65536, 6, 60)
GENERATE_FUZZYLINF_TEST(65536, 65536, 6, 250)

GENERATE_FUZZYLINF_TEST(65536, 65536, 10, 10)
GENERATE_FUZZYLINF_TEST(65536, 65536, 10, 60)
GENERATE_FUZZYLINF_TEST(65536, 65536, 10, 250)