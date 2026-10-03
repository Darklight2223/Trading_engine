#include "matching_engine.hpp"
#include "order_book.hpp"

#include <cstdint>

int main() {
    trading::MatchingEngine engine;
    if (!engine.submit(trading::Side::Sell, 101, 5).empty()) return 1;
    const auto trades = engine.submit(trading::Side::Buy, 102, 3);
    if (trades.size() != 1 || trades.front().price_ticks != 101 || trades.front().quantity != 3) return 1;
    if (engine.book().resting_quantity(trading::Side::Sell) != 2) return 1;

    const auto no_trade = engine.submit(trading::Side::Buy, 99, 4);
    if (!no_trade.empty() || engine.book().resting_quantity(trading::Side::Buy) != 4) return 1;

    trading::OrderBook order_book;
    if (!order_book.add({1001, trading::Side::Buy, 10000, 50, 1})) return 1;
    if (!order_book.add({1002, trading::Side::Sell, 10100, 40, 2})) return 1;
    if (!order_book.cancel(1001, 20) || order_book.quantity(trading::Side::Buy) != 30) return 1;
    if (!order_book.execute(1002, 10) || order_book.quantity(trading::Side::Sell) != 30) return 1;
    if (!order_book.replace(1001, {1003, trading::Side::Sell, 10200, 12, 3})) return 1;
    const auto best_bid = order_book.bids(1);
    const auto best_ask = order_book.asks(1);
    if (best_bid.size() != 1 || best_bid.front().price_ticks != 10200 ||
        best_ask.size() != 1 || best_ask.front().price_ticks != 10100) return 1;
    if (!order_book.cancel(1002, 100) || !order_book.cancel(1003, 100)) return 1;
    if (order_book.order_count() != 0) return 1;
    return 0;
}
