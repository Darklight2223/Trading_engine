#include "matching_engine.hpp"

#include <cassert>

int main() {
    trading::MatchingEngine engine;
    assert(engine.submit(trading::Side::Sell, 101, 5).empty());
    const auto trades = engine.submit(trading::Side::Buy, 102, 3);
    assert(trades.size() == 1);
    assert(trades.front().price_ticks == 101);
    assert(trades.front().quantity == 3);
    assert(engine.book().resting_quantity(trading::Side::Sell) == 2);

    const auto no_trade = engine.submit(trading::Side::Buy, 99, 4);
    assert(no_trade.empty());
    assert(engine.book().resting_quantity(trading::Side::Buy) == 4);
    return 0;
}
