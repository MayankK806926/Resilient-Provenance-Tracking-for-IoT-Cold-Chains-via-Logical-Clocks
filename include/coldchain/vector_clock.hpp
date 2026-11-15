#pragma once
/// @file vector_clock.hpp
/// @brief Fidge-Mattern vector clock for causal ordering.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace coldchain {

class VectorClock {
public:
    explicit VectorClock(size_t n = 4);
    explicit VectorClock(std::vector<uint32_t> clock);

    void tick(size_t node_idx);
    void merge(const VectorClock& other);

    bool precedes(const VectorClock& other) const;
    bool is_concurrent(const VectorClock& other) const;
    bool equals(const VectorClock& other) const;
    bool dominates_or_equal(const VectorClock& other) const;

    uint32_t operator[](size_t idx) const;
    void set(size_t idx, uint32_t val);

    size_t size() const;
    uint32_t sum() const;
    const std::vector<uint32_t>& values() const;
    std::string to_string() const;

    // Lexicographic ordering for containers/tie-breaking (NOT causal order).
    bool operator<(const VectorClock& other) const;
    bool operator==(const VectorClock& other) const;
    bool operator!=(const VectorClock& other) const;

private:
    std::vector<uint32_t> clock_;
};

} // namespace coldchain
