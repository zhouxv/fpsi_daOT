#pragma once

#include "coproto/Socket/Socket.h"
#include "cryptoTools/Common/block.h"
#include "cryptoTools/Crypto/AES.h"
#include "cryptoTools/Crypto/PRNG.h"

#include "../Common/Common.h"
#include "../SpL1/SpL1Pre.h"
#include <array>
#include <cstdint>
#include <stddef.h>
#include <vector>

namespace sparse_comp::fuzzy_l1_pre {

template <size_t tr, size_t t, size_t d, uint8_t delta, uint8_t ssp>
using SpL1SenderPre = sparse_comp::sp_l1_pre::Sender<tr, t, d, delta, ssp>;

template <size_t ts, size_t t, size_t d, uint8_t delta, uint8_t ssp>
using SpL1ReceiverPre =
    sparse_comp::sp_l1_pre::Receiver<ts, t, d, delta, ssp>;
template <size_t tr, size_t t, size_t d, uint8_t delta, uint8_t ssp>
class Sender {
private:
  osuCrypto::PRNG *prng;
  osuCrypto::AES *aes;

  static constexpr size_t rcvr_cell_count = (size_t{1} << d) * tr;

  // State shared between the offline and online phases.
  SpL1SenderPre<rcvr_cell_count, t, d, delta, ssp> *spL1Sender;

  // Input-dependent preprocessing results generated in the offline phase.
  std::vector<osuCrypto::block> point_hashs;
  std::array<std::array<uint32_t, d>, t> *in_values;
public:
  Sender(osuCrypto::PRNG &prng, osuCrypto::AES &aes)
      : prng(&prng), aes(&aes), spL1Sender(nullptr), in_values(nullptr) {}

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
  SpL1ReceiverPre<ts, cell_count, d, delta, ssp> *spL1Receiver;

  // Input-dependent preprocessing results generated in the offline phase.
  std::vector<osuCrypto::block> cells;
  std::array<std::array<uint32_t, d>, cell_count> *in_values;
public:
  Receiver(osuCrypto::PRNG &prng, osuCrypto::AES &aes)
      : prng(&prng), aes(&aes), spL1Receiver(nullptr), in_values(nullptr) {}

  coproto::task<void> offline(coproto::Socket &sock,
                              std::array<point, t> &points);

  coproto::task<void> online(coproto::Socket &sock,
                             std::array<point, t> &points,
                             std::vector<point> &intersec);
};

} // namespace sparse_comp::fuzzy_l1_pre
#include "./FuzzyL1Pre.cpp"
