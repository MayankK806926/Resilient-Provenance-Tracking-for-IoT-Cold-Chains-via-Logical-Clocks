/// @file test_ledger_integrity.cpp
/// @brief Hash chain integrity, Merkle root commitment, Merkle proof, and middle-block tamper detection.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "coldchain/events.hpp"
#include "coldchain/hashing.hpp"
#include "coldchain/ledger_block.hpp"
#include "coldchain/vector_clock.hpp"

using namespace coldchain;

static SensorEvent make_sensor(uint32_t shipment, uint8_t entity,
                                float temp, uint64_t counter,
                                VectorClock vc) {
    SensorEvent se;
    se.shipment_id            = shipment;
    se.origin_entity_id       = entity;
    se.temperature_celsius    = temp;
    se.latitude               = 13.0f;
    se.longitude              = 80.0f;
    se.local_monotonic_counter = counter;
    se.vc                     = std::move(vc);
    se.signature              = sign_event(se.serialize(), node_private_key(entity));
    se.id                     = se.compute_hash();
    return se;
}

TEST_CASE("sha256 produces deterministic 32-byte hashes") {
    auto h1 = sha256_bytes("coldchain test");
    auto h2 = sha256_bytes("coldchain test");
    CHECK(h1 == h2);
    CHECK(h1.size() == 32);
    CHECK(sha256_hex("coldchain test").size() == 64);
}

TEST_CASE("Merkle root computation and proof verification") {
    VectorClock vc(4);
    std::vector<LedgerEvent> events;
    for (size_t i = 0; i < 8; ++i) {
        vc.tick(i % 4);
        events.push_back(make_sensor(1, i % 4, 4.0f + static_cast<float>(i), i + 1, vc));
    }

    auto blk = seal_block(0, events, genesis_hash(), 4);
    CHECK(blk.merkle_root != Hash256{});

    // Test Merkle proof for each individual event
    for (size_t i = 0; i < events.size(); ++i) {
        auto proof = generate_merkle_proof(blk, i);
        Hash256 target_hash = event_hash(blk.events[i]);

        CHECK(verify_merkle_proof(blk.merkle_root, target_hash, proof, i));

        // Forged target hash should fail verification
        Hash256 fake_hash = target_hash;
        fake_hash[0] ^= 0xFF;
        CHECK_FALSE(verify_merkle_proof(blk.merkle_root, fake_hash, proof, i));
    }
}

TEST_CASE("Multi-block chain integrity and middle-block tamper detection") {
    VectorClock vc(4);
    std::vector<LedgerBlock> chain;
    Hash256 prev = genesis_hash();

    // Seal 5 consecutive blocks
    for (uint64_t b = 0; b < 5; ++b) {
        std::vector<LedgerEvent> events;
        for (size_t e = 0; e < 4; ++e) {
            vc.tick(e);
            events.push_back(make_sensor(1, e, 4.5f, b * 4 + e + 1, vc));
        }
        auto blk = seal_block(b, events, prev, 4);
        prev = blk.this_hash;
        chain.push_back(std::move(blk));
    }

    REQUIRE(chain.size() == 5);
    CHECK(verify_chain(chain));

    // Tamper with middle block (Block 2): modify an event's temperature
    auto tampered_chain = chain;
    auto* se = std::get_if<SensorEvent>(&tampered_chain[2].events[1]);
    REQUIRE(se != nullptr);
    se->temperature_celsius = 25.0f; // Excursion injection / tampering!

    // Chain verification must detect the tampering
    CHECK_FALSE(verify_chain(tampered_chain));
}

TEST_CASE("Broken chain linkage detected") {
    VectorClock vc(4);
    std::vector<LedgerBlock> chain;
    Hash256 prev = genesis_hash();

    for (uint64_t b = 0; b < 3; ++b) {
        std::vector<LedgerEvent> events = {make_sensor(1, 0, 4.0f, b + 1, vc)};
        auto blk = seal_block(b, events, prev, 4);
        prev = blk.this_hash;
        chain.push_back(std::move(blk));
    }

    // Corrupt block 2's prev_hash pointer
    chain[2].prev_hash[0] ^= 0xEE;
    CHECK_FALSE(verify_chain(chain));
}
