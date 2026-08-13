#pragma once

#include "coproto/Socket/Socket.h"
#include "cryptoTools/Common/block.h"
#include <array>
#include <cstdint>
#include <stddef.h>
#include <vector>

namespace sparse_comp::sp_l2 {

template <size_t tr, size_t ts, uint8_t delta, uint8_t ssp> class Sender {

  osuCrypto::PRNG *prng;
  osuCrypto::AES *aes;

public:
  Sender(osuCrypto::PRNG &prng, osuCrypto::AES &aes) {
    this->prng = &prng;
    this->aes = &aes;
  }

  coproto::task<void> send(coproto::Socket &sock,
                           std::vector<osuCrypto::block> &ordIndexHashSet,
                           std::array<std::array<uint32_t, 2>, ts> &in_values,
                           std::array<std::array<block, 1>, ts> &z_vec_shares);
};

template <size_t ts, size_t tr, uint8_t delta, uint8_t ssp> class Receiver {

  osuCrypto::PRNG *prng;
  osuCrypto::AES *aes;

public:
  Receiver(osuCrypto::PRNG &prng, osuCrypto::AES &aes) {
    this->prng = &prng;
    this->aes = &aes;
  }

  coproto::task<void>
  receive(coproto::Socket &sock, std::vector<osuCrypto::block> &ordIndexHashSet,
          std::array<std::array<uint32_t, 2>, tr> &in_values,
          std::array<std::array<block, 1>, tr> &z_vec_shares);
};

} // namespace sparse_comp::sp_l2

#include "./SpL2.cpp"
