#pragma once

#include "order.hpp"
#include "trade.hpp"

#include <cstdint>
#include <deque>
#include <map>
#include <vector>

namespace trading {

class OrderBook {
public:
    std::vector<Trade> add_order(Order order);
    [[nodiscard]] std::size_t bid_count() const noexcept;
    [[nodiscard]] std::size_t ask_count() const noexcept;
    [[nodiscard]] std::uint64_t resting_quantity(Side side) const noexcept;

private:
    using Level = std::deque<Order>;
    std::map<std::int64_t, Level, std::greater<>> bids_;
    std::map<std::int64_t, Level> asks_;

    void remove_empty_levels();
};

}  // namespace trading
