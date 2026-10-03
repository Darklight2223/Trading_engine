#include "itch_parser.hpp"
#include "order_book.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_map>
#include <limits>
#include <vector>

namespace {

struct Counters {
    std::uint64_t system{};
    std::uint64_t stock_directory{};
    std::uint64_t adds{};
    std::uint64_t cancels{};
    std::uint64_t executes{};
    std::uint64_t deletes{};
    std::uint64_t replaces{};
    std::uint64_t trades{};
    std::uint64_t market_control{};
    std::uint64_t cross_trades{};
    std::uint64_t broken_trades{};
    std::uint64_t imbalances{};
    std::uint64_t retail_price_improvements{};
    std::uint64_t unknown{};
    std::uint64_t applied{};
    std::uint64_t rejected{};
};

struct LatencyStats {
    std::vector<std::uint64_t> samples;
    std::uint64_t total_nanoseconds{};
    std::uint64_t maximum_nanoseconds{};

    void add(std::uint64_t value) {
        samples.push_back(value);
        total_nanoseconds += value;
        maximum_nanoseconds = std::max(maximum_nanoseconds, value);
    }

    void sort_samples() {
        std::sort(samples.begin(), samples.end());
    }

    [[nodiscard]] std::uint64_t quantile(double fraction) const {
        if (samples.empty()) return 0;
        const auto rank = static_cast<std::size_t>(
            std::ceil(fraction * static_cast<double>(samples.size())));
        const auto index = rank == 0 ? 0 : std::min(rank - 1, samples.size() - 1);
        return samples[index];
    }
};

void count_event(const trading::ItchEvent& event, Counters& counters) {
    switch (event.type) {
    case trading::ItchType::System: ++counters.system; break;
    case trading::ItchType::StockDirectory: ++counters.stock_directory; break;
    case trading::ItchType::AddOrder:
    case trading::ItchType::AddOrderWithAttribution: ++counters.adds; break;
    case trading::ItchType::OrderCancel: ++counters.cancels; break;
    case trading::ItchType::OrderExecuted:
    case trading::ItchType::OrderExecutedWithPrice: ++counters.executes; break;
    case trading::ItchType::OrderDelete: ++counters.deletes; break;
    case trading::ItchType::OrderReplace: ++counters.replaces; break;
    case trading::ItchType::Trade: ++counters.trades; break;
    case trading::ItchType::MarketControl: ++counters.market_control; break;
    case trading::ItchType::CrossTrade: ++counters.cross_trades; break;
    case trading::ItchType::BrokenTrade: ++counters.broken_trades; break;
    case trading::ItchType::Imbalance: ++counters.imbalances; break;
    case trading::ItchType::RetailPriceImprovement: ++counters.retail_price_improvements; break;
    case trading::ItchType::Unknown: ++counters.unknown; break;
    }
}

void print_counter(const Counters& counters) {
    std::cout << "system_messages=" << counters.system << '\n'
              << "stock_directory_messages=" << counters.stock_directory << '\n'
              << "add_messages=" << counters.adds << '\n'
              << "cancel_messages=" << counters.cancels << '\n'
              << "execute_messages=" << counters.executes << '\n'
              << "delete_messages=" << counters.deletes << '\n'
              << "replace_messages=" << counters.replaces << '\n'
              << "trade_messages=" << counters.trades << '\n'
              << "market_control_messages=" << counters.market_control << '\n'
              << "cross_trade_messages=" << counters.cross_trades << '\n'
              << "broken_trade_messages=" << counters.broken_trades << '\n'
              << "imbalance_messages=" << counters.imbalances << '\n'
              << "retail_price_improvement_messages=" << counters.retail_price_improvements << '\n'
              << "unknown_messages=" << counters.unknown << '\n'
              << "applied_updates=" << counters.applied << '\n'
              << "rejected_updates=" << counters.rejected << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    const std::string path = argc > 1 ? argv[1] : "data/07302019.NASDAQ_ITCH50.gz";
    std::string selected_symbol;
    bool measure_latency = false;
    std::uint64_t max_messages = std::numeric_limits<std::uint64_t>::max();
    for (int index = 2; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--latency") {
            measure_latency = true;
        } else if (argument == "--max-messages" && index + 1 < argc) {
            max_messages = std::stoull(argv[++index]);
        } else if (selected_symbol.empty()) {
            selected_symbol = argument;
        }
    }

    try {
        trading::ItchParser parser(path);
        trading::OrderBook book;
        std::unordered_map<std::uint16_t, std::string> symbols;
        Counters counters;
        LatencyStats latency;
        trading::ItchEvent event;
        const auto start = std::chrono::steady_clock::now();
        while (true) {
            if (parser.messages_read() >= max_messages) break;
            const auto message_start = measure_latency ? std::chrono::steady_clock::now()
                                                       : std::chrono::steady_clock::time_point{};
            if (!parser.next(event)) break;
            count_event(event, counters);
            if (parser.messages_read() % 1'000'000 == 0) {
                std::cerr << "replay_messages=" << parser.messages_read() << '\n';
            }
            if (event.type == trading::ItchType::StockDirectory) {
                symbols[event.stock_locate] = event.stock;
                if (measure_latency) {
                    latency.add(static_cast<std::uint64_t>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now() - message_start).count()));
                }
                continue;
            }
            if (!selected_symbol.empty()) {
                const auto found = symbols.find(event.stock_locate);
                if (found == symbols.end() || found->second != selected_symbol) {
                    if (measure_latency) {
                        latency.add(static_cast<std::uint64_t>(
                            std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now() - message_start).count()));
                    }
                    continue;
                }
            }
            const bool actionable = event.type == trading::ItchType::AddOrder ||
                                    event.type == trading::ItchType::AddOrderWithAttribution ||
                                    event.type == trading::ItchType::OrderCancel ||
                                    event.type == trading::ItchType::OrderExecuted ||
                                    event.type == trading::ItchType::OrderExecutedWithPrice ||
                                    event.type == trading::ItchType::OrderDelete ||
                                    event.type == trading::ItchType::OrderReplace;
            if (book.apply(event)) {
                ++counters.applied;
            } else if (actionable) {
                ++counters.rejected;
            }
            if (measure_latency) {
                latency.add(static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() - message_start).count()));
            }
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start).count();
        const auto messages = parser.messages_read();
        const double seconds = static_cast<double>(elapsed) / 1'000'000'000.0;

        std::cout << "dataset_path=" << path << '\n'
                  << "symbol=" << (selected_symbol.empty() ? "ALL" : selected_symbol) << '\n'
                  << "messages=" << messages << '\n'
                  << "replay_limited=" << (max_messages != std::numeric_limits<std::uint64_t>::max() ? 1 : 0) << '\n'
                  << "framed_bytes=not_tracked\n"
                  << "elapsed_ns=" << elapsed << '\n'
                  << "messages_per_second=" << (seconds > 0.0 ? messages / seconds : 0.0) << '\n'
                  << "dataset_truncated=" << (parser.truncated() ? 1 : 0) << '\n';
        if (measure_latency) {
            latency.sort_samples();
            std::cout << "latency_percentile_method=exact_nearest_rank\n"
                      << "latency_messages=" << latency.samples.size() << '\n'
                      << "latency_avg_ns=" << (latency.samples.empty() ? 0 : latency.total_nanoseconds / latency.samples.size()) << '\n'
                      << "latency_p50_ns=" << latency.quantile(0.50) << '\n'
                      << "latency_p95_ns=" << latency.quantile(0.95) << '\n'
                      << "latency_p99_ns=" << latency.quantile(0.99) << '\n'
                      << "latency_p999_ns=" << latency.quantile(0.999) << '\n'
                      << "latency_max_ns=" << latency.maximum_nanoseconds << '\n';
        }
        print_counter(counters);
        std::cout << "book_orders=" << book.order_count() << '\n'
                  << "bid_quantity=" << book.quantity(trading::Side::Buy) << '\n'
                  << "ask_quantity=" << book.quantity(trading::Side::Sell) << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "itch_benchmark_error=" << error.what() << '\n';
        return 1;
    }
}