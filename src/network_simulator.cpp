/// @file network_simulator.cpp
/// @brief Discrete-event cold-chain network simulator implementation.

#include "coldchain/network_simulator.hpp"
#include "coldchain/causal_merge_engine.hpp"
#include "coldchain/pruning_engine.hpp"
#include <iostream>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <stdexcept>
#include <string>

namespace coldchain {

NetworkSimulator::NetworkSimulator(const SimConfig& cfg)
    : cfg_(cfg), rng_(cfg.seed) {
    for (size_t i = 0; i < cfg_.num_nodes; ++i) {
        vclock_nodes_.emplace_back(static_cast<uint8_t>(i), cfg_.num_nodes, cfg_.events_per_block, cfg_.num_shipments);
    }
    entity_lamport_clocks_.resize(cfg_.num_nodes, 0);
}

void NetworkSimulator::add_shipment(uint32_t shipment_id, std::vector<uint8_t> route, double start_offset_hours) {
    assert(!route.empty());
    Shipment ship;
    ship.shipment_id    = shipment_id;
    ship.route          = std::move(route);
    ship.current_leg_idx = 0;
    ship.start_time_ms  = start_offset_hours * 3600.0 * 1000.0;
    ship.next_sample_ms = ship.start_time_ms;
    ship.finished       = false;

    // Allocate legs
    double total_sim_ms = cfg_.duration_hours * 3600.0 * 1000.0;
    double remaining_ms = total_sim_ms - ship.start_time_ms;
    if (remaining_ms <= 0.0) remaining_ms = 1000.0;

    size_t num_legs = ship.route.size();
    double base_leg_ms = remaining_ms / static_cast<double>(num_legs);

    double current_start = ship.start_time_ms;
    for (size_t leg_i = 0; leg_i < num_legs; ++leg_i) {
        double leg_duration = base_leg_ms;
        if (cfg_.randomize_leg_lengths && num_legs > 1 && leg_i + 1 < num_legs) {
            std::uniform_real_distribution<double> perturb(0.7, 1.3);
            leg_duration *= perturb(rng_);
        }
        double leg_end = (leg_i + 1 == num_legs) ? total_sim_ms : (current_start + leg_duration);
        ship.legs.push_back({ship.route[leg_i], current_start, leg_end});

        CustodyInterval ci;
        ci.shipment_id   = shipment_id;
        ci.custodian_id  = ship.route[leg_i];
        ci.start_ms      = current_start;
        ci.end_ms        = leg_end;
        custody_intervals_.push_back(ci);

        current_start = leg_end;
    }

    shipments_.push_back(ship);

    // Create a dedicated IoT logger device for this shipment
    shipment_loggers_.emplace_back(shipment_id, ship.route.front(), cfg_.num_nodes);

    // Sample distinct physical clock drift (between drift_ppm_min and drift_ppm_max)
    std::uniform_real_distribution<double> drift_dist(cfg_.drift_ppm_min, cfg_.drift_ppm_max);
    double drift = drift_dist(rng_);
    if (shipment_id % 2 == 1) drift = -drift; // Alternate positive / negative drift

    std::uniform_real_distribution<double> offset_dist(-cfg_.initial_offset_max_s * 1e9,
                                                        cfg_.initial_offset_max_s * 1e9);
    int64_t initial_offset_ns = static_cast<int64_t>(offset_dist(rng_));

    shipment_clocks_.emplace_back(drift, initial_offset_ns, cfg_.temp_dependent_drift, cfg_.seed + shipment_id * 100);
}

void NetworkSimulator::setup_shipments() {
    if (!shipments_.empty()) return;

    std::vector<uint8_t> default_route;
    for (uint8_t i = 0; i < static_cast<uint8_t>(cfg_.num_nodes); ++i) {
        default_route.push_back(i);
    }

    std::uniform_real_distribution<double> stagger_dist(0.0, cfg_.stagger_start_max_hours);
    for (uint32_t s = 1; s <= cfg_.num_shipments; ++s) {
        double stagger_hours = (cfg_.stagger_start_max_hours > 0.0) ? stagger_dist(rng_) : 0.0;
        add_shipment(s, default_route, stagger_hours);
    }
}

void NetworkSimulator::inject_blackout(uint8_t node_id, double duration_ms) {
    BlackoutPeriod bp;
    bp.start_ms  = sim_clock_ms_;
    bp.end_ms    = sim_clock_ms_ + duration_ms;
    bp.entity_id = node_id;
    blackouts_.push_back(bp);
    ++total_blackouts_;
}

void NetworkSimulator::inject_partition(uint8_t node_a, uint8_t node_b, double duration_ms) {
    NetworkPartition np;
    np.start_ms = sim_clock_ms_;
    np.end_ms   = sim_clock_ms_ + duration_ms;
    np.node_a   = node_a;
    np.node_b   = node_b;
    partitions_.push_back(np);
    ++total_partitions_;
}

void NetworkSimulator::generate_failure_events() {
    double total_ms = cfg_.duration_hours * 3600.0 * 1000.0;

    // 1. Device-to-gateway blackouts (Poisson process per custodian)
    if (cfg_.blackout_rate_per_hour > 0.0) {
        double rate_per_ms = cfg_.blackout_rate_per_hour / (3600.0 * 1000.0);
        std::exponential_distribution<double> inter_arrival(rate_per_ms);

        for (size_t node = 0; node < cfg_.num_nodes; ++node) {
            double t = inter_arrival(rng_);
            while (t < total_ms) {
                double dur_ms = cfg_.blackout_mean_hours * 3600.0 * 1000.0;
                if (cfg_.blackout_stddev_hours > 0.0 && cfg_.blackout_mean_hours > 0.0) {
                    std::lognormal_distribution<double> dist(
                        std::log(dur_ms),
                        cfg_.blackout_stddev_hours / cfg_.blackout_mean_hours);
                    dur_ms = std::max(5000.0, std::abs(dist(rng_)));
                }

                BlackoutPeriod bp;
                bp.start_ms  = t;
                bp.end_ms    = t + dur_ms;
                bp.entity_id = static_cast<uint8_t>(node);
                blackouts_.push_back(bp);
                ++total_blackouts_;

                t += dur_ms + inter_arrival(rng_);
            }
        }
    }

    // 2. Node-to-node network partitions
    if (cfg_.partition_rate_per_hour > 0.0 && cfg_.num_nodes > 1) {
        double rate_per_ms = cfg_.partition_rate_per_hour / (3600.0 * 1000.0);
        std::exponential_distribution<double> inter_arrival(rate_per_ms);

        for (size_t a = 0; a < cfg_.num_nodes; ++a) {
            for (size_t b = a + 1; b < cfg_.num_nodes; ++b) {
                double t = inter_arrival(rng_);
                while (t < total_ms) {
                    double dur_ms = cfg_.partition_mean_hours * 3600.0 * 1000.0;
                    NetworkPartition np;
                    np.start_ms = t;
                    np.end_ms   = t + dur_ms;
                    np.node_a   = static_cast<uint8_t>(a);
                    np.node_b   = static_cast<uint8_t>(b);
                    partitions_.push_back(np);
                    ++total_partitions_;
                    t += dur_ms + inter_arrival(rng_);
                }
            }
        }
    }

    // 3. Contiguous temperature excursion episodes
    if (cfg_.excursion_rate_per_hour > 0.0) {
        double rate_per_ms = cfg_.excursion_rate_per_hour / (3600.0 * 1000.0);
        std::exponential_distribution<double> inter_arrival(rate_per_ms);

        for (size_t s_idx = 0; s_idx < shipments_.size(); ++s_idx) {
            double t = inter_arrival(rng_);
            while (t < total_ms) {
                double dur_ms = cfg_.excursion_duration_mean_min * 60.0 * 1000.0;
                std::uniform_real_distribution<float> peak_dist(
                    cfg_.temp_high_c + 3.0f, cfg_.temp_high_c + 12.0f);

                ExcursionEpisode ep;
                ep.shipment_id = shipments_[s_idx].shipment_id;
                ep.start_ms    = t;
                ep.end_ms      = t + dur_ms;
                ep.peak_temp_c = peak_dist(rng_);
                excursion_episodes_.push_back(ep);

                t += dur_ms + inter_arrival(rng_);
            }
        }
    }
}

bool NetworkSimulator::is_node_in_blackout(uint8_t node_id, double time_ms) const {
    for (const auto& bo : blackouts_) {
        if (bo.entity_id == node_id && time_ms >= bo.start_ms && time_ms < bo.end_ms)
            return true;
    }
    return false;
}

bool NetworkSimulator::is_partitioned(uint8_t node_a, uint8_t node_b, double time_ms) const {
    for (const auto& np : partitions_) {
        if (((np.node_a == node_a && np.node_b == node_b) ||
             (np.node_a == node_b && np.node_b == node_a)) &&
            time_ms >= np.start_ms && time_ms < np.end_ms) {
            return true;
        }
    }
    return false;
}

float NetworkSimulator::sample_ambient_temperature(uint32_t shipment_id, double time_ms) {
    // Check if an excursion episode is active for this shipment
    for (const auto& ep : excursion_episodes_) {
        if (ep.shipment_id == shipment_id && time_ms >= ep.start_ms && time_ms < ep.end_ms) {
            // Smooth bell curve temperature excursion
            double progress = (time_ms - ep.start_ms) / (ep.end_ms - ep.start_ms);
            float delta = (ep.peak_temp_c - cfg_.temp_high_c) * static_cast<float>(std::sin(progress * 3.14159265));
            return cfg_.temp_high_c + delta;
        }
    }

    // Normal safe ambient range with Gaussian perturbation
    float nominal_mid = (cfg_.temp_low_c + cfg_.temp_high_c) * 0.5f;
    std::normal_distribution<float> norm_dist(nominal_mid, (cfg_.temp_high_c - cfg_.temp_low_c) * 0.15f);
    float t = norm_dist(rng_);
    return std::clamp(t, cfg_.temp_low_c + 0.1f, cfg_.temp_high_c - 0.1f);
}

double NetworkSimulator::sample_tx_jitter() {
    if (cfg_.tx_jitter_mean_ms <= 0.0) return 0.0;
    if (cfg_.tx_jitter_stddev_ms <= 0.0) return cfg_.tx_jitter_mean_ms;
    std::lognormal_distribution<double> dist(
        std::log(cfg_.tx_jitter_mean_ms),
        cfg_.tx_jitter_stddev_ms / cfg_.tx_jitter_mean_ms
    );
    return std::abs(dist(rng_));
}

double NetworkSimulator::sample_handoff_delay() {
    if (cfg_.handoff_delay_mean_ms <= 0.0) return 0.0;
    std::exponential_distribution<double> dist(1.0 / cfg_.handoff_delay_mean_ms);
    return dist(rng_);
}

void NetworkSimulator::broadcast_event(const LedgerEvent& ev, uint8_t origin_node) {
    // Broadcast event to ALL nodes over simulated network links
    for (size_t target = 0; target < cfg_.num_nodes; ++target) {
        double delay = (target == origin_node) ? 0.0 : sample_tx_jitter();
        if (cfg_.link_loss_probability > 0.0 && target != origin_node) {
            std::uniform_real_distribution<double> loss_dist(0.0, 1.0);
            while (loss_dist(rng_) < cfg_.link_loss_probability) {
                delay += 200.0; // Reliable transport / TCP retransmission timeout delay
            }
        }

        ScheduledMessage msg;
        msg.fire_time_ms = sim_clock_ms_ + delay;
        msg.from_node    = origin_node;
        msg.to_node      = static_cast<uint8_t>(target);
        msg.payload      = ev;
        msg.is_gossip    = false;
        message_queue_.push(msg);
    }
}

void NetworkSimulator::process_telemetry(Shipment& ship) {
    if (ship.finished || ship.current_leg_idx >= ship.legs.size()) return;

    size_t s_idx = ship.shipment_id - 1;
    auto& device = shipment_loggers_[s_idx];
    auto& clock  = shipment_clocks_[s_idx];

    const auto& leg = ship.legs[ship.current_leg_idx];
    uint8_t current_custodian = leg.custodian;
    device.set_current_custodian(current_custodian);

    bool was_offline = !device.is_online();
    bool blacked_out = is_node_in_blackout(current_custodian, sim_clock_ms_);
    device.set_online(!blacked_out);

    uint64_t sim_time_ns = static_cast<uint64_t>(sim_clock_ms_ * 1e6);

    // Reconnection flush: flush any offline buffered readings immediately when connectivity is restored
    if (was_offline && !blacked_out && device.buffered_count() > 0) {
        if (cfg_.resync_at_reconnect) {
            clock.resync(sim_time_ns);
        }
        auto buffered = device.flush_on_reconnect();
        double base_jitter = sample_tx_jitter();
        for (size_t bi = 0; bi < buffered.size(); ++bi) {
            auto& b_se = buffered[bi];
            uint64_t upload_offset_ns = static_cast<uint64_t>(bi) * 1'000'000ULL;
            if (cfg_.restamp_physical_on_upload) {
                b_se.physical_timestamp_ns = clock.now_ns(sim_time_ns, b_se.temperature_celsius) + upload_offset_ns;
            }

            for (size_t target = 0; target < cfg_.num_nodes; ++target) {
                ScheduledMessage msg;
                msg.fire_time_ms = sim_clock_ms_ + base_jitter + static_cast<double>(bi) * 0.1;
                msg.from_node    = current_custodian;
                msg.to_node      = static_cast<uint8_t>(target);
                msg.payload      = b_se;
                msg.is_gossip    = false;
                message_queue_.push(msg);
            }
        }
    }

    float temp = sample_ambient_temperature(ship.shipment_id, sim_clock_ms_);
    float lat = 12.97f + 0.1f * current_custodian;
    float lon = 77.59f + 0.1f * current_custodian;

    // IoT device logs telemetry using its dedicated local clock
    SensorEvent se = device.generate_telemetry(temp, lat, lon, sim_time_ns, &clock);

    // Record ground truth event and register causal dependency
    size_t new_idx = ground_truth_.size();
    ground_truth_.push_back(se);
    event_index_map_[se.id] = new_idx;

    // Expand ground truth causality matrix: all previously generated events for this shipment precede this event
    gt_causality_matrix_.resize(new_idx + 1);
    for (auto& row : gt_causality_matrix_) {
        row.resize(new_idx + 1, false);
    }
    for (size_t prev_i = 0; prev_i < new_idx; ++prev_i) {
        if (event_shipment_id(ground_truth_[prev_i]) == ship.shipment_id) {
            gt_causality_matrix_[prev_i][new_idx] = true;
            for (size_t k = 0; k < prev_i; ++k) {
                if (gt_causality_matrix_[k][prev_i]) {
                    gt_causality_matrix_[k][new_idx] = true;
                }
            }
        }
    }

    if (device.is_online()) {
        broadcast_event(se, current_custodian);
    }
}

void NetworkSimulator::process_custody_transfer(Shipment& ship) {
    if (ship.finished) return;
    if (ship.current_leg_idx + 1 >= ship.legs.size()) {
        ship.finished = true;
        return;
    }

    size_t s_idx = ship.shipment_id - 1;
    auto& device = shipment_loggers_[s_idx];
    auto& clock  = shipment_clocks_[s_idx];

    uint8_t from_entity = ship.legs[ship.current_leg_idx].custodian;
    uint8_t to_entity   = ship.legs[ship.current_leg_idx + 1].custodian;

    // Reconnection flush: flush any offline buffered readings
    if (!device.is_online()) {
        device.set_online(true);
        if (cfg_.resync_at_reconnect) {
            uint64_t reconnect_ns = static_cast<uint64_t>(sim_clock_ms_ * 1e6);
            clock.resync(reconnect_ns);
        }
    }

    auto buffered = device.flush_on_reconnect();
    double base_jitter = sample_tx_jitter();
    for (size_t bi = 0; bi < buffered.size(); ++bi) {
        auto& se = buffered[bi];
        // Re-stamp with upload offset if resyncing
        uint64_t upload_offset_ns = static_cast<uint64_t>(bi) * 1'000'000ULL;
        uint64_t reconnect_ns = static_cast<uint64_t>(sim_clock_ms_ * 1e6);
        if (cfg_.restamp_physical_on_upload) {
            se.physical_timestamp_ns = clock.now_ns(reconnect_ns, se.temperature_celsius) + upload_offset_ns;
        }

        for (size_t target = 0; target < cfg_.num_nodes; ++target) {
            ScheduledMessage msg;
            msg.fire_time_ms = sim_clock_ms_ + base_jitter + static_cast<double>(bi) * 0.1;
            msg.from_node    = from_entity;
            msg.to_node      = static_cast<uint8_t>(target);
            msg.payload      = se;
            msg.is_gossip    = false;
            message_queue_.push(msg);
        }
    }

    // 1. Stage 1: Sender Departure Custody Event (ce_depart)
    device.local_clock_mut().tick(from_entity);
    uint64_t from_l_ts = std::max(entity_lamport_clocks_[from_entity], device.current_lamport_ts()) + 1;
    entity_lamport_clocks_[from_entity] = from_l_ts;
    device.merge_lamport_ts(from_l_ts);

    CustodyEvent ce_depart;
    ce_depart.shipment_id          = ship.shipment_id;
    ce_depart.from_entity_id       = from_entity;
    ce_depart.to_entity_id         = to_entity;
    ce_depart.origin_entity_id     = from_entity;
    ce_depart.custody_transfer_seq = ++ship.custody_transfers_count;
    ce_depart.last_sensor_seq      = device.current_counter();
    ce_depart.vc                   = device.local_clock();
    ce_depart.lamport_ts           = from_l_ts;
    ce_depart.creation_time_ns     = static_cast<uint64_t>(sim_clock_ms_ * 1e6);
    ce_depart.physical_timestamp_ns = clock.now_ns(ce_depart.creation_time_ns, 4.0f);
    ce_depart.receipt_timestamp_ns = 0;
    ce_depart.is_pruned            = false;
    ce_depart.transfer_signature   = sign_event(ce_depart.serialize(), node_private_key(from_entity));
    ce_depart.id                   = ce_depart.compute_hash();

    size_t depart_idx = ground_truth_.size();
    ground_truth_.push_back(ce_depart);
    event_index_map_[ce_depart.id] = depart_idx;

    // Ground-truth happens-before: departure event happens after all prior events of this shipment
    gt_causality_matrix_.resize(depart_idx + 1);
    for (auto& row : gt_causality_matrix_) {
        row.resize(depart_idx + 1, false);
    }
    for (size_t prev_i = 0; prev_i < depart_idx; ++prev_i) {
        if (event_shipment_id(ground_truth_[prev_i]) == ship.shipment_id) {
            gt_causality_matrix_[prev_i][depart_idx] = true;
            for (size_t k = 0; k < prev_i; ++k) {
                if (gt_causality_matrix_[k][prev_i]) {
                    gt_causality_matrix_[k][depart_idx] = true;
                }
            }
        }
    }

    double handoff_delay_ms = sample_handoff_delay();
    // Broadcast departure event after flushed readings
    for (size_t target = 0; target < cfg_.num_nodes; ++target) {
        ScheduledMessage msg;
        msg.fire_time_ms = sim_clock_ms_ + base_jitter + handoff_delay_ms + static_cast<double>(buffered.size() + 1) * 0.1;
        msg.from_node    = from_entity;
        msg.to_node      = static_cast<uint8_t>(target);
        msg.payload      = ce_depart;
        msg.is_gossip    = false;
        message_queue_.push(msg);
    }

    // 2. Stage 2: Receiver Arrival / Intake Custody Event (ce_recv)
    VectorClock recv_vc = ce_depart.vc;
    recv_vc.tick(to_entity);
    device.set_local_clock(recv_vc);

    // Receiver signs intake event with its local custodian Lamport clock
    uint64_t recv_l_ts = std::max(entity_lamport_clocks_[to_entity], ce_depart.lamport_ts) + 1;
    entity_lamport_clocks_[to_entity] = recv_l_ts;
    device.merge_lamport_ts(recv_l_ts);

    CustodyEvent ce_recv;
    ce_recv.shipment_id          = ship.shipment_id;
    ce_recv.from_entity_id       = from_entity;
    ce_recv.to_entity_id         = to_entity;
    ce_recv.origin_entity_id     = to_entity;
    ce_recv.custody_transfer_seq = ++ship.custody_transfers_count;
    ce_recv.last_sensor_seq      = device.current_counter();
    ce_recv.vc                   = recv_vc;
    ce_recv.lamport_ts           = recv_l_ts;
    ce_recv.creation_time_ns     = static_cast<uint64_t>(sim_clock_ms_ * 1e6) + 1000ULL;
    ce_recv.physical_timestamp_ns = clock.now_ns(ce_recv.creation_time_ns, 4.0f);
    ce_recv.receipt_timestamp_ns = 0;
    ce_recv.is_pruned            = false;
    ce_recv.transfer_signature   = sign_event(ce_recv.serialize(), node_private_key(to_entity));
    ce_recv.id                   = ce_recv.compute_hash();

    size_t recv_idx = ground_truth_.size();
    ground_truth_.push_back(ce_recv);
    event_index_map_[ce_recv.id] = recv_idx;

    gt_causality_matrix_.resize(recv_idx + 1);
    for (auto& row : gt_causality_matrix_) {
        row.resize(recv_idx + 1, false);
    }

    // (a) ce_depart strictly precedes ce_recv
    gt_causality_matrix_[depart_idx][recv_idx] = true;

    // (b) All prior events of this shipment strictly precede ce_recv
    for (size_t prev_i = 0; prev_i < recv_idx; ++prev_i) {
        if (event_shipment_id(ground_truth_[prev_i]) == ship.shipment_id) {
            gt_causality_matrix_[prev_i][recv_idx] = true;
            for (size_t k = 0; k < prev_i; ++k) {
                if (gt_causality_matrix_[k][prev_i]) {
                    gt_causality_matrix_[k][recv_idx] = true;
                }
            }
        }
    }

    // (c) Prior events of this shipment strictly precede ce_recv
    for (size_t prev_i = 0; prev_i < recv_idx; ++prev_i) {
        if (event_shipment_id(ground_truth_[prev_i]) == ship.shipment_id) {
            gt_causality_matrix_[prev_i][recv_idx] = true;
            for (size_t k = 0; k < prev_i; ++k) {
                if (gt_causality_matrix_[k][prev_i]) {
                    gt_causality_matrix_[k][recv_idx] = true;
                }
            }
        }
    }

    // Broadcast receive event after departure event
    for (size_t target = 0; target < cfg_.num_nodes; ++target) {
        ScheduledMessage msg;
        msg.fire_time_ms = sim_clock_ms_ + base_jitter + handoff_delay_ms + static_cast<double>(buffered.size() + 2) * 0.1;
        msg.from_node    = to_entity;
        msg.to_node      = static_cast<uint8_t>(target);
        msg.payload      = ce_recv;
        msg.is_gossip    = false;
        message_queue_.push(msg);
    }

    // Advance to next leg
    ship.current_leg_idx++;
    device.set_current_custody_seq(ship.custody_transfers_count);
    device.set_current_custodian(to_entity);
}

void NetworkSimulator::deliver_pending_messages() {
    while (!message_queue_.empty() && message_queue_.top().fire_time_ms <= sim_clock_ms_) {
        auto msg = message_queue_.top();
        message_queue_.pop();

        // Check if link is partitioned at delivery time
        if (msg.from_node != msg.to_node && is_partitioned(msg.from_node, msg.to_node, sim_clock_ms_)) {
            // Reschedule delivery after partition heals
            msg.fire_time_ms = sim_clock_ms_ + 5000.0;
            message_queue_.push(msg);
            continue;
        }

        // Set gateway receipt timestamp
        uint64_t receipt_ns = static_cast<uint64_t>(sim_clock_ms_ * 1e6);
        set_event_receipt_time(msg.payload, receipt_ns);

        // Ingest into target node's replicated ledger
        vclock_nodes_[msg.to_node].ingest_event(
            msg.payload,
            cfg_.verify_signatures_enabled,
            cfg_.hold_back_enabled
        );
    }
}

void NetworkSimulator::step(double delta_time_ms) {
    double target = sim_clock_ms_ + delta_time_ms;
    double step_resolution = cfg_.sampling_interval_s * 1000.0;

    while (sim_clock_ms_ < target) {
        sim_clock_ms_ = std::min(sim_clock_ms_ + step_resolution, target);

        for (auto& ship : shipments_) {
            if (ship.finished || sim_clock_ms_ < ship.start_time_ms) continue;

            if (ship.current_leg_idx < ship.legs.size()) {
                const auto& leg = ship.legs[ship.current_leg_idx];
                if (sim_clock_ms_ >= leg.end_ms) {
                    process_custody_transfer(ship);
                    continue;
                }
            }

            if (sim_clock_ms_ >= ship.next_sample_ms) {
                process_telemetry(ship);
                ship.next_sample_ms = sim_clock_ms_ + cfg_.sampling_interval_s * 1000.0;
            }
        }

        deliver_pending_messages();

        // Periodic mid-run gossip and pruning under matrix clock knowledge
        if (cfg_.gossip_interval_s > 0.0 && sim_clock_ms_ - last_gossip_time_ms_ >= cfg_.gossip_interval_s * 1000.0) {
            last_gossip_time_ms_ = sim_clock_ms_;
            gossip_rounds(1);

            for (auto& node : vclock_nodes_) {
                PruningEngine::prune_stable_records(
                    node,
                    cfg_.temp_low_c,
                    cfg_.temp_high_c,
                    &vclock_nodes_,
                    &safety_violations_
                );
            }
        }
    }
}

void NetworkSimulator::run_until(double end_time_ms) {
    if (sim_clock_ms_ >= end_time_ms) return;
    step(end_time_ms - sim_clock_ms_);
}

void NetworkSimulator::run_full() {
    setup_shipments();
    generate_failure_events();

    double total_ms = cfg_.duration_hours * 3600.0 * 1000.0;
    run_until(total_ms);

    // Final flush on sim termination
    for (size_t s_idx = 0; s_idx < shipments_.size(); ++s_idx) {
        auto& device = shipment_loggers_[s_idx];
        auto& clock  = shipment_clocks_[s_idx];
        uint8_t cust = device.current_custodian();
        device.set_online(true);
        if (cfg_.resync_at_reconnect) {
            clock.resync(static_cast<uint64_t>(sim_clock_ms_ * 1e6));
        }
        auto buffered = device.flush_on_reconnect();
        double base_jitter = sample_tx_jitter();
        for (size_t bi = 0; bi < buffered.size(); ++bi) {
            auto& se = buffered[bi];
            for (size_t target = 0; target < cfg_.num_nodes; ++target) {
                ScheduledMessage msg;
                msg.fire_time_ms = sim_clock_ms_ + base_jitter + static_cast<double>(bi) * 0.1;
                msg.from_node    = cust;
                msg.to_node      = static_cast<uint8_t>(target);
                msg.payload      = se;
                msg.is_gossip    = false;
                message_queue_.push(msg);
            }
        }
    }

    // Heal all network partitions and drain all remaining in-flight messages
    partitions_.clear();
    while (!message_queue_.empty()) {
        sim_clock_ms_ = std::max(sim_clock_ms_, message_queue_.top().fire_time_ms);
        deliver_pending_messages();
    }

    // Post-condition (explicit check; NOT compiled out in Release builds)
    for (size_t i = 0; i < vclock_nodes_.size(); ++i) {
        if (vclock_nodes_[i].hold_back_count() != 0) {
            throw std::runtime_error("NetworkSimulator: hold-back queue not drained on node " + std::to_string(i));
        }
        if (vclock_nodes_[i].delivered_count() != ground_truth_.size()) {
            throw std::runtime_error("NetworkSimulator: node " + std::to_string(i) + " delivered " +
                std::to_string(vclock_nodes_[i].delivered_count()) + " of " + std::to_string(ground_truth_.size()) + " events");
        }
    }

    // Auto-seal final blocks
    for (auto& node : vclock_nodes_) {
        if (node.pending_count() > 0) {
            node.seal_block();
        }
    }
}

void NetworkSimulator::gossip_rounds(size_t rounds) {
    size_t n = vclock_nodes_.size();

    for (size_t r = 0; r < rounds; ++r) {
        // Synchronous gossip: snapshot current matrix clocks first
        std::vector<MatrixClock> snapshots;
        snapshots.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            snapshots.push_back(vclock_nodes_[i].matrix_clock());
        }

        for (size_t i = 0; i < n; ++i) {
            for (size_t j = 0; j < n; ++j) {
                if (i == j) continue;
                if (is_partitioned(static_cast<uint8_t>(i), static_cast<uint8_t>(j), sim_clock_ms_)) {
                    continue; // Gossip link blocked by partition
                }

                // Node j receives node i's snapshot
                vclock_nodes_[j].matrix_clock().merge_matrix(j, i, snapshots[i]);
                ++gossip_messages_sent_;
                gossip_bytes_sent_ += sizeof(uint32_t) * n * n;
            }
        }
    }
}

bool NetworkSimulator::is_ground_truth_preceding(size_t event_idx_a, size_t event_idx_b) const {
    if (event_idx_a >= gt_causality_matrix_.size() ||
        event_idx_b >= gt_causality_matrix_.size()) {
        return false;
    }
    return gt_causality_matrix_[event_idx_a][event_idx_b];
}

LedgerNode& NetworkSimulator::vclock_node(size_t idx)             { return vclock_nodes_[idx]; }
const LedgerNode& NetworkSimulator::vclock_node(size_t idx) const { return vclock_nodes_[idx]; }
const std::vector<LedgerNode>& NetworkSimulator::all_vclock_nodes() const { return vclock_nodes_; }
size_t NetworkSimulator::num_nodes() const                        { return cfg_.num_nodes; }
double NetworkSimulator::sim_clock_ms() const                     { return sim_clock_ms_; }

const std::vector<LedgerEvent>& NetworkSimulator::ground_truth_events() const {
    return ground_truth_;
}
const std::vector<CustodyInterval>& NetworkSimulator::true_custody_intervals() const {
    return custody_intervals_;
}

size_t NetworkSimulator::total_events_generated() const    { return ground_truth_.size(); }
size_t NetworkSimulator::total_blackouts_injected() const  { return total_blackouts_; }
size_t NetworkSimulator::total_partitions_injected() const { return total_partitions_; }
size_t NetworkSimulator::gossip_messages_sent() const     { return gossip_messages_sent_; }
size_t NetworkSimulator::gossip_bytes_sent() const        { return gossip_bytes_sent_; }
size_t NetworkSimulator::safety_violations_detected() const { return safety_violations_; }

} // namespace coldchain
