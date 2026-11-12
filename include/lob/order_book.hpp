#pragma once

#include "lob/types.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace lob {

// Outcome of add_limit, add_market and modify.
struct Result {
    bool accepted = false;  // false: request rejected, book unchanged
    Quantity filled = 0;    // quantity executed immediately
    Quantity resting = 0;   // quantity left on the book under this order id
    // Executions in match order. The span points into a buffer owned by the
    // book and is valid until the next non-const call on the same book.
    std::span<const Trade> trades;
};

// Single-instrument limit order book with price-time priority.
//
// Not thread-safe: one book is meant to be driven by one thread.
class OrderBook {
  public:
    // expected_orders pre-allocates storage for that many resting orders.
    explicit OrderBook(std::size_t expected_orders = 0);

    // Matches against the opposite side while the price crosses, then rests
    // any remainder. Rejected if quantity is zero or id is already on the book.
    Result add_limit(OrderId id, Side side, Price price, Quantity quantity);

    // Matches against the opposite side at any price. The unfilled remainder
    // is dropped, never rested. Rejected like add_limit.
    Result add_market(OrderId id, Side side, Quantity quantity);

    // Removes a resting order. Returns false if id is not on the book.
    bool cancel(OrderId id);

    // Sets the price and open quantity of a resting order.
    // Same price and a smaller or equal quantity: the order keeps its place in
    // the queue. Anything else is a cancel followed by add_limit, so the order
    // goes to the back of the queue and may trade if the new price crosses.
    // Rejected if id is not on the book or new_quantity is zero.
    Result modify(OrderId id, Price new_price, Quantity new_quantity);

    std::optional<Price> best_bid() const;
    std::optional<Price> best_ask() const;
    Quantity depth(Side side, Price price) const;  // total resting quantity at a price
    Quantity open_quantity(OrderId id) const;      // 0 if id is not on the book
    std::size_t level_count(Side side) const;
    std::size_t order_count() const { return index_.size(); }
    bool empty() const { return index_.empty(); }

  private:
    // Orders live in one vector and refer to each other by position (slot),
    // so the queue at each price is a doubly linked list without per-order
    // heap allocation.
    using Slot = std::uint32_t;
    static constexpr Slot npos = std::numeric_limits<Slot>::max();

    struct Order {
        OrderId id = 0;
        Price price = 0;
        Quantity quantity = 0;
        Side side = Side::Buy;
        Slot prev = npos;  // towards the front of the queue
        Slot next = npos;  // towards the back of the queue
    };

    struct Level {
        Quantity total = 0;
        Slot head = npos;  // oldest order, matched first
        Slot tail = npos;  // newest order
    };

    // begin() is the best price on both sides: highest bid, lowest ask.
    using Bids = std::map<Price, Level, std::greater<>>;
    using Asks = std::map<Price, Level>;

    template <class Levels, class Crosses>
    Quantity match(Levels& levels, OrderId taker_id, Quantity quantity, Crosses crosses);

    template <class Levels>
    void rest(Levels& levels, OrderId id, Side side, Price price, Quantity quantity);

    template <class Levels>
    void remove(Levels& levels, Slot slot);

    Slot allocate();
    void release(Slot slot);

    Bids bids_;
    Asks asks_;
    std::vector<Order> orders_;
    std::vector<Slot> free_slots_;
    std::unordered_map<OrderId, Slot> index_;
    std::vector<Trade> trades_;
};

}  // namespace lob
