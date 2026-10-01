#include "simulator.hpp"

namespace trading {

Simulator::Simulator(std::uint64_t seed) noexcept : seed_(seed) {}

SimulationResult Simulator::run(MatchingEngine& engine,
                                std::uint64_t order_count) const {
    SimulationResult result{0, 0, 0};
    std::uint64_t state = seed_;
    for (std::uint64_t i = 0; i < order_count; ++i) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        const auto side = (state & 1U) == 0 ? Side::Buy : Side::Sell;
        const auto price = 10000 + static_cast<std::int64_t>((state >> 8) % 21) - 10;
        const auto quantity = 1 + ((state >> 16) % 10);
        const auto trades = engine.submit(side, price, quantity);
        result.submitted_orders++;
        result.executed_trades += trades.size();
        for (const auto& trade : trades) result.executed_quantity += trade.quantity;
    }
    return result;
}

}  // namespace trading
