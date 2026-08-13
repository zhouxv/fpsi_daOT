#include "./SpBSOT.h"
#include "../Common/BaxosUtils.h"
#include "../Common/Common.h"
#include "../Common/HashUtils.h"
#include "../Common/SockUtils.h"
#include "../Common/VecMatrix.h"
#include "../CustomOPRF/CustomizedOPRF.h"
#include "cryptoTools/Crypto/AES.h"
#include "volePSI/Paxos.h"
#include <array>
#include <iostream>
#include <math.h>
#include <utility>
#include <vector>

#define SSP 40
// #define SP_SOT_PAXOS_BIN_SIZE 1 << 14
#define NUM_THREADS 1

using macoro::sync_wait;
using macoro::when_all_ready;

using OprfSender = sparse_comp::custom_oprf::Sender;
using OprfReceiver = sparse_comp::custom_oprf::Receiver;
using oprf_point = sparse_comp::custom_oprf::oprf_point;
using sparse_comp::point;
using block = osuCrypto::block;
using AES = osuCrypto::AES;
using Baxos = volePSI::Baxos;
using PaxosParam = volePSI::PaxosParam;
using Socket = coproto::Socket;

template <typename T> using VecMatrix = sparse_comp::VecMatrix<T>;

template <uint64_t N> using ZN = sparse_comp::ZN<N>;

template <typename T, size_t N> using array = std::array<T, N>;

template <typename T> using vector = std::vector<T>;

/*[CODE FOR DEBUGGING PURPOSES ONLY]*/
template <size_t t, size_t d, uint8_t l, uint64_t M>
void print_vec_shares(array<array<ZN<M>, d * l>, t> vec_shares) {

  for (size_t i = 0; i < t; i++) {

    std::cout << "sparse point idx" << i << std::endl;

    for (size_t j = 0; j < d; j++) {

      std::cout << "    batch idx: " << j << std::endl << "        ";

      for (size_t h = 0; h < l; h++) {

        std::cout << vec_shares[i][j * l + h].to_uint64_t() << " ";
      }

      std::cout << std::endl;
    }
  }
}

template <size_t t, size_t k, size_t n, uint64_t M>
std::array<VecMatrix<ZN<M>> *, t> *
mask_msg_vectors_with_r_values(array<array<ZN<M>, k>, t> &r_matrix,
                               array<array<array<ZN<M>, n>, k>, t> &msg_vecs) {
  std::array<VecMatrix<ZN<M>> *, t> *msg_vecs_plus_r =
      new std::array<VecMatrix<ZN<M>> *, t>();

  for (size_t i = 0; i < t; i++) {

    array<array<ZN<M>, n>, k> &mtx = msg_vecs[i];

    msg_vecs_plus_r->at(i) = ZN<M>::template subVec<k, n>(mtx, r_matrix[i]);
  }

  return msg_vecs_plus_r;
}

template <size_t n, uint64_t M, uint32_t k, uint32_t t>
static void
cshift_masked_matrices(std::array<VecMatrix<ZN<M>> *, t> &masked_mtxs,
                       array<std::array<ZN<n>, k>, t> &choice_vec_shares) {

  for (size_t i = 0; i < t; i++) {

    VecMatrix<ZN<M>> &mtx = *(masked_mtxs[i]);
    std::array<ZN<n>, k> &offset_vec = choice_vec_shares[i];

    for (size_t j = 0; j < k; j++) {

      mtx.cshift(j, offset_vec[j].to_size_t());
    }
  }
}

template <size_t t, size_t k, size_t n, uint64_t M>
array<VecMatrix<block> *, t> *ZN_matrix_array_to_block_matrix_array(
    array<VecMatrix<ZN<M>> *, t> &zN_mtx_array) {
  std::array<VecMatrix<block> *, t> *block_mtx_array =
      new std::array<VecMatrix<block> *, t>();

  for (size_t i = 0; i < t; i++) {
    VecMatrix<block> *block_mtx = new VecMatrix<block>(k, n);
    VecMatrix<ZN<M>> &zN_mtx = *(zN_mtx_array[i]);
    block_mtx_array->at(i) = block_mtx;
    for (size_t j = 0; j < k; j++) {
      std::vector<block> &block_mtx_row = block_mtx->row(j);
      std::vector<ZN<M>> &zN_mtx_row = zN_mtx[j];
      for (size_t h = 0; h < n; h++) {
        block_mtx_row[h] = zN_mtx_row[h].to_block();
      }
    }
  }

  return block_mtx_array;
}

template <size_t k, size_t n>
inline void xor_block_vec_mtxs(VecMatrix<block> &block_mtx,
                               VecMatrix<block> &mask_mtx) {

  for (size_t i = 0; i < k; i++) {

    std::vector<block> &block_row = block_mtx[i];
    auto mask_row = mask_mtx[i];

    for (size_t j = 0; j < n; j++) {

      block_row[j] = block_row[j] ^ mask_row[j];
    }
  }
}

template <size_t t, size_t k, size_t n>
void mask_block_mtx_using_oprf(OprfSender &oprfSender,
                               vector<block> &point_hashes,
                               array<VecMatrix<block> *, t> &block_mtxs) {
  VecMatrix<block> mask_mtx(k, n);

  for (size_t i = 0; i < t; i++) {
    VecMatrix<block> *block_mtx = block_mtxs[i];

    oprfSender.eval(point_hashes[i], k, n, mask_mtx);

    xor_block_vec_mtxs<k, n>(*block_mtx, mask_mtx);
  }
}

template <size_t t, size_t k, size_t n>
void mask_block_mtx_using_h_vec(array<VecMatrix<block> *, t> &block_mtxs,
                                vector<block> &h_vec) {

  size_t g = 0;
  for (size_t i = 0; i < t; i++) {
    VecMatrix<block> *block_mtx = block_mtxs[i];

    for (size_t j = 0; j < k; j++) {
      block h_vec_msk = h_vec[g];
      g++;
      vector<block> &block_mtx_row = block_mtx->row(j);

      for (size_t y = 0; y < n; y++) {
        block_mtx_row[y] = block_mtx_row[y] ^ h_vec_msk;
      }
    }
  }
}

template <size_t t, size_t k, size_t n>
static vector<block> *encode_okvs(const AES &aes, vector<block> &pointHashes,
                                  array<VecMatrix<block> *, t> &block_mtxs) {

  vector<block> okvs_idxs(t * k * n);
  vector<block> okvs_values(t * k * n);

  size_t g = 0;

  for (size_t i = 0; i < t; i++) {

    VecMatrix<block> &block_mtx = *(block_mtxs[i]);

    for (size_t j = 0; j < k; j++) {
      auto block_mtx_row = block_mtx[j];

      for (size_t h = 0; h < n; h++) {

        okvs_idxs[g] = sparse_comp::hash_point(aes, pointHashes[i], j, h);
        okvs_values[g] = block_mtx_row[h];

        g++;
      }
    }
  }

  Baxos paxos;
  paxos.init(t * k * n, sparse_comp::baxosBinSize(t * k * n), 3, SSP,
             PaxosParam::GF128, oc::ZeroBlock);
  vector<block> *paxos_structure = new vector<block>(paxos.size());

  // std::cout << okvs_idxs.size() << ";" << okvs_values.size() << std::endl;

  // std::cout << "sender encoded values: " << (okvs_values[0] & (block(0,-1) >>
  // 62)) << " , " << (okvs_values[1] & (block(0,-1) >> 62)) << std::endl;

  paxos.solve<block>(okvs_idxs, okvs_values, *paxos_structure, nullptr,
                     NUM_THREADS);

  // std::cout << "TEST" << std::endl;

  return paxos_structure;
}

inline size_t calc_compact_okvs_struct_size(size_t okvs_struct_size,
                                            uint8_t keep_nbits) {
  return ceil(((double)(okvs_struct_size * keep_nbits)) / ((double)64));
}

inline vector<uint64_t> *truncate_okvs(vector<block> &okvs_struct,
                                       uint8_t keep_nbits) {

  // std::cout << "okvs_struct_size: " << okvs_struct.size() << std::endl;
  // std::cout << "keep_nbits: " << uint64_t(keep_nbits) << std::endl;

  const size_t cmpct_okvs_strcut_size =
      calc_compact_okvs_struct_size(okvs_struct.size(), keep_nbits);
  // std::cout << "cmpct_okvs_strcut_size: " << cmpct_okvs_strcut_size <<
  // std::endl;
  vector<uint64_t> *cmpct_paxos_struct_p =
      new vector<uint64_t>(cmpct_okvs_strcut_size);
  vector<uint64_t> &cmpct_paxos_struct = *cmpct_paxos_struct_p;

  const uint64_t mask = (uint64_t(-1) >> (64 - keep_nbits));
  size_t curr_cmpt_struct_idx = 0;
  size_t curr_cell_bit_offset = 0;
  for (size_t i = 0; i < okvs_struct.size(); i++) {
    uint64_t data = okvs_struct[i].get<uint64_t>(0) & mask;

    cmpct_paxos_struct[curr_cmpt_struct_idx] =
        cmpct_paxos_struct[curr_cmpt_struct_idx] ^
        (data << curr_cell_bit_offset);
    curr_cell_bit_offset += keep_nbits;

    if (curr_cell_bit_offset == 64) {
      curr_cmpt_struct_idx++;

      curr_cell_bit_offset = 0;
    } else if (curr_cell_bit_offset > 64) {
      curr_cmpt_struct_idx++;
      cmpct_paxos_struct[curr_cmpt_struct_idx] =
          cmpct_paxos_struct[curr_cmpt_struct_idx] ^
          (data >> (keep_nbits - curr_cell_bit_offset + 64));

      curr_cell_bit_offset = curr_cell_bit_offset - 64;
    }
  }

  return cmpct_paxos_struct_p;
}

inline void reconstruct_okvs(vector<uint64_t> &compressed_okvs,
                             uint8_t keep_nbits, vector<block> &okvs_struct) {
  size_t curr_cmpt_struct_idx = 0;
  size_t curr_cell_bit_offset = 0;
  for (size_t i = 0; i < okvs_struct.size(); i++) {
    uint64_t mask = (uint64_t(-1) >> (64 - keep_nbits));
    uint64_t b0 =
        (compressed_okvs[curr_cmpt_struct_idx] >> curr_cell_bit_offset) & mask;
    curr_cell_bit_offset += keep_nbits;

    if (curr_cell_bit_offset == 64) {
      curr_cmpt_struct_idx++;

      curr_cell_bit_offset = 0;
    } else if (curr_cell_bit_offset >= 64) {
      curr_cmpt_struct_idx++;
      uint64_t rem_bit_count = curr_cell_bit_offset - 64;
      mask = rem_bit_count != 0 ? uint64_t(-1) >> (64 - rem_bit_count)
                                : uint64_t(0);

      b0 = b0 ^ ((compressed_okvs[curr_cmpt_struct_idx] & mask)
                 << (keep_nbits - rem_bit_count));

      curr_cell_bit_offset = rem_bit_count;
    }

    okvs_struct[i] = block(0, b0);
  }
}

/*static void print_okvs(vector<uint64_t>& truncated_okvs) {

    for (size_t i=0;i < truncated_okvs.size();i++) {
        std::cout << truncated_okvs[i] << std::endl;
    }

}

static void print_truncated_okvs(vector<block>& okvs) {
     for (size_t i=0;i < okvs.size();i++) {
        std::cout << (okvs[i] & (block(0,-1) >> 62)) << std::endl;
    }
}*/

template <uint64_t M>
Proto sendTruncatedOkvsStructure(Socket &sock, vector<block> &okvs_struct) {
  MC_BEGIN(Proto, &sock, &okvs_struct,
           truncated_okvs = (vector<uint64_t> *)nullptr, log2M = uint8_t(0),
           t = coproto::task<void>());

  log2M = ceil(log2(M));

  //	std::cout << "computing truncated okvs (s)" << std::endl;

  truncated_okvs = truncate_okvs(okvs_struct, log2M);

  //	std::cout << "truncated okvs computed (s)" << std::endl;

  //	std::cout << "truncated okvs vec size: " << truncated_okvs->size() <<
  // std::endl;

  // print_truncated_okvs(okvs_struct);

  // MC_AWAIT(sock.send(std::move(*truncated_okvs)));

  // std::cout << "sending truncated okvs (s); okvs byte size" <<
  // truncated_okvs->size() * sizeof(uint64_t) << std::endl;

  t = sparse_comp::send<uint64_t, sparse_comp::COPROTO_MAX_SEND_SIZE_BYTES>(
      sock, *truncated_okvs);

  MC_AWAIT(t);

  // std::cout << "sent truncated okvs (s)" << std::endl;

  delete truncated_okvs;

  MC_END();
}

template <size_t t, size_t k>
Proto sender_query_oprf(coproto::Socket &sock, OprfReceiver &oprfRecv,
                        const vector<block> &ordIndexSet,
                        vector<block> &oprf_vals) {
  MC_BEGIN(Proto, &sock, &oprfRecv, &ordIndexSet, &oprf_vals,
           oprf_points = vector<oprf_point>(t * k), g = (size_t)0,
           sparse_point = point());

  g = 0;

  for (size_t i = 0; i < t; i++) {
    // std::cout << "i = " << i << std::endl;
    // std::cout << sparse_point.coords[0] << ";" << sparse_point.coords[1] <<
    // std::endl;

    for (size_t j = 0; j < k; j++) {

      oprf_points[g] = oprf_point(ordIndexSet[i], j, 0);
      // std::cout << "coord[0] = " << oprf_points[g].point.coords[0] << "
      // coord[1] = " << oprf_points[g].point.coords[1] << " sot_ix = " <<
      // (+oprf_points[g].sot_idx) << " sot_choice_share = " <<
      // (+oprf_points[g].sot_choice_share) << std::endl;

      g++;
    }
  }

  MC_AWAIT(oprfRecv.receive(sock, t * k, oprf_points, oprf_vals));

  MC_END();
}

template <size_t tr, size_t t, size_t k, size_t n, uint64_t M>
Proto sparse_comp::sp_bsot::Sender<tr, t, k, n, M>::send(
    coproto::Socket &sock, vector<block> &ordIndexSet,
    array<array<array<ZN<M>, n>, k>, t> &msg_vecs,
    array<array<ZN<n>, k>, t> &choice_vec_shares,
    array<array<ZN<M>, k>, t> &output_shares) {
  MC_BEGIN(Proto, this, &sock, &ordIndexSet, &msg_vecs, &choice_vec_shares,
           &output_shares, oprfSendProto = Proto(), oprfRecvProto = Proto(),
           msg_vecs_masked_with_r = (array<VecMatrix<ZN<M>> *, t> *)nullptr,
           block_matrix = (array<VecMatrix<block> *, t> *)nullptr,
           okvs_structure = (vector<block> *)nullptr,
           h_vec = vector<block>(t * k));
  // std::cout << "(SENDER) SENDING OPRF" << std::endl;

  // std::cout << "spsot bytes sent: " << sock.bytesSent() << std::endl;
  // std::cout << "spsot bytes received: " << sock.bytesReceived() << std::endl;

  //	std::cout << "oprf sending" << std::endl;

  oprfSendProto = this->oprfSender->send(sock, k * tr);
  oprfRecvProto =
      sender_query_oprf<t, k>(sock, *(this->oprfReceiver), ordIndexSet, h_vec);
  MC_AWAIT(oprfSendProto);
  MC_AWAIT(oprfRecvProto);

  //	std::cout << "oprf sent 2" << std::endl;

  ZN<M>::template sample<t, k>(*(this->prng), output_shares);

  msg_vecs_masked_with_r =
      mask_msg_vectors_with_r_values<t, k, n, M>(output_shares, msg_vecs);

  //	std::cout << "msg_vecs_masked_with_r (s)" << std::endl;

  // std::cout << "sender output share: " << (output_shares[0][0].to_uint64_t())
  // << std::endl; std::cout << "sender msg vec: " <<
  // (msg_vecs[0][0][0]).to_uint64_t() << "," <<
  // (msg_vecs[0][0][1]).to_uint64_t() << std::endl; std::cout << "sender msg
  // vec masked with r: " <<
  // ((*msg_vecs_masked_with_r)[0]->row(0)[0]).to_uint64_t() << "," <<
  // ((*msg_vecs_masked_with_r)[0]->row(0)[1]).to_uint64_t() << std::endl;

  cshift_masked_matrices<n, M, k, t>(*msg_vecs_masked_with_r,
                                     choice_vec_shares);

  //	std::cout << "cshift_masked_matrices (s)" << std::endl;

  // std::cout << "cshifted sender msg vec masked with r: " <<
  // ((*msg_vecs_masked_with_r)[0]->row(0)[0]).to_uint64_t() << "," <<
  // ((*msg_vecs_masked_with_r)[0]->row(0)[1]).to_uint64_t() << std::endl;

  block_matrix = ZN_matrix_array_to_block_matrix_array<t, k, n, M>(
      *msg_vecs_masked_with_r);
  sparse_comp::free_array<VecMatrix<ZN<M>>, t>(msg_vecs_masked_with_r);

  //	std::cout << "ZN_matrix_array_to_block_matrix_array (s)" << std::endl;

  // std::cout << "shifted sender msg vec as block: " <<
  // (*block_matrix)[0]->row(0)[0] << " , " << (*block_matrix)[0]->row(0)[1] <<
  // std::endl;

  // std::cout << "spsot bytes sent: " << sock.bytesSent() << std::endl;
  // std::cout << "spsot bytes received: " << sock.bytesReceived() << std::endl;

  // std::cout << "(SENDER) OPRF SENT" << std::endl;

  mask_block_mtx_using_oprf<t, k, n>(*(this->oprfSender), ordIndexSet,
                                     *block_matrix);
  mask_block_mtx_using_h_vec<t, k, n>(*block_matrix, h_vec);

  //	std::cout << "mask_block_mtx_using_oprf (s)" << std::endl;

  // std::cout << "(SENDER) ENCODING OKVS AS PART OF SP SOT" << std::endl;

  okvs_structure = encode_okvs<t, k, n>(this->aes, ordIndexSet, *block_matrix);
  sparse_comp::free_array<VecMatrix<block>, t>(block_matrix);

  //	std::cout << "sending truncated okvs" << std::endl;

  MC_AWAIT(sendTruncatedOkvsStructure<M>(sock, *okvs_structure));

  //	std::cout << "sent truncated okvs" << std::endl;

  // REMBER TO FREE/DELETE ALLOCATED LISTS AND MATRICES!!!!!!

  delete okvs_structure;

  MC_END();
}

template <uint32_t ts, uint32_t tr, uint32_t k, size_t n>
static vector<block> *decode_okvs(const AES &aes,
                                  const vector<block> &ordIndexSet,
                                  array<array<ZN<n>, k>, tr> &choice_vec_shares,
                                  vector<block> &paxos_structure) {
  vector<block> okvs_idxs(tr * k);
  vector<block> *okvs_values = new vector<block>(tr * k);

  size_t g = 0;

  for (size_t i = 0; i < tr; i++) {
    array<ZN<n>, k> &choice_vec = choice_vec_shares[i];

    for (size_t j = 0; j < k; j++) {

      okvs_idxs[g] = sparse_comp::hash_point(aes, ordIndexSet[i], j,
                                             choice_vec[j].to_size_t());
      g++;
    }
  }

  // std::cout << "before block paxos decoding" << std::endl;
  // std::cout << paxos_structure.size() << std::endl;

  Baxos paxos;
  paxos.init(ts * k * n, sparse_comp::baxosBinSize(ts * k * n), 3, SSP,
             PaxosParam::GF128, oc::ZeroBlock);
  paxos.decode<block>(okvs_idxs, *okvs_values, paxos_structure);

  // std::cout << "receiver paxos decoded values:" << std::endl;

  // std::cout << "after block paxos decoding" << std::endl;

  return okvs_values;
}

template <uint32_t t, uint32_t k, size_t n, uint64_t M>
void extract_shares_from_okvs_values(
    CustomOPRFSender &oprfSender, vector<block> &ordIndexSet,
    vector<block> &okvs_values, vector<block> &oprf_values,
    array<array<ZN<M>, k>, t> &output_shares_mtx) {
  vector<block> h_oprf_vals(t * k);
  uint8_t log2M = ceil(log2(M));

  uint64_t mask = uint64_t(-1) >> (64 - log2M);

  size_t g = 0;

  // auto start = std::chrono::high_resolution_clock::now();
  // std::cout << "before oprfSender.eval" << std::endl;
  oprfSender.eval(ordIndexSet, k, h_oprf_vals);
  // auto end = std::chrono::high_resolution_clock::now();
  // std::chrono::duration<double> elapsed = end - start;
  // std::cout << "Time taken to execute oprfSender.eval: " << elapsed.count()
  // << " seconds" << std::endl;

  for (size_t i = 0; i < t; i++) {
    array<ZN<M>, k> &output_shares_row = output_shares_mtx.at(i);

    for (size_t j = 0; j < k; j++) {

      uint64_t u64_share =
          (okvs_values[g] ^ oprf_values[g] ^ h_oprf_vals[k * i + j])
              .get<uint64_t>()[0] &
          mask;
      output_shares_row[j] = ZN<M>(u64_share);

      g++;
    }
  }
}

template <size_t t, size_t k, size_t n>
Proto receiver_query_oprf(coproto::Socket &sock, OprfReceiver &oprfReceiver,
                          const vector<block> &ordIndexSet,
                          array<array<ZN<n>, k>, t> &choice_vec_shares,
                          vector<block> &oprf_vals) {
  MC_BEGIN(Proto, &sock, &oprfReceiver, &ordIndexSet, &choice_vec_shares,
           &oprf_vals, oprf_points = vector<oprf_point>(t * k), g = (size_t)0,
           sparse_point = point());

  g = 0;

  for (size_t i = 0; i < t; i++) {
    // std::cout << "i = " << i << std::endl;
    // std::cout << sparse_point.coords[0] << ";" << sparse_point.coords[1] <<
    // std::endl;
    array<ZN<n>, k> &choice_vec_share = choice_vec_shares[i];

    for (size_t j = 0; j < k; j++) {

      oprf_points[g] =
          oprf_point(ordIndexSet[i], j, choice_vec_share[j].to_size_t());
      // std::cout << "coord[0] = " << oprf_points[g].point.coords[0] << "
      // coord[1] = " << oprf_points[g].point.coords[1] << " sot_ix = " <<
      // (+oprf_points[g].sot_idx) << " sot_choice_share = " <<
      // (+oprf_points[g].sot_choice_share) << std::endl;

      g++;
    }
  }

  MC_AWAIT(oprfReceiver.receive(sock, t * k, oprf_points, oprf_vals));

  MC_END();
}

template <size_t ts, size_t t, size_t k, size_t n, uint64_t M>
static void internalReceive(const AES &aes, vector<block> &oprf_vals,
                            CustomOPRFSender &oprfSender,
                            vector<block> &paxos_structure,
                            vector<block> &ordIndexSet,
                            array<array<ZN<n>, k>, t> &choice_vec_shares,
                            array<array<ZN<M>, k>, t> &output_shares) {
  auto okvs_vals = decode_okvs<ts, t, k, n>(aes, ordIndexSet, choice_vec_shares,
                                            paxos_structure);

  extract_shares_from_okvs_values<t, k, n, M>(
      oprfSender, ordIndexSet, *okvs_vals, oprf_vals, output_shares);

  delete okvs_vals;
}

template <uint64_t M>
Proto receiveTruncatedOkvsStructure(coproto::Socket &sock,
                                    vector<block> &okvs_struct) {
  MC_BEGIN(Proto, &sock, &okvs_struct,
           truncated_okvs = (vector<uint64_t> *)nullptr, log2M = uint8_t(0),
           t = coproto::task<void>());
  log2M = ceil(log2(M));

  truncated_okvs = new vector<uint64_t>(
      calc_compact_okvs_struct_size(okvs_struct.size(), log2M));

  // std::cout << "received trucated okvs size: " <<
  // calc_compact_okvs_struct_size(okvs_struct.size(), log2M) << std::endl;

  // std::cout << "receiving truncated okvs (r)" << std::endl;

  t = sparse_comp::receive<uint64_t, sparse_comp::COPROTO_MAX_SEND_SIZE_BYTES>(
      sock, truncated_okvs->size(), *truncated_okvs);

  MC_AWAIT(t);

  // std::cout << "received truncated okvs (r)" << std::endl;

  // MC_AWAIT(sock.recvResize(*truncated_okvs));

  reconstruct_okvs(*truncated_okvs, log2M, okvs_struct);

  delete truncated_okvs;

  MC_END();
}

template <size_t ts, size_t tr, size_t k, size_t n, uint64_t M>
Proto sparse_comp::sp_bsot::Receiver<ts, tr, k, n, M>::receive(
    coproto::Socket &sock, vector<block> &ordIndexSet,
    array<array<ZN<n>, k>, tr> &choice_vec_shares,
    array<array<ZN<M>, k>, tr> &output_shares) {
  MC_BEGIN(Proto, this, &sock, &ordIndexSet, &choice_vec_shares, &output_shares,
           oprf_values = vector<block>(tr * k), paxos = Baxos{},
           paxos_structure = (vector<block> *)nullptr, proto = Proto(),
           oprfSendProto = Proto(), i = size_t(0));
  // std::cout << "(RECEIVER) BEFORE SP SOT OPRF QUERY" << std::endl;

  //	std::cout << "receiving oprf (r)" << std::endl;

  proto = receiver_query_oprf<tr, k, n>(
      sock, *(this->oprfReceiver), ordIndexSet, choice_vec_shares, oprf_values);

  oprfSendProto = this->oprfSender->send(sock, k * ts);

  MC_AWAIT(proto);
  MC_AWAIT(oprfSendProto);

  //	std::cout << "oprf received (r)" << std::endl;

  // std::cout << "(RECEIVER) AFTER SP SOT OPRF QUERY" << std::endl;

  paxos = Baxos();
  paxos.init(ts * k * n, sparse_comp::baxosBinSize(ts * k * n), 3, SSP,
             PaxosParam::GF128, oc::ZeroBlock);

  paxos_structure = new vector<block>(paxos.size());

  //	std::cout << "wainting truncated okvs (r)" << std::endl;

  MC_AWAIT(receiveTruncatedOkvsStructure<M>(sock, *paxos_structure));

  //	std::cout << "received truncated okvs (r)" << std::endl;

  internalReceive<ts, tr, k, n, M>(this->aes, oprf_values, *(this->oprfSender),
                                   *paxos_structure, ordIndexSet,
                                   choice_vec_shares, output_shares);

  //	std::cout << "internal received (r)" << std::endl;

  delete paxos_structure;

  MC_END();
}
