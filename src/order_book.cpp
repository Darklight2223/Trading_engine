#include "order_book.hpp"

#include <algorithm>

namespace trading {

std::vector<Trade> OrderBook::add_order(Order incoming) {
    std::vector<Trade> trades;
    const auto match = [&](auto& opposing) {
        while (incoming.quantity > 0 && !opposing.empty()) {
            auto level_it = opposing.begin();
            const bool crosses = incoming.side == Side::Buy
                ? incoming.price_ticks >= level_it->first
                : incoming.price_ticks <= level_it->first;
            if (!crosses) {
                break;
            }

            auto& level = level_it->second;
            while (incoming.quantity > 0 && !level.empty()) {
                auto& resting = level.front();
                const auto quantity = std::min(incoming.quantity, resting.quantity);
                trades.push_back(Trade{incoming.id, resting.id, resting.price_ticks, quantity});
                incoming.quantity -= quantity;
                resting.quantity -= quantity;
                if (resting.quantity == 0) {
                    level.pop_front();
                }
            }
            if (level.empty()) {
                opposing.erase(level_it);
            }
        }
    };

    if (incoming.side == Side::Buy) {
        match(asks_);
    } else {
        match(bids_);
    }

    if (incoming.quantity > 0) {
        if (incoming.side == Side::Buy) {
            bids_[incoming.price_ticks].push_back(incoming);
        } else {
            asks_[incoming.price_ticks].push_back(incoming);
        }
    }
    return trades;
}

std::size_t OrderBook::bid_count() const noexcept {
    std::size_t count = 0;
    for (const auto& [price, level] : bids_) count += level.size();
    return count;
}

std::size_t OrderBook::ask_count() const noexcept {
    std::size_t count = 0;
    for (const auto& [price, level] : asks_) count += level.size();
    return count;
}

std::uint64_t OrderBook::resting_quantity(Side side) const noexcept {
    std::uint64_t quantity = 0;
    const auto add_quantity = [&quantity](const auto& levels) {
        for (const auto& [price, level] : levels) {
            for (const auto& order : level) quantity += order.quantity;
        }
    };
    if (side == Side::Buy) {
        add_quantity(bids_);
    } else {
        add_quantity(asks_);
    }
    return quantity;
}

}  // namespace trading
