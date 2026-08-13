#include "./FuzzyL2.h"
#include "../Common/Common.h"
#include "../Common/HashUtils.h"
#include "../FuzzyLInf/FuzzyLInf.h"
#include "../SpL2/SpL2.h"
#include <array>
#include <cstdint>
#include <iostream>
#include <vector>

template <size_t tr, size_t t, uint8_t delta, uint8_t ssp>
using SpL2Sender = sparse_comp::sp_l2::Sender<tr, t, delta, ssp>;

template <size_t ts, size_t t, uint8_t delta, uint8_t ssp>
using SpL2Receiver = sparse_comp::sp_l2::Receiver<ts, t, delta, ssp>;

template <size_t tr, size_t t, size_t d, uint8_t delta, uint8_t ssp>
Proto sparse_comp::fuzzy_l2::Sender<tr, t, d, delta, ssp>::send(
    Socket &sock, array<point, t> &points) {
  constexpr const size_t twotod = (size_t)pow(2, d);
  constexpr const size_t rcvr_cell_count = twotod * tr;

  MC_BEGIN(Proto, this, &sock, &points,
           spL2Sender = (SpL2Sender<rcvr_cell_count, t, delta, ssp> *)nullptr,
           point_hashs = vector<block>(),
           in_values = (array<array<uint32_t, d>, t> *)nullptr,
           out_vec_shares = (array<array<block, 1>, t> *)nullptr,
           idx_okvs = vector<block>(), point_ctxs = vector<block>(),
           prt = Proto());

  spL2Sender = new SpL2Sender<rcvr_cell_count, t, delta, ssp>(*(this->prng),
                                                              *(this->aes));
  in_values = new array<array<uint32_t, d>, t>();
  out_vec_shares = new array<array<block, 1>, t>();

  // Maps points to cells using spatial hashing
  sparse_comp::spatial_hash<t>(*(this->aes), points, point_hashs, d, delta);

  // Maps points to in_values
  sndr_points_to_in_values<t, d>(points, *in_values);

  prt = spL2Sender->send(sock, point_hashs, *in_values, *out_vec_shares);

  MC_AWAIT(prt);

  compute_final_encryped_points<t, d, ssp>(
      *(this->aes), points, point_hashs, *out_vec_shares, idx_okvs, point_ctxs);

  prt = sparse_comp::send<block, sparse_comp::COPROTO_MAX_SEND_SIZE_BYTES>(
      sock, idx_okvs);
  MC_AWAIT(prt);
  prt = sparse_comp::send<block, sparse_comp::COPROTO_MAX_SEND_SIZE_BYTES>(
      sock, point_ctxs);
  MC_AWAIT(prt);

  delete spL2Sender;
  delete in_values;
  delete out_vec_shares;

  MC_END();
}

template <size_t ts, size_t t, size_t d, uint8_t delta, uint8_t ssp>
Proto sparse_comp::fuzzy_l2::Receiver<ts, t, d, delta, ssp>::receive(
    Socket &sock, array<point, t> &points, vector<point> &intersec) {
  constexpr const size_t twotod = (size_t)pow(2, d);
  constexpr const size_t cell_count = twotod * t;

  MC_BEGIN(Proto, this, &sock, &points, &intersec,
           spL2Receiver = (SpL2Receiver<ts, cell_count, delta, ssp> *)nullptr,
           cells = vector<block>(cell_count),
           in_values = (array<array<uint32_t, d>, cell_count> *)nullptr,
           out_vec_shares = (array<array<block, 1>, cell_count> *)nullptr,
           idx_okvs = vector<block>(), point_ctxs = vector<block>(),
           paxos = Baxos(), prt = Proto());

  spL2Receiver =
      new SpL2Receiver<ts, cell_count, delta, ssp>(*(this->prng), *(this->aes));
  in_values = new array<array<uint32_t, d>, cell_count>();
  out_vec_shares = new array<array<block, 1>, cell_count>();

  // Maps points to adjcent cells using spatial hashing
  sparse_comp::spatial_cell_hash<t, d, cell_count>(*(this->aes), points, cells,
                                                   delta);

  // Maps points to in_values
  rcvr_points_to_in_values<t, d, cell_count>(points, *in_values);

  prt = spL2Receiver->receive(sock, cells, *in_values, *out_vec_shares);
  MC_AWAIT(prt);

  paxos.init(ts, sparse_comp::baxosBinSize(ts), 3, ssp, PaxosParam::GF128,
             oc::ZeroBlock);
  idx_okvs.resize(paxos.size());

  point_ctxs.resize(ts * sparse_comp::point_encoding_block_count(d));

  prt = sparse_comp::receive<block, sparse_comp::COPROTO_MAX_SEND_SIZE_BYTES>(
      sock, idx_okvs.size(), idx_okvs);
  MC_AWAIT(prt);

  prt = sparse_comp::receive<block, sparse_comp::COPROTO_MAX_SEND_SIZE_BYTES>(
      sock, point_ctxs.size(), point_ctxs);
  MC_AWAIT(prt);

  receiver_intersection<ts, t, d, cell_count, ssp>(*(this->aes), points, cells,
                                                   *out_vec_shares, idx_okvs,
                                                   point_ctxs, intersec);

  delete spL2Receiver;
  delete in_values;
  delete out_vec_shares;

  MC_END();
}