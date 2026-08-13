#pragma once

#include "../CustomOPRF/CustomizedOPRF.h"
#include "coproto/Socket/Socket.h"
#include "cryptoTools/Common/block.h"
#include "cryptoTools/Crypto/AES.h"
#include "cryptoTools/Crypto/PRNG.h"
#include <array>
#include <cstdint>
#include <stddef.h>
#include <vector>

namespace sparse_comp::sp_linf_pre {

template <size_t tr, size_t t, size_t d, uint8_t delta, uint8_t ssp>
class Sender {

  osuCrypto::PRNG *prng;
  osuCrypto::AES *aes;

  static constexpr size_t oprf_instances = 2;
  std::vector<sparse_comp::custom_oprf::Sender *> oprfSenders;
  std::vector<sparse_comp::custom_oprf::Receiver *> oprfReceivers;

public:
  Sender(osuCrypto::PRNG &prng, osuCrypto::AES &aes)
      : oprfReceivers(oprf_instances, nullptr),
        oprfSenders(oprf_instances, nullptr) {
    this->prng = &prng;
    this->aes = &aes;
  }

  coproto::task<void>
  send(coproto::Socket &sock, std::vector<oc::block> &ordIndexHashSet,
       std::array<std::array<uint32_t, d>, t> &in_values,
       std::array<std::array<oc::block, 1>, t> &out_vec_shares);

  coproto::task<void> offline(coproto::Socket &sock);
  coproto::task<void>
  online(coproto::Socket &sock, std::vector<oc::block> &ordIndexHashSet,
         std::array<std::array<uint32_t, d>, t> &in_values,
         std::array<std::array<oc::block, 1>, t> &out_vec_shares);
};

template <size_t ts, size_t t, size_t d, uint8_t delta, uint8_t ssp>
class Receiver {

  osuCrypto::PRNG *prng;
  osuCrypto::AES *aes;

  static constexpr size_t oprf_instances = 2;
  std::vector<sparse_comp::custom_oprf::Sender *> oprfSenders;
  std::vector<sparse_comp::custom_oprf::Receiver *> oprfReceivers;

public:
  Receiver(osuCrypto::PRNG &prng, osuCrypto::AES &aes)
      : oprfReceivers(oprf_instances, nullptr),
        oprfSenders(oprf_instances, nullptr) {
    this->prng = &prng;
    this->aes = &aes;
  }

  coproto::task<void>
  receive(coproto::Socket &sock, std::vector<osuCrypto::block> &ordIndexHashSet,
          std::array<std::array<uint32_t, d>, t> &in_values,
          std::array<std::array<oc::block, 1>, t> &z_vec_shares);

  coproto::task<void> offline(coproto::Socket &sock);
  coproto::task<void>
  online(coproto::Socket &sock, std::vector<osuCrypto::block> &ordIndexHashSet,
         std::array<std::array<uint32_t, d>, t> &in_values,
         std::array<std::array<oc::block, 1>, t> &z_vec_shares);
};

} // namespace sparse_comp::sp_linf_pre

#include "./SpLInfPre.cpp"
