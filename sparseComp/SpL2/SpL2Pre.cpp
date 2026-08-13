#include "./SpL2Pre.h"
#include "../BlockSpBSOT/BlockSpBSOT.h"
#include "../Common/ZN.h"
#include "../CustomOPRF/CustomizedOPRF.h"
#include "../SpBSOT/SpBSOT.h"
#include "./SpL2.h"
#include "coproto/Socket/Socket.h"
#include <array>
#include <cmath>

using std::abs;
using std::floor;

template <typename T, size_t N> using array = std::array<T, N>;

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
using Proto = coproto::task<void>;

template <size_t tr, size_t t, uint8_t delta, uint8_t ssp>
Proto sparse_comp::sp_l2_pre::Sender<tr, t, delta, ssp>::offline(Socket &sock) {

  MC_BEGIN(Proto, this, &sock);

  MC_AWAIT(OprfSender::setup(sock, *(this->prng), oprf_instances,
                             oprfSenders)); // Setup OPRFs
  MC_AWAIT(OprfReceiver::setup(sock, *(this->prng), oprf_instances,
                               oprfReceivers)); // Setup OPRFs

  MC_END();
}

template <size_t tr, size_t t, uint8_t delta, uint8_t ssp>
Proto sparse_comp::sp_l2_pre::Sender<tr, t, delta, ssp>::online(
    Socket &sock, vector<block> &ordIndexHashSet,
    array<array<uint32_t, 2>, t> &in_values,
    array<array<block, 1>, t> &z_vec_shares) {
  static_assert(ssp <= MAX_SSP, "ssp must be less or equal to 128");
  constexpr uint8_t l = IN_COMP_BIT_LEN;
  static_assert(l <= 8, "l must be less or equal to 8");
  constexpr const uint16_t twotol = (uint16_t)pow(2, l);
  constexpr const uint64_t M = 2 * (delta + 1) + 1;

  // constexpr const uint64_t two_to_ssp = std::pow(2,ssp);

  constexpr const size_t oprf_instances = 2;
  MC_BEGIN(Proto, this, &sock, &ordIndexHashSet, &in_values, &z_vec_shares,
           zn_in_values = (array<array<ZN<twotol>, 2>, t> *)nullptr,
           h_vec_shares = (array<array<ZN<M>, 2>, t> *)nullptr,
           g_vec_shares = (array<array<ZN<M>, 1>, t> *)nullptr,
           hashed_z_shares = vector<block>(), prt = Proto());

  zn_in_values = new array<array<ZN<twotol>, 2>, t>();
  SpL2_in_values_to_zn<t, twotol>(in_values, *zn_in_values);
  h_vec_shares = new array<array<ZN<M>, 2>, t>();
  prt = SpL2_sender_compute_h_shares<tr, t, twotol, delta, M>(
      oprfSenders[0], oprfReceivers[0], sock, *(this->prng), ordIndexHashSet,
      *zn_in_values, *h_vec_shares);
  MC_AWAIT(prt);
  delete zn_in_values;
  // std::cout << "(s) h_vec_shares[0][0]: " <<
  // h_vec_shares->at(0)[0].to_uint64_t() << " h_vec_shares[0][1]: " <<
  // h_vec_shares->at(0)[1].to_uint64_t() << std::endl; std::cout << "(s)
  // h_vec_shares[1][0]: " << h_vec_shares->at(1)[0].to_uint64_t() << "
  // h_vec_shares[1][1]: " << h_vec_shares->at(1)[1].to_uint64_t() << std::endl;

  g_vec_shares = new array<array<ZN<M>, 1>, t>();
  SpL2_comp_g_shares<t, delta, M>(*h_vec_shares, *g_vec_shares);
  delete h_vec_shares;
  // std::cout << "(s) g_vec_shares[0][0]: " <<
  // g_vec_shares->at(0)[0].to_uint64_t() << std::endl; std::cout << "(s)
  // g_vec_shares[1][0]: " << g_vec_shares->at(1)[0].to_uint64_t() << std::endl;

  prt = SpL2_sender_comp_z_shares<tr, t, delta, M>(
      oprfSenders[1], oprfReceivers[1], sock, *(this->prng), ordIndexHashSet,
      *g_vec_shares, z_vec_shares);
  MC_AWAIT(prt);
  delete g_vec_shares;

  MC_END();
}

template <size_t ts, size_t t, uint8_t delta, uint8_t ssp>
Proto sparse_comp::sp_l2_pre::Receiver<ts, t, delta, ssp>::offline(
    Socket &sock) {

  MC_BEGIN(Proto, this, &sock);
  MC_AWAIT(OprfReceiver::setup(sock, *(this->prng), oprf_instances,
                               oprfReceivers)); // Setup OPRFs
  MC_AWAIT(OprfSender::setup(sock, *(this->prng), oprf_instances,
                             oprfSenders)); // Setup OPRFs

  MC_END();
}

template <size_t ts, size_t t, uint8_t delta, uint8_t ssp>
Proto sparse_comp::sp_l2_pre::Receiver<ts, t, delta, ssp>::online(
    Socket &sock, vector<block> &ordIndexHashSet,
    array<array<uint32_t, 2>, t> &in_values,
    array<array<block, 1>, t> &z_vec_shares) {
  static_assert(ssp <= MAX_SSP, "ssp must be less or equal to 128");
  constexpr const uint8_t l = IN_COMP_BIT_LEN;
  static_assert(l <= 8, "l must be less or equal to 8");
  constexpr const uint16_t twotol = (uint16_t)pow(2, l);
  constexpr const uint64_t M = 2 * (delta + 1) + 1;

  // constexpr const uint64_t two_to_ssp = std::pow(2,ssp);

  constexpr const size_t oprf_instances = 2;
  MC_BEGIN(Proto, this, &sock, &ordIndexHashSet, &in_values, &z_vec_shares,
           zn_in_values = (array<array<ZN<twotol>, 2>, t> *)nullptr,
           h_vec_shares = (array<array<ZN<M>, 2>, t> *)nullptr,
           g_vec_shares = (array<array<ZN<M>, 1>, t> *)nullptr,
           sender_hashed_z_sender_shares = vector<block>(), prt = Proto());

  zn_in_values = new array<array<ZN<twotol>, 2>, t>();
  SpL2_in_values_to_zn<t, twotol>(in_values, *zn_in_values);
  h_vec_shares = new array<array<ZN<M>, 2>, t>();
  prt = SpL2_recvr_compute_h_shares<ts, t, twotol, delta, M>(
      oprfReceivers[0], oprfSenders[0], sock, *(this->prng), ordIndexHashSet,
      *zn_in_values, *h_vec_shares);
  MC_AWAIT(prt);
  delete zn_in_values;
  // std::cout << "(r) h_vec_shares[0][0]: " <<
  // h_vec_shares->at(0)[0].to_uint64_t() << " h_vec_shares[0][1]: " <<
  // h_vec_shares->at(0)[1].to_uint64_t() << std::endl; std::cout << "(r)
  // h_vec_shares[1][0]: " << h_vec_shares->at(1)[0].to_uint64_t() << "
  // h_vec_shares[1][1]: " << h_vec_shares->at(1)[1].to_uint64_t() << std::endl;

  g_vec_shares = new array<array<ZN<M>, 1>, t>();
  SpL2_comp_g_shares<t, delta, M>(*h_vec_shares, *g_vec_shares);
  delete h_vec_shares;
  // std::cout << "(r) g_vec_shares[0][0]: " <<
  // g_vec_shares->at(0)[0].to_uint64_t() << std::endl; std::cout << "(r)
  // g_vec_shares[1][0]: " << g_vec_shares->at(1)[0].to_uint64_t() << std::endl;

  prt = SpL2_receiver_comp_z_shares<ts, t, delta, M>(
      oprfReceivers[1], oprfSenders[1], sock, *(this->prng), ordIndexHashSet,
      *g_vec_shares, z_vec_shares);
  MC_AWAIT(prt);
  delete g_vec_shares;

  MC_END();
}
