/// @file test_matrix_clock.cpp
/// @brief Unit tests for MatrixClock (N×N global knowledge propagation).

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "coldchain/matrix_clock.hpp"

using namespace coldchain;

TEST_CASE("MatrixClock initialization") {
    MatrixClock mc4;
    CHECK(mc4.n() == 4);
    for (size_t j = 0; j < 4; ++j) {
        for (size_t k = 0; k < 4; ++k) {
            CHECK(mc4.at(j, k) == 0);
        }
    }
}

TEST_CASE("MatrixClock set_own_row updates only targeted row") {
    MatrixClock mc(4);
    VectorClock vc(4);
    vc.tick(0);
    vc.tick(1);
    vc.tick(1);

    mc.set_own_row(vc, 1);

    CHECK(mc.at(1, 0) == 1);
    CHECK(mc.at(1, 1) == 2);
    CHECK(mc.at(1, 2) == 0);
    CHECK(mc.at(1, 3) == 0);

    // Other rows remain 0
    for (size_t j : {0, 2, 3}) {
        for (size_t k = 0; k < 4; ++k) {
            CHECK(mc.at(j, k) == 0);
        }
    }
}

TEST_CASE("MatrixClock merge_matrix never overwrites own row (receipt-based knowledge)") {
    MatrixClock mc_self(3);
    mc_self.set(0, 0, 10); // Own row: self_idx = 0

    MatrixClock mc_incoming(3);
    mc_incoming.set(0, 0, 99); // Sender claims higher knowledge of row 0
    mc_incoming.set(1, 1, 5);  // Sender knows row 1

    mc_self.merge_matrix(/*self_idx*/ 0, /*sender_idx*/ 1, mc_incoming);

    // Invariant: Node 0's own row (row 0) must NOT be overwritten by incoming gossip!
    CHECK(mc_self.at(0, 0) == 10);
    CHECK(mc_self.at(1, 1) == 5);
}

TEST_CASE("MatrixClock column_min hand-computed example") {
    MatrixClock mc(3);
    mc.set(0, 0, 5); mc.set(0, 1, 3); mc.set(0, 2, 2);
    mc.set(1, 0, 4); mc.set(1, 1, 6); mc.set(1, 2, 1);
    mc.set(2, 0, 3); mc.set(2, 1, 4); mc.set(2, 2, 7);

    VectorClock cmin = mc.column_min();
    CHECK(cmin.size() == 3);
    CHECK(cmin[0] == 3);
    CHECK(cmin[1] == 3);
    CHECK(cmin[2] == 1);
}

TEST_CASE("MatrixClock column_min all zeros when matrix is zero") {
    MatrixClock mc(4);
    VectorClock cmin = mc.column_min();
    for (size_t k = 0; k < 4; ++k)
        CHECK(cmin[k] == 0);
}

TEST_CASE("MatrixClock gossip convergence — after VC merge (message passing)") {
    const size_t N = 3;

    VectorClock vc0(N); vc0.tick(0); // [1,0,0]
    VectorClock vc1(N); vc1.merge(vc0); vc1.tick(1); // [1,1,0]
    VectorClock vc2(N); vc2.merge(vc1); vc2.tick(2); // [1,1,1]

    MatrixClock mc0(N), mc1(N), mc2(N);
    mc0.set_own_row(vc0, 0);
    mc1.set_own_row(vc1, 1);
    mc2.set_own_row(vc2, 2);

    for (int round = 0; round < 3; ++round) {
        MatrixClock s0 = mc0, s1 = mc1, s2 = mc2;
        mc0.merge_matrix(0, 1, s1); mc0.merge_matrix(0, 2, s2);
        mc1.merge_matrix(1, 0, s0); mc1.merge_matrix(1, 2, s2);
        mc2.merge_matrix(2, 0, s0); mc2.merge_matrix(2, 1, s1);
    }

    VectorClock cmin = mc0.column_min();
    CHECK(cmin[0] == 1);
    CHECK(cmin[1] == 0);
    CHECK(cmin[2] == 0);

    CHECK(mc1.column_min()[0] == 1);
    CHECK(mc2.column_min()[0] == 1);
}

TEST_CASE("MatrixClock row() returns correct VectorClock") {
    MatrixClock mc(3);
    mc.set(1, 0, 5);
    mc.set(1, 1, 3);
    mc.set(1, 2, 8);

    VectorClock row1 = mc.row(1);
    CHECK(row1[0] == 5);
    CHECK(row1[1] == 3);
    CHECK(row1[2] == 8);
}
