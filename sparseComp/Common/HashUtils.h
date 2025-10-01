#pragma once

#include "./Common.h"
#include "cryptoTools/Common/block.h"
#include "cryptoTools/Crypto/AES.h"
#include <array>
#include <vector>

using block = osuCrypto::block;
using AES = osuCrypto::AES;
using point = sparse_comp::point;

static constexpr size_t int_pow(size_t base, size_t exp) {
  return (exp == 0) ? 1 : base * int_pow(base, exp - 1);
}

namespace sparse_comp {

OC_FORCEINLINE block hash_point(const AES &aes, const point &pot) {
  static_assert(point::MAX_DIM == 10);

  block pointAsBlocks[3] = {block(0, 0), block(0, 0), block(0, 0)};

  memcpy(pointAsBlocks[0].data(), pot.coords, sizeof(uint32_t) * 4);
  memcpy(pointAsBlocks[1].data(), pot.coords + 4, sizeof(uint32_t) * 4);
  memcpy(pointAsBlocks[2].data(), pot.coords + 8, sizeof(uint32_t) * 2);

  block digest = aes.hashBlock(pointAsBlocks[0]);

  for (size_t i = 1; i < 3; i++) {
    digest = aes.hashBlock(digest ^ pointAsBlocks[i]);
  }

  return digest;
}

OC_FORCEINLINE block hash_point(const AES &aes, const block &pointHash,
                                size_t sot_idx, size_t msg_vec_idx) {

  block extra_block = block(sot_idx, msg_vec_idx);

  block digest = aes.hashBlock(extra_block ^ pointHash);

  return digest;
}

template <size_t t>
void hash_points(const AES &aes, std::vector<point> &points,
                 std::vector<block> &hashes) {
  static_assert(point::MAX_DIM == 10);

  hashes.resize(t);

  for (size_t i = 0; i < t; i++) {
    block pointAsBlocks[3] = {block(0, 0), block(0, 0), block(0, 0)};

    memcpy(pointAsBlocks[0].data(), points[i].coords, sizeof(uint32_t) * 4);
    memcpy(pointAsBlocks[1].data(), points[i].coords + 4, sizeof(uint32_t) * 4);
    memcpy(pointAsBlocks[2].data(), points[i].coords + 8, sizeof(uint32_t) * 2);

    block digest = aes.hashBlock(pointAsBlocks[0]);

    for (size_t j = 1; j < 3; j++) {
      digest = aes.hashBlock(digest ^ pointAsBlocks[j]);
    }

    hashes[i] = digest;
  }
}

inline point spatial_hash(const point &in_point, size_t point_dim,
                          uint8_t delta) {
  point out_point;
  out_point.coord_dim = point_dim;

  for (size_t j = 0; j < point_dim; j++) {
    out_point.coords[j] = in_point.coords[j] / ((uint32_t)(2 * delta));
  }

  return out_point;
}

template <size_t t>
inline void spatial_hash(const AES &hasher, std::array<point, t> &in_points,
                         std::vector<block> &out_points, size_t point_dim,
                         uint8_t delta) {
  out_points.resize(t);

  std::vector<point> spts_hashes(t);

  for (size_t i = 0; i < t; i++) {
    spts_hashes[i].coord_dim = point_dim;

    for (size_t j = 0; j < point_dim; j++) {
      spts_hashes[i].coords[j] =
          in_points[i].coords[j] / ((uint32_t)(2 * delta));
    }
  }

  hash_points<t>(hasher, spts_hashes, out_points);
}

template <size_t t, size_t d, size_t cell_count>
inline void spatial_cell_hash(const AES &hasher,
                              std::array<point, t> &point_center,
                              std::vector<block> &cells, uint8_t delta) {
  constexpr const size_t twotod = int_pow(2, d);

  static_assert(cell_count == twotod * t);
  static_assert(d <= point::MAX_DIM);

  std::vector<point> cells_as_points(cell_count);

  uint32_t u32_delta = ((uint32_t)delta);

  for (size_t i = 0; i < t; i++) {
    for (size_t j = 0; j < twotod; j++) {

      cells_as_points[twotod * i + j].coord_dim = point_center[i].coord_dim;

      for (size_t k = 0; k < d; k++) {

        cells_as_points[twotod * i + j].coords[k] =
            point_center[i].coords[k] / (2 * u32_delta);

        uint32_t b = ((j >> k) & 1);
        if (b == 0)
          continue;

        if ((point_center[i].coords[k] + u32_delta) / (2 * u32_delta) >
            (point_center[i].coords[k]) / (2 * u32_delta)) {
          cells_as_points[twotod * i + j].coords[k] += b;
        } else if (b == 1) {
          cells_as_points[twotod * i + j].coords[k] -= b;
        }
      }
    }
  }

  hash_points<cell_count>(hasher, cells_as_points, cells);
}

}; // namespace sparse_comp