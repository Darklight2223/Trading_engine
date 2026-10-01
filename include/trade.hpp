#pragma once

#include <cstdint>

namespace trading {

struct Trade {
    std::uint64_t taker_order_id{};
    std::uint64_t maker_order_id{};
    std::int64_t price_ticks{};
    std::uint64_t quantity{};
};

}  // namespace trading
