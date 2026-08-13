#include "./BlockSpBSOT.h"
#include "../Common/BaxosUtils.h"
#include "../Common/Common.h"
#include "../Common/HashUtils.h"
#include "../Common/SockUtils.h"
#include "../Common/VecMatrix.h"
#include "../CustomOPRF/CustomizedOPRF.h"
#include "../SpBSOT/SpBSOT.h"
#include "cryptoTools/Crypto/AES.h"
#include "volePSI/Paxos.h"
#include <array>
#include <iostream>
#include <utility>
#include <vector>

#define SSP 40
// #define BLOCK_SP_SOT_PAXOS_BIN_SIZE 1 << 14
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
/*
template<size_t t,size_t d, uint8_t l, uint64_t M>
void print_vec_shares(array<array<ZN<M>,d*l>,t> vec_shares) {

    for (size_t i=0;i < t;i++) {

        std::cout << "sparse point idx" << i << std::endl;

        for (size_t j=0;j < d;j++) {

            std::cout << "    batch idx: " << j << std::endl << "        ";

            for (size_t h=0;h < l;h++) {

                std::cout << vec_shares[i][j*l + h].to_uint64_t() << " ";

            }

            std::cout << std::endl;

        }

    }

}
*/

template <size_t t, size_t k, size_t n>
std::array<VecMatrix<block> *, t> *
mask_msg_vectors_with_r_values(array<array<block, k>, t> &r_matrix,
                               array<array<array<block, n>, k>, t> &msg_vecs) {
  std::array<VecMatrix<block> *, t> *msg_vecs_plus_r =
      new std::array<VecMatrix<block> *, t>();

  for (size_t i = 0; i < t; i++) {

    msg_vecs_plus_r->at(i) = new VecMatrix<block>(k, n);

    for (size_t j = 0; j < k; j++) {

      for (size_t h = 0; h < n; h++) {
        msg_vecs_plus_r->at(i)->row(j)[h] = msg_vecs[i][j][h] ^ r_matrix[i][j];
      }
    }
  }

  return msg_vecs_plus_r;
}

template <size_t n, uint32_t k, uint32_t t>
static void
cshift_masked_matrices(std::array<VecMatrix<block> *, t> &masked_mtxs,
                       array<std::array<ZN<n>, k>, t> &choice_vec_shares) {

  for (size_t i = 0; i < t; i++) {

    VecMatrix<block> &mtx = *(masked_mtxs[i]);
    std::array<ZN<n>, k> &offset_vec = choice_vec_shares[i];

    for (size_t j = 0; j < k; j++) {

      mtx.cshift(j, offset_vec[j].to_size_t());
    }
  }
}

/*
template<size_t k, size_t n>
inline void xor_block_vec_mtxs(VecMatrix<block>& block_mtx, VecMatrix<block>&
mask_mtx) {

    for (size_t i=0;i < k;i++) {

        std::vector<block>& block_row = block_mtx[i];
        auto mask_row = mask_mtx[i];

        for (size_t j=0;j < n;j++) {

            block_row[j] = block_row[j] ^ mask_row[j];

        }
    }

}
*/

/*
template<size_t t, size_t k, size_t n>
void mask_block_mtx_using_oprf(OprfSender& oprfSender, const array<point,t>&
sparse_points, array<VecMatrix<block>*,t>& block_mtxs) { VecMatrix<block>
mask_mtx(k,n);

    for(size_t i=0;i < t;i++) {
        VecMatrix<block>* block_mtx = block_mtxs[i];
        point sparse_point = sparse_points[i];

        oprfSender.eval(sparse_point, k, n, mask_mtx);

        xor_block_vec_mtxs<k,n>(*block_mtx, mask_mtx);
    }

}
*/

template <size_t t, size_t k, size_t n>
static vector<block> *
tmp_encode_okvs(const AES &aes, const vector<block> &pointHashes,
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

  paxos.solve<block>(okvs_idxs, okvs_values, *paxos_structure, nullptr,
                     NUM_THREADS);

  // std::cout << "TEST" << std::endl;

  return paxos_structure;
}

Proto sendOkvsStructure(Socket &sock, vector<block> &paxos_structure) {
  MC_BEGIN(Proto, &sock, &paxos_structure, t = coproto::task<void>());

  t = sparse_comp::send<block, sparse_comp::COPROTO_MAX_SEND_SIZE_BYTES>(
      sock, paxos_structure);

  MC_AWAIT(t);

  MC_END();
}

template <size_t t, size_t k>
void sample_output_shares(PRNG &prng,
                          array<array<block, k>, t> &output_shares) {

  for (size_t i = 0; i < t; i++) {
    for (size_t j = 0; j < k; j++) {
      output_shares[i][j] = prng.get<block>();
    }
  }
}

template <size_t tr, size_t t, size_t k, size_t n>
Proto sparse_comp::block_sp_bsot::Sender<tr, t, k, n>::send(
    coproto::Socket &sock, vector<block> &ordIndexSet,
    array<array<array<block, n>, k>, t> &msg_vecs,
    array<array<ZN<n>, k>, t> &choice_vec_shares,
    array<array<block, k>, t> &output_shares) {
  MC_BEGIN(Proto, this, &sock, &ordIndexSet, &msg_vecs, &choice_vec_shares,
           &output_shares, oprfSendProto = Proto(), oprfRecvProto = Proto(),
           block_matrix = (array<VecMatrix<block> *, t> *)nullptr,
           okvs_structure = (vector<block> *)nullptr,
           h_vec = vector<block>(t * k));
  // std::cout << "(SENDER) SENDING OPRF" << std::endl;

  oprfSendProto = this->oprfSender->send(sock, k * tr);
  oprfRecvProto =
      sender_query_oprf<t, k>(sock, *(this->oprfReceiver), ordIndexSet, h_vec);

  sample_output_shares(*(this->prng), output_shares);

  block_matrix =
      mask_msg_vectors_with_r_values<t, k, n>(output_shares, msg_vecs);

  // std::cout << (output_shares[0][0].to_uint64_t()) << std::endl;
  // std::cout << (msg_vecs[0][0][50]).to_uint64_t() << std::endl;
  // std::cout << ((*msg_vecs_masked_with_r)[0]->row(0)[50]).to_uint64_t() <<
  // std::endl;

  cshift_masked_matrices<n, k, t>(*block_matrix, choice_vec_shares);

  // std::cout << ((*msg_vecs_masked_with_r)[0]->row(0)[50]).to_uint64_t() <<
  // std::endl;

  MC_AWAIT(oprfSendProto);
  MC_AWAIT(oprfRecvProto);

  // std::cout << "(SENDER) OPRF SENT" << std::endl;

  mask_block_mtx_using_oprf<t, k, n>(*(this->oprfSender), ordIndexSet,
                                     *block_matrix);
  mask_block_mtx_using_h_vec<t, k, n>(*block_matrix, h_vec);

  // std::cout << "(SENDER) ENCODING OKVS AS PART OF SP SOT" << std::endl;

  okvs_structure =
      tmp_encode_okvs<t, k, n>(this->aes, ordIndexSet, *block_matrix);

  // std::cout << okvs_structure->size() << std::endl;

  MC_AWAIT(sendOkvsStructure(sock, *okvs_structure));

  // REMBER TO FREE/DELETE ALLOCATED LISTS AND MATRICES!!!!!!

  sparse_comp::free_array<VecMatrix<block>, t>(block_matrix);
  delete okvs_structure;

  MC_END();
}

template <uint32_t ts, uint32_t tr, uint32_t k, size_t n>
static vector<block> *
tmp_decode_okvs(const AES &aes, const vector<block> &ordIndexSet,
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

template <uint32_t t, uint32_t k, size_t n>
void extract_shares_from_okvs_values(
    CustomOPRFSender &oprfSender, vector<block> &ordIndexSet,
    vector<block> &okvs_values, vector<block> &oprf_values,
    array<array<block, k>, t> &output_shares_mtx) {
  vector<block> h_oprf_vals(t * k);

  size_t g = 0;

  oprfSender.eval(ordIndexSet, k, h_oprf_vals);

  for (size_t i = 0; i < t; i++) {
    array<block, k> &output_shares_row = output_shares_mtx.at(i);

    for (size_t j = 0; j < k; j++) {

      output_shares_row[j] =
          okvs_values[g] ^ oprf_values[g] ^ h_oprf_vals[k * i + j];

      g++;
    }
  }
}

template <size_t ts, size_t tr, size_t k, size_t n>
static void internalReceive(AES &aes, CustomOPRFSender &oprfSender,
                            vector<block> &oprf_vals,
                            vector<block> &paxos_structure,
                            vector<block> &ordIndexSet,
                            array<array<ZN<n>, k>, tr> &choice_vec_shares,
                            array<array<block, k>, tr> &output_shares) {
  auto okvs_vals = tmp_decode_okvs<ts, tr, k, n>(
      aes, ordIndexSet, choice_vec_shares, paxos_structure);

  extract_shares_from_okvs_values<tr, k, n>(oprfSender, ordIndexSet, *okvs_vals,
                                            oprf_vals, output_shares);

  delete okvs_vals;
}

Proto receiveOkvsStructure(coproto::Socket &sock,
                           vector<block> &paxos_structure) {
  MC_BEGIN(Proto, &sock, &paxos_structure, t = coproto::task<void>());

  t = sparse_comp::receive<block, sparse_comp::COPROTO_MAX_SEND_SIZE_BYTES>(
      sock, paxos_structure.size(), paxos_structure);
  MC_AWAIT(t);

  MC_END();
}

template <size_t ts, size_t tr, size_t k, size_t n>
Proto sparse_comp::block_sp_bsot::Receiver<ts, tr, k, n>::receive(
    coproto::Socket &sock, vector<block> &ordIndexSet,
    array<array<ZN<n>, k>, tr> &choice_vec_shares,
    array<array<block, k>, tr> &output_shares) {
  MC_BEGIN(Proto, this, &sock, &ordIndexSet, &choice_vec_shares, &output_shares,
           oprf_values = vector<block>(tr * k), paxos = Baxos{},
           paxos_structure = (vector<block> *)nullptr, proto = Proto(),
           oprfSendProto = Proto());

  // std::cout << "(RECEIVER) BEFORE SP SOT OPRF QUERY" << std::endl;

  proto = receiver_query_oprf<tr, k, n>(
      sock, *(this->oprfReceiver), ordIndexSet, choice_vec_shares, oprf_values);

  oprfSendProto = this->oprfSender->send(sock, k * ts);

  MC_AWAIT(proto);
  MC_AWAIT(oprfSendProto);

  // std::cout << "(RECEIVER) AFTER SP SOT OPRF QUERY" << std::endl;

  paxos = Baxos();
  paxos.init(ts * k * n, sparse_comp::baxosBinSize(ts * k * n), 3, SSP,
             PaxosParam::GF128, oc::ZeroBlock);

  paxos_structure = new vector<block>(paxos.size());

  MC_AWAIT(receiveOkvsStructure(sock, *paxos_structure));

  // std::cout << "block receive before internal" << std::endl;

  internalReceive<ts, tr, k, n>(this->aes, *(this->oprfSender), oprf_values,
                                *paxos_structure, ordIndexSet,
                                choice_vec_shares, output_shares);

  delete paxos_structure;

  MC_END();
}
