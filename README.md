# lob-matching-engine

A single-instrument limit order book with a price-time-priority matching engine, in C++20.
The library has no dependencies beyond the standard library; the tests use GoogleTest.

**Status: work in progress.** The matching core and its tests are done. A throughput and
latency benchmark and a replay of public exchange data are next, so there are no performance
numbers here yet.

## What it does

| Request | Behaviour |
|---|---|
| `add_limit(id, side, price, quantity)` | Trades against the opposite side while the price crosses, best price first and oldest order first within a price. Any remainder rests on the book. |
| `add_market(id, side, quantity)` | Trades at any price until filled or the opposite side is empty. The unfilled remainder is dropped. |
| `cancel(id)` | Removes a resting order. |
| `modify(id, new_price, new_quantity)` | Same price and a smaller quantity keeps the order's place in the queue. A new price or a larger quantity is a cancel and re-entry at the back of the queue, and can trade. |

Every trade is at the resting order's price. Requests with a zero quantity, an id that is
already on the book, or (for `modify`) an unknown id are rejected and leave the book unchanged.

```cpp
#include "lob/order_book.hpp"

lob::OrderBook book;
book.add_limit(1, lob::Side::Sell, 101, 5);   // rests
book.add_limit(2, lob::Side::Sell, 100, 5);   // rests, better price
auto r = book.add_limit(3, lob::Side::Buy, 101, 8);
// r.trades: {maker 2, taker 3, price 100, qty 5}, {maker 1, taker 3, price 101, qty 3}
// r.filled == 8, r.resting == 0, book.best_ask() == 101 with 2 left
```

## Design

- **Prices are integer ticks**, never floating point, so price comparison is exact.
- **Price levels** are kept in a `std::map` per side (bids sorted high to low, asks low to
  high), so the best price is always `begin()`. Finding or creating a level is O(log L) in
  the number of levels; reading the best price is O(1).
- **The queue at each price** is a doubly linked list threaded through one `std::vector` of
  orders. Orders refer to their neighbours by index, not pointer, so there is no heap
  allocation per order and the vector can grow without invalidating links. Slots of filled
  and cancelled orders are reused through a free list.
- **Cancel is O(1)** after the id lookup: an `unordered_map` from order id to slot finds the
  order, and the list links remove it from the middle of its queue without a scan.
- **Trades are returned as a `std::span`** over a buffer the book reuses, so matching does
  not allocate once the buffer has grown.

Out of scope for now: multiple instruments, order types beyond limit and market (IOC, FOK,
stop, iceberg), self-trade prevention, and thread safety (one book, one thread).

## Tests

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

- `tests/test_order_book.cpp`: one test per rule (price priority, time priority, partial
  fills on either side, sweeping levels, market orders, cancel from the middle of a queue,
  both kinds of modify, rejects).
- `tests/test_randomized.cpp`: a differential test. The same seeded stream of 20,000 random
  requests drives the real book and a deliberately naive reference book
  (`tests/reference_book.hpp`: one vector, linear scans). After every request the trades must
  be identical, the book must not be crossed, and the visible state must match.

`-DLOB_SANITIZE=ON` builds with AddressSanitizer and UndefinedBehaviorSanitizer; CI runs the
tests both ways on Linux and macOS.

## License

MIT
