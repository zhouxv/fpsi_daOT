#include "./FuzzyL1Pre.h"
#include "../Common/Common.h"
#include "../Common/HashUtils.h"
#include "./FuzzyL1.h"
#include <array>
#include <cstdint>
#include <iostream>
#include <vector>

template <size_t tr, size_t t, size_t d, uint8_t delta, uint8_t ssp>
Proto sparse_comp::fuzzy_l1_pre::Sender<tr, t, d, delta, ssp>::offline(
    Socket &sock, array<point, t> &points) {

  MC_BEGIN(Proto, this, &sock, &points, prt = Proto());
  // Create the SpL1 instance whose preprocessing state
  // will be reused in the online phase.
  this->spL1Sender = new SpL1SenderPre<rcvr_cell_count, t, d, delta, ssp>(
      *(this->prng), *(this->aes));

  // Perform input-dependent spatial hashing in the offline phase.
  sparse_comp::spatial_hash<t>(*(this->aes), points, this->point_hashs, d,
                               delta);
  // Convert the input points to the representation required by SpL1.
  this->in_values = new array<array<uint32_t, d>, t>();

  sndr_points_to_in_values<t, d>(points, *(this->in_values));

  // Perform the input-independent cryptographic preprocessing of SpL1.
  prt = this->spL1Sender->offline(sock);
  MC_AWAIT(prt);

  MC_END();
}

template <size_t ts, size_t t, size_t d, uint8_t delta, uint8_t ssp>
Proto sparse_comp::fuzzy_l1_pre::Receiver<ts, t, d, delta, ssp>::offline(
    Socket &sock, array<point, t> &points) {

  MC_BEGIN(Proto, this, &sock, &points, prt = Proto());

  // Create the SpL1 instance whose preprocessing state
  // will be reused in the online phase.
  this->spL1Receiver = new SpL1ReceiverPre<ts, cell_count, d, delta, ssp>(
      *(this->prng), *(this->aes));
  // Generate all candidate spatial cells for the receiver points.
  sparse_comp::spatial_cell_hash<t, d, cell_count>(*(this->aes), points,
                                                   this->cells, delta);

  // Convert the receiver points to the representation required by SpL1.
  this->in_values = new array<array<uint32_t, d>, cell_count>();

  rcvr_points_to_in_values<t, d, cell_count>(points, *(this->in_values));
  // Perform the input-independent cryptographic preprocessing of SpL1.
  prt = this->spL1Receiver->offline(sock);
  MC_AWAIT(prt);

  MC_END();
}

template <size_t tr, size_t t, size_t d, uint8_t delta, uint8_t ssp>
Proto sparse_comp::fuzzy_l1_pre::Sender<tr, t, d, delta, ssp>::online(
    Socket &sock, array<point, t> &points) {
  MC_BEGIN(Proto, this, &sock, &points,
           out_vec_shares = (array<array<block, 1>, t> *)nullptr,
           idx_okvs = vector<block>(), point_ctxs = vector<block>(),
           prt = Proto());

  out_vec_shares = new array<array<block, 1>, t>();

  // Execute the online SpL1 evaluation using the preprocessing
  // generated in the offline phase.
  prt = this->spL1Sender->online(sock, this->point_hashs, *(this->in_values),
                                 *out_vec_shares);

  MC_AWAIT(prt);
  // Construct the final OKVS and encrypted point payloads.
  compute_final_encryped_points<t, d, ssp>(*(this->aes), points,
                                           this->point_hashs, *out_vec_shares,
                                           idx_okvs, point_ctxs);

  // Send the final OKVS encoding.
  prt = sparse_comp::send<block, sparse_comp::COPROTO_MAX_SEND_SIZE_BYTES>(
      sock, idx_okvs);

  MC_AWAIT(prt);
  // Send the encrypted point payloads.
  prt = sparse_comp::send<block, sparse_comp::COPROTO_MAX_SEND_SIZE_BYTES>(
      sock, point_ctxs);

  MC_AWAIT(prt);

  delete out_vec_shares;

  // Release preprocessing data that is no longer needed.
  delete this->in_values;
  this->in_values = nullptr;

  delete this->spL1Sender;
  this->spL1Sender = nullptr;

  this->point_hashs.clear();

  MC_END();
}
template <size_t ts, size_t t, size_t d, uint8_t delta, uint8_t ssp>
Proto sparse_comp::fuzzy_l1_pre::Receiver<ts, t, d, delta, ssp>::online(
    Socket &sock, array<point, t> &points, vector<point> &intersec) {

  MC_BEGIN(Proto, this, &sock, &points, &intersec,
           out_vec_shares = (array<array<block, 1>, cell_count> *)nullptr,
           idx_okvs = vector<block>(), point_ctxs = vector<block>(),
           paxos = Baxos(), prt = Proto());
  out_vec_shares = new array<array<block, 1>, cell_count>();

  // Execute the online SpL1 evaluation using the preprocessing
  // generated in the offline phase.
  prt = this->spL1Receiver->online(sock, this->cells, *(this->in_values),
                                   *out_vec_shares);

  MC_AWAIT(prt);

  // Initialize the final Baxos instance used to decode the sender payload.
  paxos.init(ts, sparse_comp::baxosBinSize(ts), 3, ssp, PaxosParam::GF128,
             oc::ZeroBlock);
  idx_okvs.resize(paxos.size());

  point_ctxs.resize(ts * sparse_comp::point_encoding_block_count(d));

  // Receive the final OKVS encoding.
  prt = sparse_comp::receive<block, sparse_comp::COPROTO_MAX_SEND_SIZE_BYTES>(
      sock, idx_okvs.size(), idx_okvs);

  MC_AWAIT(prt);

  // Receive the encrypted point payloads.
  prt = sparse_comp::receive<block, sparse_comp::COPROTO_MAX_SEND_SIZE_BYTES>(
      sock, point_ctxs.size(), point_ctxs);

  MC_AWAIT(prt);
  // Decode the sender payloads and recover the fuzzy intersection.
  receiver_intersection<ts, t, d, cell_count, ssp>(
      *(this->aes), points, this->cells, *out_vec_shares, idx_okvs, point_ctxs,
      intersec);

  delete out_vec_shares;

  // Release preprocessing data that is no longer needed.
  delete this->in_values;
  this->in_values = nullptr;

  delete this->spL1Receiver;
  this->spL1Receiver = nullptr;

  this->cells.clear();

  MC_END();
}
