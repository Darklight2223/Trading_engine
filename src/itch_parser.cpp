#include "itch_parser.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#endif

namespace trading {
namespace {

std::uint16_t read_u16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>((data[0] << 8U) | data[1]);
}

std::uint32_t read_u32(const std::uint8_t* data) {
    return (static_cast<std::uint32_t>(data[0]) << 24U) |
           (static_cast<std::uint32_t>(data[1]) << 16U) |
           (static_cast<std::uint32_t>(data[2]) << 8U) |
           static_cast<std::uint32_t>(data[3]);
}

std::uint64_t read_u64(const std::uint8_t* data, std::size_t size) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < size; ++index) {
        value = (value << 8U) | data[index];
    }
    return value;
}

std::string read_text(const std::uint8_t* data, std::size_t size) {
    std::string value(reinterpret_cast<const char*>(data), size);
    while (!value.empty() && value.back() == ' ') value.pop_back();
    return value;
}

ItchType type_for(char type) {
    switch (type) {
    case 'S': return ItchType::System;
    case 'R': return ItchType::StockDirectory;
    case 'A': return ItchType::AddOrder;
    case 'F': return ItchType::AddOrderWithAttribution;
    case 'E': return ItchType::OrderExecuted;
    case 'C': return ItchType::OrderExecutedWithPrice;
    case 'X': return ItchType::OrderCancel;
    case 'D': return ItchType::OrderDelete;
    case 'U': return ItchType::OrderReplace;
    case 'P': return ItchType::Trade;
    case 'H':
    case 'Y':
    case 'L':
    case 'V':
    case 'W':
    case 'K':
    case 'J':
    case 'h': return ItchType::MarketControl;
    case 'Q': return ItchType::CrossTrade;
    case 'B': return ItchType::BrokenTrade;
    case 'I': return ItchType::Imbalance;
    case 'N': return ItchType::RetailPriceImprovement;
    default: return ItchType::Unknown;
    }
}

#ifdef _WIN32
struct GzipApi {
    HMODULE library{};
    void* (*open)(const char*, const char*){};
    int (*read)(void*, void*, unsigned int){};
    int (*close)(void*){};
};
#endif

}  // namespace

ItchParser::ItchParser(std::string path, bool strict)
    : path_(std::move(path)), strict_(strict) {
    if (path_.size() >= 3 && path_.substr(path_.size() - 3) == ".gz") {
#ifdef _WIN32
        auto* api = new GzipApi{};
        api->library = LoadLibraryA("zlib1.dll");
        if (api->library == nullptr) {
            delete api;
            throw std::runtime_error("zlib1.dll is required for gzip input: " + path_);
        }
        api->open = reinterpret_cast<decltype(api->open)>(GetProcAddress(api->library, "gzopen"));
        api->read = reinterpret_cast<decltype(api->read)>(GetProcAddress(api->library, "gzread"));
        api->close = reinterpret_cast<decltype(api->close)>(GetProcAddress(api->library, "gzclose"));
        if (api->open == nullptr || api->read == nullptr || api->close == nullptr) {
            FreeLibrary(api->library);
            delete api;
            throw std::runtime_error("zlib1.dll does not export the gzip API");
        }
        gzip_api_ = api;
        gzip_file_ = api->open(path_.c_str(), "rb");
        if (gzip_file_ == nullptr) {
            close_gzip();
            throw std::runtime_error("unable to open gzip input: " + path_);
        }
#else
        throw std::runtime_error("native gzip input currently requires Windows zlib1.dll");
#endif
    } else {
        raw_file_.open(path_, std::ios::binary);
        if (!raw_file_) throw std::runtime_error("unable to open ITCH file: " + path_);
    }
    buffer_.reserve(65535);
    input_buffer_.resize(1U << 20U);
}

ItchParser::~ItchParser() {
    close_gzip();
}

void ItchParser::close_gzip() noexcept {
#ifdef _WIN32
    auto* api = static_cast<GzipApi*>(gzip_api_);
    if (api == nullptr) return;
    if (gzip_file_ != nullptr && api->close(gzip_file_) != 0) truncated_ = true;
    if (api->library != nullptr) FreeLibrary(api->library);
    delete api;
    gzip_file_ = nullptr;
    gzip_api_ = nullptr;
#endif
}

bool ItchParser::read_bytes(void* destination, std::size_t size) {
    if (raw_file_.is_open()) {
        raw_file_.read(static_cast<char*>(destination), static_cast<std::streamsize>(size));
        return static_cast<std::size_t>(raw_file_.gcount()) == size;
    }
    auto* api = static_cast<GzipApi*>(gzip_api_);
    if (api == nullptr || gzip_file_ == nullptr) return false;
    auto* output = static_cast<std::uint8_t*>(destination);
    std::size_t copied = 0;
    while (copied < size) {
        if (input_position_ == input_size_) {
            const auto count = api->read(gzip_file_, input_buffer_.data(),
                                         static_cast<unsigned int>(input_buffer_.size()));
            if (count < 0) {
                truncated_ = true;
                close_gzip();
                return false;
            }
            if (count == 0) {
                close_gzip();
                return false;
            }
            input_position_ = 0;
            input_size_ = static_cast<std::size_t>(count);
        }
        const auto available = input_size_ - input_position_;
        const auto requested = size - copied;
        const auto count = std::min(available, requested);
        std::memcpy(output + copied, input_buffer_.data() + input_position_, count);
        input_position_ += count;
        copied += count;
    }
    return true;
}

bool ItchParser::read_message(std::vector<std::uint8_t>& message) {
    std::uint8_t header[2]{};
    if (!read_bytes(header, sizeof(header))) return false;
    const auto length = read_u16(header);
    if (length == 0) {
        truncated_ = true;
        return false;
    }
    message.resize(length);
    if (!read_bytes(message.data(), message.size())) {
        truncated_ = true;
        return false;
    }
    return true;
}

bool ItchParser::next(ItchEvent& event) {
    if (buffer_.empty()) buffer_.reserve(65535);
    if (!read_message(buffer_)) {
        if (strict_ && truncated_) throw std::runtime_error("truncated ITCH stream: " + path_);
        return false;
    }

    const auto* message = buffer_.data();
    const auto length = buffer_.size();
    event = ItchEvent{};
    event.raw_type = static_cast<char>(message[0]);
    event.type = type_for(event.raw_type);
    if (length >= 11) {
        event.stock_locate = read_u16(message + 1);
        event.tracking_number = read_u16(message + 3);
        event.timestamp_nanoseconds = read_u64(message + 5, 6);
    }
    if (event.raw_type == 'S' && length >= 12) {
        event.event_code = static_cast<char>(message[11]);
    } else if (event.raw_type == 'R' && length >= 19) {
        event.stock = read_text(message + 11, 8);
    } else if ((event.raw_type == 'A' || event.raw_type == 'F') && length >= 36) {
        event.order_reference = read_u64(message + 11, 8);
        event.side = static_cast<char>(message[19]) == 'B' ? Side::Buy : Side::Sell;
        event.shares = read_u32(message + 20);
        event.stock = read_text(message + 24, 8);
        event.price_ticks = static_cast<std::int64_t>(read_u32(message + 32));
    } else if (event.raw_type == 'X' && length >= 23) {
        event.order_reference = read_u64(message + 11, 8);
        event.shares = read_u32(message + 19);
    } else if (event.raw_type == 'D' && length >= 19) {
        event.order_reference = read_u64(message + 11, 8);
    } else if ((event.raw_type == 'E' || event.raw_type == 'C') && length >= 31) {
        event.order_reference = read_u64(message + 11, 8);
        event.shares = read_u32(message + 19);
        if (event.raw_type == 'C' && length >= 36) {
            event.printable = message[31] == 'Y';
            event.price_ticks = static_cast<std::int64_t>(read_u32(message + 32));
        }
    } else if (event.raw_type == 'U' && length >= 35) {
        event.order_reference = read_u64(message + 11, 8);
        event.new_order_reference = read_u64(message + 19, 8);
        event.shares = read_u32(message + 27);
        event.price_ticks = static_cast<std::int64_t>(read_u32(message + 31));
    } else if (event.raw_type == 'P' && length >= 44) {
        event.order_reference = read_u64(message + 11, 8);
        event.side = static_cast<char>(message[19]) == 'B' ? Side::Buy : Side::Sell;
        event.shares = read_u32(message + 20);
        event.stock = read_text(message + 24, 8);
        event.price_ticks = static_cast<std::int64_t>(read_u32(message + 32));
    }
    ++messages_read_;
    return true;
}

std::uint64_t ItchParser::messages_read() const noexcept {
    return messages_read_;
}

bool ItchParser::truncated() const noexcept {
    return truncated_;
}

}  // namespace trading