#pragma once

#include "coproto/Socket/Socket.h"
#include "cryptoTools/Common/block.h"
#include "cryptoTools/Crypto/AES.h"
#include "cryptoTools/Crypto/PRNG.h"
#include <array>
#include <cstdint>
#include <stddef.h>
#include <vector>

#include "../Common/Common.h"

namespace sparse_comp::fuzzy_l2 {

template <size_t tr, size_t t, size_t d, uint8_t delta, uint8_t ssp>
class Sender {

  osuCrypto::PRNG *prng;
  osuCrypto::AES *aes;

public:
  Sender(osuCrypto::PRNG &prng, osuCrypto::AES &aes) {
    this->prng = &prng;
    this->aes = &aes;
  }

  coproto::task<void> send(coproto::Socket &sock, std::array<point, t> &points);
};

template <size_t ts, size_t t, size_t d, uint8_t delta, uint8_t ssp>
class Receiver {

  osuCrypto::PRNG *prng;
  osuCrypto::AES *aes;

public:
  Receiver(osuCrypto::PRNG &prng, osuCrypto::AES &aes) {
    this->prng = &prng;
    this->aes = &aes;
  }

  coproto::task<void> receive(coproto::Socket &sock,
                              std::array<point, t> &points,
                              std::vector<point> &intersec);
};

} // namespace sparse_comp::fuzzy_l2

#include "./FuzzyL2.cpp"
