#include "./SpLInf.h"
#include "../BlockSpBSOT/BlockSpBSOT.h"
#include "../Common/HashUtils.h"
#include "../Common/SockUtils.h"
#include "../Common/ZN.h"
#include "../CustomOPRF/CustomizedOPRF.h"
#include "../SpBSOT/SpBSOT.h"
#include "cryptoTools/Crypto/PRNG.h"
#include <array>
#include <cmath>
#include <iostream>
#include <unordered_set>
#include <vector>

#define MAX_SSP 128
#define IN_COMP_BIT_LEN 8
#define MAX_UINT8 255

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

template <size_t t, uint64_t M>
void print_vec_shares(array<ZN<M>, t> vec_shares) {

  for (size_t i = 0; i < t; i++) {

    std::cout << "sparse point idx " << i << " => "
              << vec_shares[i].to_uint64_t() << std::endl;
  }
}

template <size_t t, size_t d, uint64_t N>
void gen_zero_shares(array<array<ZN<N>, d>, t> &zero_shares) {

  for (size_t i = 0; i < t; i++) {
    for (size_t j = 0; j < d; j++) {
      zero_shares[i][j] = ZN<N>(0);
    }
  }
}

template <size_t t, size_t d, uint64_t N>
void in_values_to_zn(array<array<uint32_t, d>, t> &in_values,
                     array<array<ZN<N>, d>, t> &convted_values) {

  for (size_t i = 0; i < t; i++) {
    for (size_t j = 0; j < d; j++) {
      convted_values[i][j] = ZN<N>(in_values[i][j]);
    }
  }
}

template <size_t t> void sample_r_blocks(PRNG &prng, array<block, t> r_blocks) {

  for (size_t i = 0; i < t; i++) {
    r_blocks[i] = prng.get<block>();
  }
}

template <size_t t, size_t d>
void comp_g_shares(array<array<ZN<d + 1>, d>, t> &h_vec_shares,
                   array<array<ZN<d + 1>, 1>, t> &g_shares) {

  for (size_t i = 0; i < t; i++) {
    g_shares[i][0] = ZN<d + 1>(0);

    for (size_t j = 0; j < d; j++) {

      g_shares[i][0] = g_shares[i][0] + h_vec_shares[i][j];
    }
  }
}

template <size_t tr, size_t ts, size_t d>
Proto sender_comp_z_shares(OprfSender *oprfSender, OprfReceiver *oprfReceiver,
                           Socket &sock, PRNG &prng, vector<block> &ordIndexSet,
                           array<array<ZN<d + 1>, 1>, ts> &g_vec_shares,
                           array<array<block, 1>, ts> &z_vec_shares) {
  MC_BEGIN(Proto, &sock, oprfSender, oprfReceiver, &prng, &ordIndexSet,
           &g_vec_shares, &z_vec_shares,
           msg_vecs = (array<array<array<block, d + 1>, 1>, ts> *)nullptr,
           bsotSender = (BlockSpBSOTSender<tr, ts, 1, d + 1> *)nullptr);
  msg_vecs = new array<array<array<block, d + 1>, 1>, ts>();
  bsotSender =
      new BlockSpBSOTSender<tr, ts, 1, d + 1>(prng, oprfSender, oprfReceiver);

  for (size_t i = 0; i < ts; i++) {
    (*msg_vecs)[i][0][0] = prng.get<block>();
    (*msg_vecs)[i][0][d] = block(0, 0);

    for (size_t j = 1; j < d; j++) {
      (*msg_vecs)[i][0][j] = (*msg_vecs)[i][0][0];
    }
  }

  MC_AWAIT(bsotSender->send(sock, ordIndexSet, *msg_vecs, g_vec_shares,
                            z_vec_shares));

  delete bsotSender;
  delete msg_vecs;

  MC_END();
}

template <size_t ts, size_t tr, size_t d>
Proto receiver_comp_z_shares(OprfReceiver *oprfReceiver, OprfSender *oprfSender,
                             Socket &sock, PRNG &prng,
                             vector<block> &ordIndexSet,
                             array<array<ZN<d + 1>, 1>, tr> &g_vec_shares,
                             array<array<block, 1>, tr> &z_vec_shares) {
  MC_BEGIN(Proto, &sock, oprfReceiver, oprfSender, &prng, &ordIndexSet,
           &g_vec_shares, &z_vec_shares,
           bsotReceiver = (BlockSpBSOTReceiver<ts, tr, 1, d + 1> *)nullptr);

  bsotReceiver =
      new BlockSpBSOTReceiver<ts, tr, 1, d + 1>(prng, oprfReceiver, oprfSender);

  MC_AWAIT(
      bsotReceiver->receive(sock, ordIndexSet, g_vec_shares, z_vec_shares));

  delete bsotReceiver;

  MC_END();
}

template <size_t tr, size_t t, uint16_t twotol, size_t d, uint8_t delta>
Proto sender_comp_polydom_intrvl(
    OprfSender *oprfSender, OprfReceiver *oprfReceiver, Socket &sock,
    PRNG &prng, vector<block> &ordIndexSet,
    array<array<ZN<twotol>, d>, t> &in_values,
    array<array<ZN<d + 1>, d>, t> &out_vec_shares) {
  static_assert(delta <= MAX_UINT8, "delta must be less or equal to 255");

  MC_BEGIN(Proto, &sock, oprfSender, oprfReceiver, &prng, &ordIndexSet,
           &in_values, &out_vec_shares,
           zero_shares = (array<array<ZN<twotol>, d>, t> *)nullptr,
           msg_vecs = (array<array<array<ZN<d + 1>, twotol>, d>, t> *)nullptr,
           bsotSender = (SpBSOTSender<tr, t, d, twotol, d + 1> *)nullptr,
           int64_delta = (int64_t)delta, in_values_ij = (int64_t)0,
           dist_0_mod256 = (int64_t)0, dist_1_mod256 = (int64_t)0);
  zero_shares = new array<array<ZN<twotol>, d>, t>();
  msg_vecs = new array<array<array<ZN<d + 1>, twotol>, d>, t>();
  bsotSender =
      new SpBSOTSender<tr, t, d, twotol, d + 1>(prng, oprfSender, oprfReceiver);

  for (size_t i = 0; i < t; i++) {
    array<array<ZN<d + 1>, twotol>, d> &msg_vec_batch = msg_vecs->at(i);

    for (size_t j = 0; j < d; j++) {
      array<ZN<d + 1>, twotol> &msg_vec = msg_vec_batch[j];

      for (int64_t h = 0; h < twotol; h++) {
        in_values_ij = in_values[i][j].to_int64_t() % (MAX_UINT8 + 1);
        dist_0_mod256 = in_values_ij - h;
        dist_1_mod256 = h - in_values_ij;

        if (dist_0_mod256 < 0)
          dist_0_mod256 += (MAX_UINT8 + 1);
        if (dist_1_mod256 < 0)
          dist_1_mod256 += (MAX_UINT8 + 1);

        if (dist_0_mod256 <= int64_delta || dist_1_mod256 <= int64_delta) {
          msg_vec[h] = ZN<d + 1>(1);
        } else {
          msg_vec[h] = ZN<d + 1>(0);
        }
      }
    }
  }

  gen_zero_shares<t, d, twotol>(*zero_shares);

  MC_AWAIT(bsotSender->send(sock, ordIndexSet, *msg_vecs, *zero_shares,
                            out_vec_shares));

  delete bsotSender;
  delete zero_shares;
  delete msg_vecs;

  MC_END();
}

template <size_t ts, size_t t, uint16_t twotol, size_t d, uint8_t delta>
Proto receiver_comp_polydom_intrvl(
    OprfReceiver *oprfReceiver, OprfSender *oprfSender, Socket &sock,
    PRNG &prng, vector<block> &ordIndexSet,
    array<array<ZN<twotol>, d>, t> &in_values,
    array<array<ZN<d + 1>, d>, t> &out_vec_shares) {
  MC_BEGIN(Proto, &sock, oprfReceiver, oprfSender, &prng, &ordIndexSet,
           &in_values, &out_vec_shares,
           bsotReceiver = (SpBSOTReceiver<ts, t, d, twotol, d + 1> *)nullptr);
  bsotReceiver = new SpBSOTReceiver<ts, t, d, twotol, d + 1>(prng, oprfReceiver,
                                                             oprfSender);

  MC_AWAIT(bsotReceiver->receive(sock, ordIndexSet, in_values, out_vec_shares));

  delete bsotReceiver;

  MC_END();
}

template <size_t tr, size_t t, size_t d, uint8_t delta, uint8_t ssp>
Proto sparse_comp::sp_linf::Sender<tr, t, d, delta, ssp>::send(
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
           g_shares = (array<array<ZN<d + 1>, 1>, t> *)nullptr,
           oprfSenders = std::vector<OprfSender *>(oprf_instances),
           oprfReceivers = std::vector<OprfReceiver *>(oprf_instances),
           prt = Proto(), prt2 = Proto());

  MC_AWAIT(OprfSender::setup(sock, *(this->prng), oprf_instances,
                             oprfSenders)); // Setup OPRFs
  MC_AWAIT(OprfReceiver::setup(sock, *(this->prng), oprf_instances,
                               oprfReceivers)); // Setup OPRFs

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
Proto sparse_comp::sp_linf::Receiver<ts, t, d, delta, ssp>::receive(
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
           g_shares = (array<array<ZN<d + 1>, 1>, t> *)nullptr,
           oprfReceivers = std::vector<OprfReceiver *>(oprf_instances),
           oprfSenders = std::vector<OprfSender *>(oprf_instances),
           prt = Proto(), prt2 = Proto());

  MC_AWAIT(OprfReceiver::setup(sock, *(this->prng), oprf_instances,
                               oprfReceivers)); // Setup OPRFs
  MC_AWAIT(OprfSender::setup(sock, *(this->prng), oprf_instances,
                             oprfSenders)); // Setup OPRFs

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