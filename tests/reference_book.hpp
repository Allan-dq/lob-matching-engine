#pragma once

#include "lob/types.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

namespace lob::testing {

// Deliberately naive order book used only as an oracle in tests: one flat
// vector, linear scans everywhere. Slow, but short enough to check by eye.
// It follows the same rules as lob::OrderBook.
class ReferenceBook {
  public:
    struct Outcome {
        bool accepted = false;
        std::vector<Trade> trades;
    };

    Outcome add_limit(OrderId id, Side side, Price price, Quantity quantity) {
        if (quantity == 0 || find(id) != orders_.end())
            return {};
        Outcome out{true, {}};
        const Quantity remaining = match(id, side, quantity, price, out.trades);
        if (remaining > 0)
            orders_.push_back({id, side, price, remaining, next_arrival_++});
        return out;
    }

    Outcome add_market(OrderId id, Side side, Quantity quantity) {
        if (quantity == 0 || find(id) != orders_.end())
            return {};
        Outcome out{true, {}};
        match(id, side, quantity, std::nullopt, out.trades);
        return out;
    }

    bool cancel(OrderId id) {
        const auto it = find(id);
        if (it == orders_.end())
            return false;
        orders_.erase(it);
        return true;
    }

    Outcome modify(OrderId id, Price new_price, Quantity new_quantity) {
        const auto it = find(id);
        if (it == orders_.end() || new_quantity == 0)
            return {};
        if (new_price == it->price && new_quantity <= it->quantity) {
            it->quantity = new_quantity;
            return {true, {}};
        }
        const Side side = it->side;
        orders_.erase(it);
        return add_limit(id, side, new_price, new_quantity);
    }

    std::optional<Price> best(Side side) const {
        std::optional<Price> result;
        for (const Entry& o : orders_) {
            if (o.side != side)
                continue;
            if (!result || (side == Side::Buy ? o.price > *result : o.price < *result))
                result = o.price;
        }
        return result;
    }

    Quantity depth(Side side, Price price) const {
        Quantity total = 0;
        for (const Entry& o : orders_)
            if (o.side == side && o.price == price)
                total += o.quantity;
        return total;
    }

    Quantity open_quantity(OrderId id) const {
        for (const Entry& o : orders_)
            if (o.id == id)
                return o.quantity;
        return 0;
    }

    std::size_t order_count() const { return orders_.size(); }

  private:
    struct Entry {
        OrderId id;
        Side side;
        Price price;
        Quantity quantity;
        std::uint64_t arrival;  // smaller = earlier = higher time priority
    };

    std::vector<Entry>::iterator find(OrderId id) {
        return std::find_if(orders_.begin(), orders_.end(),
                            [id](const Entry& o) { return o.id == id; });
    }

    // limit == nullopt means a market order. Returns the unfilled quantity.
    Quantity match(OrderId taker_id, Side taker_side, Quantity quantity,
                   std::optional<Price> limit, std::vector<Trade>& trades) {
        while (quantity > 0) {
            // Pick the opposite-side order with the best price, earliest arrival.
            auto best = orders_.end();
            for (auto it = orders_.begin(); it != orders_.end(); ++it) {
                if (it->side == taker_side)
                    continue;
                if (best == orders_.end()) {
                    best = it;
                    continue;
                }
                const bool better_price =
                    taker_side == Side::Buy ? it->price < best->price : it->price > best->price;
                if (better_price || (it->price == best->price && it->arrival < best->arrival))
                    best = it;
            }
            if (best == orders_.end())
                break;
            if (limit && (taker_side == Side::Buy ? best->price > *limit : best->price < *limit))
                break;

            const Quantity fill = std::min(quantity, best->quantity);
            trades.push_back({best->id, taker_id, best->price, fill});
            quantity -= fill;
            best->quantity -= fill;
            if (best->quantity == 0)
                orders_.erase(best);
        }
        return quantity;
    }

    std::vector<Entry> orders_;
    std::uint64_t next_arrival_ = 0;
};

}  // namespace lob::testing
