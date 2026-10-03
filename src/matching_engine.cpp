#include "matching_engine.hpp"

namespace trading {

std::vector<Trade> MatchingEngine::submit(Side side, std::int64_t price_ticks,
                                          std::uint64_t quantity) {
    return book_.add_order({next_order_id_++, side, price_ticks, quantity,
                            next_sequence_++});
}

bool MatchingEngine::cancel(std::uint64_t order_reference, std::uint64_t quantity) {
    return book_.cancel(order_reference, quantity);
}

bool MatchingEngine::replace(std::uint64_t old_reference, std::int64_t price_ticks,
                             std::uint64_t quantity) {
    return book_.replace(old_reference, {next_order_id_++, Side::Buy, price_ticks,
                                         quantity, next_sequence_++});
}

const OrderBook& MatchingEngine::book() const noexcept {
    return book_;
}

}  // namespace trading
