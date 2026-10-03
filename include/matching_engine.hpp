#pragma once

#include "order_book.hpp"

#include <cstdint>
#include <vector>

namespace trading {

class MatchingEngine {
public:
    std::vector<Trade> submit(Side side, std::int64_t price_ticks,
                              std::uint64_t quantity);
    bool cancel(std::uint64_t order_reference, std::uint64_t quantity);
    bool replace(std::uint64_t old_reference, std::int64_t price_ticks,
                 std::uint64_t quantity);
    [[nodiscard]] const OrderBook& book() const noexcept;

private:
    OrderBook book_;
    std::uint64_t next_order_id_{1};
    std::uint64_t next_sequence_{1};
};

}  // namespace trading
