#include "itch_parser.hpp"
#include "order_book.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>

namespace {

class ReplaySession {
public:
    explicit ReplaySession(std::string path) : path_(std::move(path)) {
        open();
    }

    void open() {
        try {
            parser_ = std::make_unique<trading::ItchParser>(path_);
            book_.clear();
            symbols_.clear();
            type_counts_.clear();
            applied_ = 0;
            rejected_ = 0;
            std::cout << "opened " << path_ << '\n';
        } catch (const std::exception& error) {
            parser_.reset();
            std::cout << "open failed: " << error.what() << '\n';
        }
    }

    void set_symbol(std::string symbol) {
        std::transform(symbol.begin(), symbol.end(), symbol.begin(),
                       [](unsigned char character) { return static_cast<char>(std::toupper(character)); });
        symbol_ = std::move(symbol);
        open();
        std::cout << "symbol=" << (symbol_.empty() ? "ALL" : symbol_) << '\n';
    }

    bool step() {
        if (!parser_) return false;
        trading::ItchEvent event;
        if (!parser_->next(event)) return false;
        ++type_counts_[event.raw_type];
        if (event.type == trading::ItchType::StockDirectory) {
            symbols_[event.stock_locate] = event.stock;
            return true;
        }
        if (!symbol_.empty()) {
            const auto found = symbols_.find(event.stock_locate);
            if (found == symbols_.end() || found->second != symbol_) return true;
        }
        if (book_.apply(event)) {
            ++applied_;
        } else if (event.type == trading::ItchType::AddOrder ||
                   event.type == trading::ItchType::OrderCancel ||
                   event.type == trading::ItchType::OrderExecuted ||
                   event.type == trading::ItchType::OrderDelete ||
                   event.type == trading::ItchType::OrderReplace) {
            ++rejected_;
        }
        return true;
    }

    void run(std::uint64_t count) {
        std::uint64_t processed = 0;
        while (processed < count && step()) ++processed;
        std::cout << "processed=" << processed << " parser_messages="
                  << (parser_ ? parser_->messages_read() : 0) << '\n';
    }

    void show_book(std::size_t depth) const {
        const auto asks = book_.asks(depth);
        const auto bids = book_.bids(depth);
        std::cout << "book symbol=" << (symbol_.empty() ? "ALL" : symbol_) << '\n';
        std::cout << "       ASK PRICE       QTY ORDERS | BID PRICE       QTY ORDERS\n";
        const auto rows = std::max(asks.size(), bids.size());
        for (std::size_t index = 0; index < rows; ++index) {
            if (index < asks.size()) {
                std::cout << std::setw(16) << price(asks[index].price_ticks)
                          << std::setw(12) << asks[index].quantity
                          << std::setw(7) << asks[index].order_count;
            } else {
                std::cout << std::setw(16) << "" << std::setw(12) << "" << std::setw(7) << "";
            }
            std::cout << " | ";
            if (index < bids.size()) {
                std::cout << std::setw(16) << price(bids[index].price_ticks)
                          << std::setw(12) << bids[index].quantity
                          << std::setw(7) << bids[index].order_count;
            }
            std::cout << '\n';
        }
    }

    void stats() const {
        std::cout << "symbol=" << (symbol_.empty() ? "ALL" : symbol_)
                  << " orders=" << book_.order_count()
                  << " bid_qty=" << book_.quantity(trading::Side::Buy)
                  << " ask_qty=" << book_.quantity(trading::Side::Sell)
                  << " applied=" << applied_ << " rejected_updates=" << rejected_ << '\n';
            std::cout << "types=";
            for (const auto& [type, count] : type_counts_) std::cout << type << ':' << count << ' ';
            std::cout << '\n';
        if (parser_ && parser_->truncated()) {
            std::cout << "warning=source stream ended before gzip trailer or message completion\n";
        }
    }

private:
    static std::string price(std::int64_t ticks) {
        std::ostringstream output;
        output << std::fixed << std::setprecision(4) << static_cast<double>(ticks) / 10000.0;
        return output.str();
    }

    std::string path_;
    std::string symbol_;
    std::unique_ptr<trading::ItchParser> parser_;
    trading::OrderBook book_;
    std::unordered_map<std::uint16_t, std::string> symbols_;
    std::unordered_map<char, std::uint64_t> type_counts_;
    std::uint64_t applied_{};
    std::uint64_t rejected_{};
};

void print_help() {
    std::cout << "commands:\n"
              << "  help                         show commands\n"
              << "  open <file>                  open raw ITCH or .gz dataset\n"
              << "  symbol <NASDAQ symbol>       reset and replay one symbol\n"
              << "  run [messages]               replay messages (default 100000)\n"
              << "  step                         replay one message\n"
              << "  book [levels]                show top of book (default 10)\n"
              << "  stats                        show replay and book statistics\n"
              << "  reset                        restart current input\n"
              << "  quit                         exit\n";
}

}  // namespace

int main(int argc, char** argv) {
    ReplaySession session(argc > 1 ? argv[1] : "data/07302019.NASDAQ_ITCH50.gz");
    std::cout << "ITCH market replay terminal; type help for commands\n";
    std::string line;
    while (std::cout << "itch> " && std::getline(std::cin, line)) {
        std::istringstream input(line);
        std::string command;
        input >> command;
        if (command == "help") {
            print_help();
        } else if (command == "open") {
            std::string path;
            input >> path;
            if (path.empty()) std::cout << "usage: open <file>\n";
            else session = ReplaySession(path);
        } else if (command == "symbol") {
            std::string symbol;
            input >> symbol;
            session.set_symbol(symbol);
        } else if (command == "run") {
            std::uint64_t count = 100000;
            input >> count;
            session.run(count);
        } else if (command == "step") {
            std::cout << (session.step() ? "ok\n" : "end of stream\n");
        } else if (command == "book") {
            std::size_t depth = 10;
            input >> depth;
            session.show_book(depth);
        } else if (command == "stats") {
            session.stats();
        } else if (command == "reset") {
            session.open();
        } else if (command == "quit" || command == "exit") {
            break;
        } else if (!command.empty()) {
            std::cout << "unknown command; type help\n";
        }
    }
}