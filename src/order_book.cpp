#include "lob/order_book.hpp"

#include <algorithm>

namespace lob {

OrderBook::OrderBook(std::size_t expected_orders) {
    orders_.reserve(expected_orders);
    index_.reserve(expected_orders);
}

Result OrderBook::add_limit(OrderId id, Side side, Price price, Quantity quantity) {
    trades_.clear();
    if (quantity == 0 || index_.contains(id))
        return {};

    Quantity remaining = 0;
    if (side == Side::Buy) {
        remaining = match(asks_, id, quantity, [price](Price ask) { return ask <= price; });
        if (remaining > 0)
            rest(bids_, id, side, price, remaining);
    } else {
        remaining = match(bids_, id, quantity, [price](Price bid) { return bid >= price; });
        if (remaining > 0)
            rest(asks_, id, side, price, remaining);
    }
    return {true, quantity - remaining, remaining, trades_};
}

Result OrderBook::add_market(OrderId id, Side side, Quantity quantity) {
    trades_.clear();
    if (quantity == 0 || index_.contains(id))
        return {};

    const auto any_price = [](Price) { return true; };
    const Quantity remaining = side == Side::Buy ? match(asks_, id, quantity, any_price)
                                                 : match(bids_, id, quantity, any_price);
    return {true, quantity - remaining, 0, trades_};
}

bool OrderBook::cancel(OrderId id) {
    const auto it = index_.find(id);
    if (it == index_.end())
        return false;

    const Slot slot = it->second;
    if (orders_[slot].side == Side::Buy)
        remove(bids_, slot);
    else
        remove(asks_, slot);
    return true;
}

Result OrderBook::modify(OrderId id, Price new_price, Quantity new_quantity) {
    trades_.clear();
    const auto it = index_.find(id);
    if (it == index_.end() || new_quantity == 0)
        return {};

    Order& order = orders_[it->second];
    if (new_price == order.price && new_quantity <= order.quantity) {
        Level& level = order.side == Side::Buy ? bids_.find(order.price)->second
                                               : asks_.find(order.price)->second;
        level.total -= order.quantity - new_quantity;
        order.quantity = new_quantity;
        return {true, 0, new_quantity, trades_};
    }

    const Side side = order.side;  // copy before cancel() recycles the slot
    cancel(id);
    return add_limit(id, side, new_price, new_quantity);
}

std::optional<Price> OrderBook::best_bid() const {
    if (bids_.empty())
        return std::nullopt;
    return bids_.begin()->first;
}

std::optional<Price> OrderBook::best_ask() const {
    if (asks_.empty())
        return std::nullopt;
    return asks_.begin()->first;
}

Quantity OrderBook::depth(Side side, Price price) const {
    if (side == Side::Buy) {
        const auto it = bids_.find(price);
        return it == bids_.end() ? 0 : it->second.total;
    }
    const auto it = asks_.find(price);
    return it == asks_.end() ? 0 : it->second.total;
}

Quantity OrderBook::open_quantity(OrderId id) const {
    const auto it = index_.find(id);
    return it == index_.end() ? 0 : orders_[it->second].quantity;
}

std::size_t OrderBook::level_count(Side side) const {
    return side == Side::Buy ? bids_.size() : asks_.size();
}

// Fills `quantity` against `levels`, best price first and oldest order first
// within a price, for as long as crosses(level price) holds. Returns the
// quantity that could not be filled.
template <class Levels, class Crosses>
Quantity OrderBook::match(Levels& levels, OrderId taker_id, Quantity quantity, Crosses crosses) {
    Quantity remaining = quantity;
    while (remaining > 0 && !levels.empty()) {
        const auto best = levels.begin();
        if (!crosses(best->first))
            break;

        Level& level = best->second;
        while (remaining > 0 && level.head != npos) {
            const Slot slot = level.head;
            Order& maker = orders_[slot];
            const Quantity fill = std::min(remaining, maker.quantity);
            trades_.push_back({maker.id, taker_id, maker.price, fill});
            remaining -= fill;
            maker.quantity -= fill;
            level.total -= fill;

            if (maker.quantity == 0) {
                level.head = maker.next;
                if (level.head != npos)
                    orders_[level.head].prev = npos;
                else
                    level.tail = npos;
                index_.erase(maker.id);
                release(slot);
            }
        }
        if (level.head == npos)
            levels.erase(best);
    }
    return remaining;
}

// Appends a new order to the back of the queue at `price`.
template <class Levels>
void OrderBook::rest(Levels& levels, OrderId id, Side side, Price price, Quantity quantity) {
    const Slot slot = allocate();
    Level& level = levels[price];  // creates the level if it is not there yet
    orders_[slot] = Order{id, price, quantity, side, level.tail, npos};
    if (level.tail != npos)
        orders_[level.tail].next = slot;
    else
        level.head = slot;
    level.tail = slot;
    level.total += quantity;
    index_.emplace(id, slot);
}

// Unlinks a resting order from its queue and drops the level if it is now empty.
template <class Levels>
void OrderBook::remove(Levels& levels, Slot slot) {
    const Order& order = orders_[slot];
    const auto it = levels.find(order.price);
    Level& level = it->second;

    if (order.prev != npos)
        orders_[order.prev].next = order.next;
    else
        level.head = order.next;
    if (order.next != npos)
        orders_[order.next].prev = order.prev;
    else
        level.tail = order.prev;

    level.total -= order.quantity;
    if (level.head == npos)
        levels.erase(it);
    index_.erase(order.id);
    release(slot);
}

OrderBook::Slot OrderBook::allocate() {
    if (!free_slots_.empty()) {
        const Slot slot = free_slots_.back();
        free_slots_.pop_back();
        return slot;
    }
    orders_.emplace_back();
    return static_cast<Slot>(orders_.size() - 1);
}

void OrderBook::release(Slot slot) {
    free_slots_.push_back(slot);
}

}  // namespace lob
