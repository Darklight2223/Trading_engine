#include "matching_engine.hpp"

namespace trading {

std::vector<Trade> MatchingEngine::submit(Side side, std::int64_t price_ticks,
                                          std::uint64_t quantity) {
    return book_.add_order({next_order_id_++, side, price_ticks, quantity,
                            next_sequence_++});
}

const OrderBook& MatchingEngine::book() const noexcept {
    return book_;
}

}  // namespace trading
