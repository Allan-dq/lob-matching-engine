#pragma once

#include <cstdint>

namespace lob {

using OrderId = std::uint64_t;
using Price = std::int64_t;  // price in integer ticks, never floating point
using Quantity = std::uint64_t;

enum class Side : std::uint8_t { Buy, Sell };

// One execution between a resting order (maker) and an incoming order (taker).
// The trade price is always the resting order's price.
struct Trade {
    OrderId maker_id;
    OrderId taker_id;
    Price price;
    Quantity quantity;

    friend bool operator==(const Trade&, const Trade&) = default;
};

}  // namespace lob
