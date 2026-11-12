#include "lob/order_book.hpp"
#include "print_trade.hpp"
#include "reference_book.hpp"

#include <gtest/gtest.h>

#include <random>
#include <vector>

using namespace lob;
using lob::testing::ReferenceBook;

namespace {

constexpr Price kLowPrice = 95;
constexpr Price kHighPrice = 105;

// Compares everything observable about the two books.
void expect_same_state(const OrderBook& book, const ReferenceBook& reference, OrderId max_id) {
    ASSERT_EQ(book.order_count(), reference.order_count());
    ASSERT_EQ(book.best_bid(), reference.best(Side::Buy));
    ASSERT_EQ(book.best_ask(), reference.best(Side::Sell));
    for (Price p = kLowPrice; p <= kHighPrice; ++p) {
        ASSERT_EQ(book.depth(Side::Buy, p), reference.depth(Side::Buy, p)) << "bid depth at " << p;
        ASSERT_EQ(book.depth(Side::Sell, p), reference.depth(Side::Sell, p)) << "ask depth at " << p;
    }
    for (OrderId id = 1; id <= max_id; ++id)
        ASSERT_EQ(book.open_quantity(id), reference.open_quantity(id)) << "order " << id;
}

// Drives the real book and the naive reference book with the same random
// stream of requests and requires identical trades and identical state.
void run_differential(std::uint64_t seed, int steps) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> action(0, 99);
    std::uniform_int_distribution<Price> price(kLowPrice, kHighPrice);
    std::uniform_int_distribution<Quantity> quantity(0, 20);  // 0 exercises the reject path
    std::bernoulli_distribution coin(0.5);

    OrderBook book;
    ReferenceBook reference;
    OrderId next_id = 1;

    for (int step = 0; step < steps; ++step) {
        const int a = action(rng);
        const Side side = coin(rng) ? Side::Buy : Side::Sell;
        // Ids up to next_id + 1 cover live, dead and never-used orders.
        const OrderId some_id = std::uniform_int_distribution<OrderId>(1, next_id + 1)(rng);

        if (a < 50) {  // new limit order; sometimes with an id already in use
            const OrderId id = a < 3 ? some_id : next_id++;
            const Price p = price(rng);
            const Quantity q = quantity(rng);
            const Result got = book.add_limit(id, side, p, q);
            const auto want = reference.add_limit(id, side, p, q);
            ASSERT_EQ(got.accepted, want.accepted) << "step " << step;
            ASSERT_EQ(std::vector<Trade>(got.trades.begin(), got.trades.end()), want.trades)
                << "step " << step;
            if (got.accepted)
                ASSERT_EQ(got.resting, reference.open_quantity(id)) << "step " << step;
        } else if (a < 60) {  // market order
            const OrderId id = next_id++;
            const Quantity q = quantity(rng);
            const Result got = book.add_market(id, side, q);
            const auto want = reference.add_market(id, side, q);
            ASSERT_EQ(got.accepted, want.accepted) << "step " << step;
            ASSERT_EQ(std::vector<Trade>(got.trades.begin(), got.trades.end()), want.trades)
                << "step " << step;
        } else if (a < 80) {  // cancel
            ASSERT_EQ(book.cancel(some_id), reference.cancel(some_id)) << "step " << step;
        } else {  // modify
            const Price p = price(rng);
            const Quantity q = quantity(rng);
            const Result got = book.modify(some_id, p, q);
            const auto want = reference.modify(some_id, p, q);
            ASSERT_EQ(got.accepted, want.accepted) << "step " << step;
            ASSERT_EQ(std::vector<Trade>(got.trades.begin(), got.trades.end()), want.trades)
                << "step " << step;
        }

        // A book with price-time priority can never be left crossed.
        if (book.best_bid() && book.best_ask())
            ASSERT_LT(*book.best_bid(), *book.best_ask()) << "step " << step;

        ASSERT_EQ(book.order_count(), reference.order_count()) << "step " << step;
        if (step % 500 == 0)
            expect_same_state(book, reference, next_id);
    }
    expect_same_state(book, reference, next_id);
}

}  // namespace

TEST(OrderBookRandomized, MatchesReferenceBookSeed1) {
    run_differential(1, 20000);
}

TEST(OrderBookRandomized, MatchesReferenceBookSeed2) {
    run_differential(2, 20000);
}

TEST(OrderBookRandomized, MatchesReferenceBookSeed3) {
    run_differential(3, 20000);
}
