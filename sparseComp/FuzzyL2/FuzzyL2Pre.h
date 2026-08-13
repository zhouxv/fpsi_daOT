#pragma once

#include "coproto/Socket/Socket.h"
#include "cryptoTools/Common/block.h"
#include "cryptoTools/Crypto/AES.h"
#include "cryptoTools/Crypto/PRNG.h"

#include "../Common/Common.h"
#include "../SpL2/SpL2Pre.h"
#include <array>
#include <cstdint>
#include <stddef.h>
#include <vector>

namespace sparse_comp::fuzzy_l2_pre {

template <size_t tr, size_t t, uint8_t delta, uint8_t ssp>
using SpL2SenderPre = sparse_comp::sp_l2_pre::Sender<tr, t, delta, ssp>;

template <size_t ts, size_t t, uint8_t delta, uint8_t ssp>
using SpL2ReceiverPre =
    sparse_comp::sp_l2_pre::Receiver<ts, t, delta, ssp>;
template <size_t tr, size_t t, size_t d, uint8_t delta, uint8_t ssp>
class Sender {
private:
  osuCrypto::PRNG *prng;
  osuCrypto::AES *aes;

  static constexpr size_t rcvr_cell_count = (size_t{1} << d) * tr;

  // State shared between the offline and online phases.
  SpL2SenderPre<rcvr_cell_count, t, delta, ssp> *spL2Sender;

  // Input-dependent preprocessing results generated in the offline phase.
  std::vector<osuCrypto::block> point_hashs;
  std::array<std::array<uint32_t, d>, t> *in_values;
public:
  Sender(osuCrypto::PRNG &prng, osuCrypto::AES &aes)
      : prng(&prng), aes(&aes), spL2Sender(nullptr), in_values(nullptr) {}

  coproto::task<void> offline(coproto::Socket &sock,
                              std::array<point, t> &points);

  coproto::task<void> online(coproto::Socket &sock,
                             std::array<point, t> &points);
};
template <size_t ts, size_t t, size_t d, uint8_t delta, uint8_t ssp>
class Receiver {
private:
  osuCrypto::PRNG *prng;
  osuCrypto::AES *aes;

  static constexpr size_t cell_count = (size_t{1} << d) * t;

  // State shared between the offline and online phases.
  SpL2ReceiverPre<ts, cell_count, delta, ssp> *spL2Receiver;

  // Input-dependent preprocessing results generated in the offline phase.
  std::vector<osuCrypto::block> cells;
  std::array<std::array<uint32_t, d>, cell_count> *in_values;
public:
  Receiver(osuCrypto::PRNG &prng, osuCrypto::AES &aes)
      : prng(&prng), aes(&aes), spL2Receiver(nullptr), in_values(nullptr) {}

  coproto::task<void> offline(coproto::Socket &sock,
                              std::array<point, t> &points);

  coproto::task<void> online(coproto::Socket &sock,
                             std::array<point, t> &points,
                             std::vector<point> &intersec);
};

} // namespace sparse_comp::fuzzy_l2_pre
#include "./FuzzyL2Pre.cpp"
