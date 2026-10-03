#include "order_book.hpp"
#include "itch_parser.hpp"

#include <algorithm>
#include <iterator>
#include <limits>

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
                if (resting.side == Side::Buy) {
                    bid_quantity_ -= quantity;
                } else {
                    ask_quantity_ -= quantity;
                }
                if (resting.quantity == 0) {
                    orders_.erase(resting.id);
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
        add(incoming);
    }
    return trades;
}

bool OrderBook::add(Order order) {
    if (order.id == 0 || order.quantity == 0 || orders_.contains(order.id)) return false;
    Level::iterator iterator;
    if (order.side == Side::Buy) {
        auto& level = bids_[order.price_ticks];
        level.push_back(order);
        iterator = std::prev(level.end());
        bid_quantity_ += order.quantity;
    } else {
        auto& level = asks_[order.price_ticks];
        level.push_back(order);
        iterator = std::prev(level.end());
        ask_quantity_ += order.quantity;
    }
    orders_.emplace(order.id, Location{order.side, order.price_ticks, iterator});
    return true;
}

void OrderBook::erase(Location location) {
    const auto id = location.iterator->id;
    const auto quantity = location.iterator->quantity;
    if (location.side == Side::Buy) {
        bid_quantity_ -= quantity;
        auto level = bids_.find(location.price_ticks);
        level->second.erase(location.iterator);
        if (level->second.empty()) bids_.erase(level);
    } else {
        ask_quantity_ -= quantity;
        auto level = asks_.find(location.price_ticks);
        level->second.erase(location.iterator);
        if (level->second.empty()) asks_.erase(level);
    }
    orders_.erase(id);
}

bool OrderBook::reduce(std::uint64_t order_reference, std::uint64_t quantity) {
    const auto found = orders_.find(order_reference);
    if (found == orders_.end() || quantity == 0) return false;
    auto& order = *found->second.iterator;
    const auto reduction = std::min(order.quantity, quantity);
    order.quantity -= reduction;
    if (order.side == Side::Buy) bid_quantity_ -= reduction;
    else ask_quantity_ -= reduction;
    if (order.quantity == 0) erase(found->second);
    return true;
}

bool OrderBook::cancel(std::uint64_t order_reference, std::uint64_t quantity) {
    return reduce(order_reference, quantity);
}

bool OrderBook::execute(std::uint64_t order_reference, std::uint64_t quantity) {
    return reduce(order_reference, quantity);
}

bool OrderBook::replace(std::uint64_t old_reference, Order replacement) {
    const auto found = orders_.find(old_reference);
    if (found == orders_.end() || replacement.id == 0 || replacement.quantity == 0 ||
        orders_.contains(replacement.id)) return false;
    replacement.side = found->second.side;
    erase(found->second);
    return add(replacement);
}

bool OrderBook::apply(const ItchEvent& event) {
    switch (event.type) {
    case ItchType::AddOrder:
    case ItchType::AddOrderWithAttribution:
        return add({event.order_reference, event.side, event.price_ticks,
                    event.shares, event.timestamp_nanoseconds});
    case ItchType::OrderCancel:
        return cancel(event.order_reference, event.shares);
    case ItchType::OrderExecuted:
    case ItchType::OrderExecutedWithPrice:
        return execute(event.order_reference, event.shares);
    case ItchType::OrderDelete:
        return cancel(event.order_reference, std::numeric_limits<std::uint64_t>::max());
    case ItchType::OrderReplace:
        return replace(event.order_reference,
                       {event.new_order_reference, Side::Buy, event.price_ticks,
                        event.shares, event.timestamp_nanoseconds});
    default:
        return false;
    }
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
    return side == Side::Buy ? bid_quantity_ : ask_quantity_;
}

BookLevel OrderBook::summarize(std::int64_t price_ticks, const Level& level) {
    BookLevel result{price_ticks, 0, level.size()};
    for (const auto& order : level) result.quantity += order.quantity;
    return result;
}

std::vector<BookLevel> OrderBook::bids(std::size_t depth) const {
    std::vector<BookLevel> result;
    for (const auto& [price, level] : bids_) {
        if (result.size() == depth) break;
        result.push_back(summarize(price, level));
    }
    return result;
}

std::vector<BookLevel> OrderBook::asks(std::size_t depth) const {
    std::vector<BookLevel> result;
    for (const auto& [price, level] : asks_) {
        if (result.size() == depth) break;
        result.push_back(summarize(price, level));
    }
    return result;
}

std::size_t OrderBook::order_count() const noexcept {
    return orders_.size();
}

std::uint64_t OrderBook::quantity(Side side) const noexcept {
    return side == Side::Buy ? bid_quantity_ : ask_quantity_;
}

void OrderBook::clear() noexcept {
    bids_.clear();
    asks_.clear();
    orders_.clear();
    bid_quantity_ = 0;
    ask_quantity_ = 0;
}

}  // namespace trading
