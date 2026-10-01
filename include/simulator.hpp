#pragma once

#include "matching_engine.hpp"

#include <cstdint>

namespace trading {

struct SimulationResult {
    std::uint64_t submitted_orders{};
    std::uint64_t executed_trades{};
    std::uint64_t executed_quantity{};
};

class Simulator {
public:
    explicit Simulator(std::uint64_t seed = 42) noexcept;
    SimulationResult run(MatchingEngine& engine, std::uint64_t order_count) const;

private:
    std::uint64_t seed_;
};

}  // namespace trading
