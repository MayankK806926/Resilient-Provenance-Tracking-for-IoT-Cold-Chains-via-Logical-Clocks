#pragma once
/// @file matrix_clock.hpp
/// @brief N×N matrix clock for tracking global knowledge and pruning horizons.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "coldchain/vector_clock.hpp"

namespace coldchain {

class MatrixClock {
public:
    explicit MatrixClock(size_t n = 4);

    void set_own_row(const VectorClock& vc, size_t self_idx);
    void merge_matrix(size_t self_idx, size_t sender_idx, const MatrixClock& incoming);

    uint32_t at(size_t j, size_t k) const;
    void set(size_t j, size_t k, uint32_t val);

    // Global knowledge horizon: min_j M[j][k] across all rows.
    VectorClock column_min() const;
    VectorClock row(size_t j) const;

    size_t n() const;
    std::string to_string() const;
    bool operator==(const MatrixClock& other) const;

private:
    size_t n_;
    std::vector<std::vector<uint32_t>> matrix_;
};

} // namespace coldchain
