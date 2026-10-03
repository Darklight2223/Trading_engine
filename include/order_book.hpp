#pragma once

#include "order.hpp"
#include "trade.hpp"

#include <cstdint>
#include <list>
#include <map>
#include <unordered_map>
#include <vector>

namespace trading {

struct ItchEvent;

struct BookLevel {
    std::int64_t price_ticks{};
    std::uint64_t quantity{};
    std::size_t order_count{};
};

class OrderBook {
public:
    std::vector<Trade> add_order(Order order);
    bool add(Order order);
    bool cancel(std::uint64_t order_reference, std::uint64_t quantity);
    bool execute(std::uint64_t order_reference, std::uint64_t quantity);
    bool replace(std::uint64_t old_reference, Order replacement);
    bool apply(const ItchEvent& event);

    [[nodiscard]] std::size_t bid_count() const noexcept;
    [[nodiscard]] std::size_t ask_count() const noexcept;
    [[nodiscard]] std::uint64_t resting_quantity(Side side) const noexcept;
    [[nodiscard]] std::vector<BookLevel> bids(std::size_t depth) const;
    [[nodiscard]] std::vector<BookLevel> asks(std::size_t depth) const;
    [[nodiscard]] std::size_t order_count() const noexcept;
    [[nodiscard]] std::uint64_t quantity(Side side) const noexcept;
    void clear() noexcept;

private:
    using Level = std::list<Order>;
    struct Location {
        Side side;
        std::int64_t price_ticks;
        Level::iterator iterator;
    };

    bool reduce(std::uint64_t order_reference, std::uint64_t quantity);
    void erase(Location location);
    static BookLevel summarize(std::int64_t price_ticks, const Level& level);

    std::map<std::int64_t, Level, std::greater<>> bids_;
    std::map<std::int64_t, Level> asks_;
    std::unordered_map<std::uint64_t, Location> orders_;
    std::uint64_t bid_quantity_{};
    std::uint64_t ask_quantity_{};
};

}  // namespace trading
