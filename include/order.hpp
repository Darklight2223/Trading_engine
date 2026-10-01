#pragma once

#include <cstdint>

namespace trading {

enum class Side { Buy, Sell };

enum class OrderType { Limit };

struct Order {
    std::uint64_t id{};
    Side side{Side::Buy};
    std::int64_t price_ticks{};
    std::uint64_t quantity{};
    std::uint64_t sequence{};
};

}  // namespace trading
