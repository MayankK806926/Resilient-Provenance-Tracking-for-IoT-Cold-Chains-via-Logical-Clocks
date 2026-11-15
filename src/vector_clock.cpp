/// @file vector_clock.cpp
/// @brief VectorClock implementation — Fidge-Mattern logical clock operations.

#include "coldchain/vector_clock.hpp"

#include <algorithm>
#include <cassert>
#include <sstream>

namespace coldchain {

VectorClock::VectorClock(size_t n) : clock_(n, 0) {}

VectorClock::VectorClock(std::vector<uint32_t> clock) : clock_(std::move(clock)) {}

void VectorClock::tick(size_t node_idx) {
    assert(node_idx < clock_.size() && "tick: index out of bounds");
    assert(clock_[node_idx] < UINT32_MAX && "tick: overflow guard");
    ++clock_[node_idx];
}

void VectorClock::set(size_t idx, uint32_t val) {
    assert(idx < clock_.size() && "set: index out of bounds");
    clock_[idx] = val;
}

void VectorClock::merge(const VectorClock& other) {
    assert(clock_.size() == other.clock_.size() && "merge: dimension mismatch");
    for (size_t i = 0; i < clock_.size(); ++i)
        clock_[i] = std::max(clock_[i], other.clock_[i]);
}

bool VectorClock::precedes(const VectorClock& other) const {
    assert(clock_.size() == other.clock_.size());
    // ∀k: this[k] <= other[k]  AND  ∃k: this[k] < other[k]
    bool at_least_one_strict = false;
    for (size_t k = 0; k < clock_.size(); ++k) {
        if (clock_[k] > other.clock_[k]) return false;
        if (clock_[k] < other.clock_[k]) at_least_one_strict = true;
    }
    return at_least_one_strict;
}

bool VectorClock::dominates_or_equal(const VectorClock& other) const {
    assert(clock_.size() == other.clock_.size());
    for (size_t k = 0; k < clock_.size(); ++k) {
        if (clock_[k] < other.clock_[k]) return false;
    }
    return true;
}

bool VectorClock::is_concurrent(const VectorClock& other) const {
    return !this->precedes(other) &&
           !other.precedes(*this) &&
           !this->equals(other);
}

bool VectorClock::equals(const VectorClock& other) const {
    return clock_ == other.clock_;
}

uint32_t VectorClock::operator[](size_t idx) const {
    assert(idx < clock_.size());
    return clock_[idx];
}

size_t VectorClock::size() const { return clock_.size(); }

uint32_t VectorClock::sum() const {
    uint32_t s = 0;
    for (uint32_t v : clock_) s += v;
    return s;
}

const std::vector<uint32_t>& VectorClock::values() const {
    return clock_;
}

std::string VectorClock::to_string() const {
    std::ostringstream os;
    os << '[';
    for (size_t i = 0; i < clock_.size(); ++i) {
        if (i) os << ',';
        os << clock_[i];
    }
    os << ']';
    return os.str();
}

bool VectorClock::operator<(const VectorClock& other) const {
    // Lexicographic — NOT causal. For container ordering only.
    return clock_ < other.clock_;
}

bool VectorClock::operator==(const VectorClock& other) const {
    return clock_ == other.clock_;
}

bool VectorClock::operator!=(const VectorClock& other) const {
    return clock_ != other.clock_;
}

} // namespace coldchain
