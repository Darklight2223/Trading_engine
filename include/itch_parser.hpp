#pragma once

#include "order.hpp"

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace trading {

enum class ItchType {
    System,
    StockDirectory,
    AddOrder,
    AddOrderWithAttribution,
    OrderExecuted,
    OrderExecutedWithPrice,
    OrderCancel,
    OrderDelete,
    OrderReplace,
    Trade,
    MarketControl,
    CrossTrade,
    BrokenTrade,
    Imbalance,
    RetailPriceImprovement,
    Unknown,
};

struct ItchEvent {
    ItchType type{ItchType::Unknown};
    char raw_type{'?'};
    std::uint16_t stock_locate{};
    std::uint16_t tracking_number{};
    std::uint64_t timestamp_nanoseconds{};
    std::uint64_t order_reference{};
    std::uint64_t new_order_reference{};
    std::uint64_t shares{};
    std::int64_t price_ticks{};
    Side side{Side::Buy};
    char event_code{};
    bool printable{};
    std::string stock;
};

class ItchParser {
public:
    explicit ItchParser(std::string path, bool strict = false);
    ~ItchParser();

    ItchParser(const ItchParser&) = delete;
    ItchParser& operator=(const ItchParser&) = delete;

    bool next(ItchEvent& event);
    [[nodiscard]] std::uint64_t messages_read() const noexcept;
    [[nodiscard]] bool truncated() const noexcept;

private:
    bool read_bytes(void* destination, std::size_t size);
    bool read_message(std::vector<std::uint8_t>& message);
    void close_gzip() noexcept;

    std::string path_;
    bool strict_{};
    bool truncated_{};
    std::uint64_t messages_read_{};
    std::ifstream raw_file_;
    void* gzip_api_{};
    void* gzip_file_{};
    std::vector<std::uint8_t> buffer_;
    std::vector<std::uint8_t> input_buffer_;
    std::size_t input_position_{};
    std::size_t input_size_{};
};

}  // namespace trading