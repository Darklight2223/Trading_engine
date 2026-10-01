#include "matching_engine.hpp"
#include "simulator.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>

int main() {
    trading::MatchingEngine engine;
    constexpr std::uint64_t simulator_seed = 42;
    trading::Simulator simulator(simulator_seed);
    constexpr std::uint64_t order_count = 100000;

    const auto start = std::chrono::steady_clock::now();
    const auto result = simulator.run(engine, order_count);
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start).count();
    const double throughput = static_cast<double>(result.submitted_orders) * 1'000'000'000.0 /
                              static_cast<double>(elapsed);

    std::cout << "orders=" << result.submitted_orders << '\n'
              << "simulator_seed=" << simulator_seed << '\n'
              << "trades=" << result.executed_trades << '\n'
              << "executed_quantity=" << result.executed_quantity << '\n'
              << "elapsed_ns=" << elapsed << '\n'
              << "avg_order_latency_ns=" << (elapsed / order_count) << '\n'
              << "throughput_orders_per_second=" << throughput << '\n'
              << "resting_bids=" << engine.book().bid_count() << '\n'
              << "resting_asks=" << engine.book().ask_count() << '\n';
}
