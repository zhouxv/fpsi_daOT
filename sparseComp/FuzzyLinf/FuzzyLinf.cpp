#include "./FuzzyLinf.h"
#include "../Common/Common.h"
#include "../Common/HashUtils.h"
#include "../SpLInf/SpLInf.h"
#include <array>
#include <cstdint>
#include <iostream>
#include <vector>

template <size_t tr, size_t t, size_t d, uint8_t delta, uint8_t ssp>
using SpLinfSender = sparse_comp::sp_linf::Sender<tr, t, d, delta, ssp>;

template <size_t ts, size_t t, size_t d, uint8_t delta, uint8_t ssp>
using SpLinfReceiver = sparse_comp::sp_linf::Receiver<ts, t, d, delta, ssp>;

template <size_t t, size_t d>
static void
sndr_points_to_in_values(std::array<point, t> &points,
                         std::array<std::array<uint32_t, d>, t> &in_values) {
  for (size_t i = 0; i < t; i++) {
    for (size_t j = 0; j < d; j++) {
      in_values[i][j] = points[i].coords[j];
    }
  }
}

template <size_t ts, size_t d, uint8_t ssp>
void compute_final_encryped_points(AES &hash, array<point, ts> &sndr_points,
                                   vector<block> &sndr_points_spthashs,
                                   array<array<block, 1>, ts> &z_vec_shares,
                                   vector<block> &idx_okvs,
                                   vector<block> &point_ctxs) {
  static_assert(ts > 0 && d > 0);
  static_assert(ssp <= 64, "ssp must be less or equal to 64");

  vector<block> okvs_vals(ts);

  sparse_comp::points_to_blocks<ts, d>(sndr_points, point_ctxs);
  size_t pt_blk_cnt = sparse_comp::point_encoding_block_count(d);

  for (size_t i = 0; i < ts; i++) {
    PRNG prng(z_vec_shares[i][0]);
    block k0 = prng.get<block>();

    okvs_vals[i] = k0 ^ block((uint64_t)0, (uint64_t)i);

    for (size_t j = 0; j < pt_blk_cnt; j++) {
      point_ctxs[i * pt_blk_cnt + j] =
          point_ctxs[i * pt_blk_cnt + j] ^ prng.get<block>();
    }
  }

  Baxos paxos;
  paxos.init(ts, sparse_comp::baxosBinSize(ts), 3, ssp, PaxosParam::GF128,
             oc::ZeroBlock);

  idx_okvs.resize(paxos.size());

  paxos.solve<block>(sndr_points_spthashs, okvs_vals, idx_okvs, nullptr, 1);
}

template <size_t ts, size_t tr, size_t d, size_t cell_count, uint32_t ssp>
void receiver_intersection(AES &hash, array<point, tr> &rcver_points,
                           vector<block> &rcvr_cells,
                           array<array<block, 1>, cell_count> &rcvr_z_shares,
                           vector<block> &sndr_idx_okvs,
                           vector<block> &sndr_point_ctxs,
                           vector<point> &intersec) {
  constexpr const size_t twotod = (size_t)pow(2, d);
  static_assert(cell_count == twotod * tr);
  static_assert(ts > 0 && tr > 0);
  static_assert(ssp <= 64, "ssp must be less or equal to 64");

  size_t pt_blk_cnt = sparse_comp::point_encoding_block_count(d);
  // vector<block> decoded_keys(cell_count);
  vector<block> decoded_vals(cell_count);

  // for (size_t i=0;i < cell_count;i++) {
  //     decoded_keys[i] = sparse_comp::hash_point(hash, rcvr_cells[i]);
  // }

  Baxos paxos;
  paxos.init(ts, sparse_comp::baxosBinSize(ts), 3, ssp, PaxosParam::GF128,
             oc::ZeroBlock);

  paxos.decode<block>(rcvr_cells, decoded_vals, sndr_idx_okvs);

  const block high_u64_msk = block(0xFFFFFFFFFFFFFFFFULL, 0);

  std::vector<block> dec_blocks(pt_blk_cnt);

  for (size_t i = 0; i < cell_count; i++) {
    PRNG prng(rcvr_z_shares[i][0]);
    block k0 = prng.get<block>();

    block dec_okvs_val = decoded_vals[i] ^ k0;

    if ((dec_okvs_val & high_u64_msk) != block(0, 0))
      continue;

    size_t idx = (size_t)(reinterpret_cast<uint64_t *>(dec_okvs_val.data())[0]);

    for (size_t j = 0; j < pt_blk_cnt; j++) {
      dec_blocks[j] = sndr_point_ctxs[idx * pt_blk_cnt + j] ^ prng.get<block>();
    }

    point pt = sparse_comp::blocks_to_point<d>(dec_blocks);

    intersec.push_back(pt);
  }
}

template <size_t tr, size_t t, size_t d, uint8_t delta, uint8_t ssp>
Proto sparse_comp::fuzzy_linf::Sender<tr, t, d, delta, ssp>::send(
    Socket &sock, array<point, t> &points) {
  constexpr const size_t twotod = (size_t)pow(2, d);
  constexpr const size_t rcvr_cell_count = twotod * tr;

  MC_BEGIN(
      Proto, this, &sock, &points,
      spLinfSender = (SpLinfSender<rcvr_cell_count, t, d, delta, ssp> *)nullptr,
      point_hashs = vector<block>(),
      in_values = (array<array<uint32_t, d>, t> *)nullptr,
      out_vec_shares = (array<array<block, 1>, t> *)nullptr,
      idx_okvs = vector<block>(), point_ctxs = vector<block>(), prt = Proto());

  spLinfSender = new SpLinfSender<rcvr_cell_count, t, d, delta, ssp>(
      *(this->prng), *(this->aes));
  in_values = new array<array<uint32_t, d>, t>();
  out_vec_shares = new array<array<block, 1>, t>();

  // Maps points to cells using spatial hashing
  sparse_comp::spatial_hash<t>(*(this->aes), points, point_hashs, d, delta);

  // Maps points to in_values
  sndr_points_to_in_values<t, d>(points, *in_values);

  prt = spLinfSender->send(sock, point_hashs, *in_values, *out_vec_shares);

  MC_AWAIT(prt);

  compute_final_encryped_points<t, d, ssp>(
      *(this->aes), points, point_hashs, *out_vec_shares, idx_okvs, point_ctxs);

  prt = sparse_comp::send<block, sparse_comp::COPROTO_MAX_SEND_SIZE_BYTES>(
      sock, idx_okvs);
  MC_AWAIT(prt);
  prt = sparse_comp::send<block, sparse_comp::COPROTO_MAX_SEND_SIZE_BYTES>(
      sock, point_ctxs);
  MC_AWAIT(prt);

  delete spLinfSender;
  delete in_values;
  delete out_vec_shares;

  MC_END();
}

template <size_t t, size_t d, size_t cell_count>
static void rcvr_points_to_in_values(
    std::array<point, t> &point_center,
    std::array<std::array<uint32_t, d>, cell_count> &in_values) {
  constexpr const size_t twotod = (size_t)pow(2, d);

  static_assert(cell_count == twotod * t);
  static_assert(d <= point::MAX_DIM);

  for (size_t i = 0; i < t; i++) {
    for (size_t j = 0; j < twotod; j++) {
      for (size_t k = 0; k < d; k++) {
        in_values[twotod * i + j][k] = point_center[i].coords[k];
      }
    }
  }
}

template <size_t ts, size_t t, size_t d, uint8_t delta, uint8_t ssp>
Proto sparse_comp::fuzzy_linf::Receiver<ts, t, d, delta, ssp>::receive(
    Socket &sock, array<point, t> &points, vector<point> &intersec) {
  constexpr const size_t twotod = (size_t)pow(2, d);
  constexpr const size_t cell_count = twotod * t;

  MC_BEGIN(Proto, this, &sock, &points, &intersec,
           spLinfReceiver =
               (SpLinfReceiver<ts, cell_count, d, delta, ssp> *)nullptr,
           cells = vector<block>(cell_count),
           in_values = (array<array<uint32_t, d>, cell_count> *)nullptr,
           out_vec_shares = (array<array<block, 1>, cell_count> *)nullptr,
           idx_okvs = vector<block>(), point_ctxs = vector<block>(),
           paxos = Baxos(), prt = Proto());

  spLinfReceiver = new SpLinfReceiver<ts, cell_count, d, delta, ssp>(
      *(this->prng), *(this->aes));
  in_values = new array<array<uint32_t, d>, cell_count>();
  out_vec_shares = new array<array<block, 1>, cell_count>();

  // Maps points to adjcent cells using spatial hashing
  sparse_comp::spatial_cell_hash<t, d, cell_count>(*(this->aes), points, cells,
                                                   delta);

  // Maps points to in_values
  rcvr_points_to_in_values<t, d, cell_count>(points, *in_values);

  prt = spLinfReceiver->receive(sock, cells, *in_values, *out_vec_shares);
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

  delete spLinfReceiver;
  delete in_values;
  delete out_vec_shares;

  MC_END();
}