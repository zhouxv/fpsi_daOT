#include "./SpLInfPre.h"
#include "../BlockSpBSOT/BlockSpBSOT.h"
#include "../Common/HashUtils.h"
#include "../Common/SockUtils.h"
#include "../Common/ZN.h"
#include "../CustomOPRF/CustomizedOPRF.h"
#include "../SpBSOT/SpBSOT.h"
#include "./SpLInf.h"
#include "cryptoTools/Crypto/PRNG.h"
#include <array>
#include <cmath>
#include <iostream>
#include <unordered_set>
#include <vector>

using osuCrypto::PRNG;
using std::ceil;
using std::log2;

template <uint64_t N> using ZN = sparse_comp::ZN<N>;

template <typename T, size_t N> using array = std::array<T, N>;

template <typename T> using vector = std::vector<T>;

template <size_t tr, size_t ts, size_t k, size_t n>
using BlockSpBSOTSender = sparse_comp::block_sp_bsot::Sender<tr, ts, k, n>;

template <size_t ts, size_t tr, size_t k, size_t n>
using BlockSpBSOTReceiver = sparse_comp::block_sp_bsot::Receiver<ts, tr, k, n>;

template <size_t tr, size_t t, size_t k, size_t n, uint64_t M>
using SpBSOTSender = sparse_comp::sp_bsot::Sender<tr, t, k, n, M>;

template <size_t ts, size_t t, size_t k, size_t n, uint64_t M>
using SpBSOTReceiver = sparse_comp::sp_bsot::Receiver<ts, t, k, n, M>;

using OprfSender = sparse_comp::custom_oprf::Sender;
using OprfReceiver = sparse_comp::custom_oprf::Receiver;

template <size_t tr, size_t t, size_t d, uint8_t delta, uint8_t ssp>
Proto sparse_comp::sp_linf_pre::Sender<tr, t, d, delta, ssp>::offline(
    Socket &sock) {

  MC_BEGIN(Proto, this, &sock);

  MC_AWAIT(OprfSender::setup(sock, *(this->prng), oprf_instances,
                             oprfSenders)); // Setup OPRFs
  MC_AWAIT(OprfReceiver::setup(sock, *(this->prng), oprf_instances,
                               oprfReceivers)); // Setup OPRFs

  MC_END();
}

template <size_t tr, size_t t, size_t d, uint8_t delta, uint8_t ssp>
Proto sparse_comp::sp_linf_pre::Sender<tr, t, d, delta, ssp>::online(
    Socket &sock, vector<block> &ordIndexSet,
    array<array<uint32_t, d>, t> &in_values,
    array<array<block, 1>, t> &z_vec_shares) {
  static_assert(ssp <= MAX_SSP, "ssp must be less or equal to 128");

  constexpr uint8_t l = IN_COMP_BIT_LEN;
  static_assert(l <= 8, "l must be less or equal to 8");
  constexpr const uint16_t twotol = (uint16_t)pow(2, l);

  constexpr const size_t oprf_instances = 2;

  MC_BEGIN(Proto, this, &sock, &ordIndexSet, &in_values, &z_vec_shares,
           zn_in_values = (array<array<ZN<twotol>, d>, t> *)nullptr,
           h_vec_shares = (array<array<ZN<d + 1>, d>, t> *)nullptr,
           g_shares = (array<array<ZN<d + 1>, 1>, t> *)nullptr, prt = Proto(),
           prt2 = Proto());

  zn_in_values = new array<array<ZN<twotol>, d>, t>();
  in_values_to_zn<t, d, twotol>(in_values, *zn_in_values);

  h_vec_shares = new array<array<ZN<d + 1>, d>, t>();
  prt = sender_comp_polydom_intrvl<tr, t, twotol, d, delta>(
      oprfSenders[0], oprfReceivers[0], sock, *(this->prng), ordIndexSet,
      *zn_in_values, *h_vec_shares);
  MC_AWAIT(prt);
  delete zn_in_values;

  g_shares = new array<array<ZN<d + 1>, 1>, t>();
  comp_g_shares<t, d>(*h_vec_shares, *g_shares);
  delete h_vec_shares;

  prt = sender_comp_z_shares<tr, t, d>(oprfSenders[1], oprfReceivers[1], sock,
                                       *(this->prng), ordIndexSet, *g_shares,
                                       z_vec_shares);
  MC_AWAIT(prt);
  delete g_shares;

  MC_END();
}

template <size_t ts, size_t t, size_t d, uint8_t delta, uint8_t ssp>
Proto sparse_comp::sp_linf_pre::Receiver<ts, t, d, delta, ssp>::offline(
    Socket &sock) {

  MC_BEGIN(Proto, this, &sock);

  MC_AWAIT(OprfReceiver::setup(sock, *(this->prng), oprf_instances,
                               oprfReceivers)); // Setup OPRFs
  MC_AWAIT(OprfSender::setup(sock, *(this->prng), oprf_instances,
                             oprfSenders)); // Setup OPRFs

  MC_END();
}

template <size_t ts, size_t t, size_t d, uint8_t delta, uint8_t ssp>
Proto sparse_comp::sp_linf_pre::Receiver<ts, t, d, delta, ssp>::online(
    Socket &sock, vector<osuCrypto::block> &ordIndexSet,
    array<array<uint32_t, d>, t> &in_values,
    array<array<block, 1>, t> &z_vec_shares) {
  static_assert(ssp <= MAX_SSP, "ssp must be less or equal to 128");

  constexpr const uint8_t l = IN_COMP_BIT_LEN;
  static_assert(l <= 8, "l must be less or equal to 8");
  constexpr const uint16_t twotol = (uint16_t)pow(2, l);

  constexpr const size_t oprf_instances = 2;

  MC_BEGIN(Proto, this, &sock, &ordIndexSet, &in_values, &z_vec_shares,
           zn_in_values = (array<array<ZN<twotol>, d>, t> *)nullptr,
           h_vec_shares = (array<array<ZN<d + 1>, d>, t> *)nullptr,
           g_shares = (array<array<ZN<d + 1>, 1>, t> *)nullptr, prt = Proto(),
           prt2 = Proto());

  zn_in_values = new array<array<ZN<twotol>, d>, t>();
  h_vec_shares = new array<array<ZN<d + 1>, d>, t>();

  in_values_to_zn<t, d, twotol>(in_values, *zn_in_values);

  prt = receiver_comp_polydom_intrvl<ts, t, twotol, d, delta>(
      oprfReceivers[0], oprfSenders[0], sock, *(this->prng), ordIndexSet,
      *zn_in_values, *h_vec_shares);
  MC_AWAIT(prt);
  delete zn_in_values;

  g_shares = new array<array<ZN<d + 1>, 1>, t>();
  comp_g_shares<t, d>(*h_vec_shares, *g_shares);
  delete h_vec_shares;

  prt = receiver_comp_z_shares<ts, t, d>(oprfReceivers[1], oprfSenders[1], sock,
                                         *(this->prng), ordIndexSet, *g_shares,
                                         z_vec_shares);
  MC_AWAIT(prt);

  delete g_shares;

  MC_END();
}