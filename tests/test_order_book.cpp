#include "lob/order_book.hpp"
#include "print_trade.hpp"

#include <gtest/gtest.h>

#include <vector>

using namespace lob;

namespace {

// Copies the trades out of a Result, because the span is only valid until the
// next call on the book.
std::vector<Trade> trades_of(const Result& result) {
    return {result.trades.begin(), result.trades.end()};
}

}  // namespace

TEST(OrderBook, StartsEmpty) {
    OrderBook book;
    EXPECT_TRUE(book.empty());
    EXPECT_EQ(book.order_count(), 0u);
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_FALSE(book.best_ask().has_value());
    EXPECT_EQ(book.depth(Side::Buy, 100), 0u);
}

TEST(OrderBook, RestingOrdersSetBestPricesAndDepth) {
    OrderBook book;
    const Result r = book.add_limit(1, Side::Buy, 99, 10);
    EXPECT_TRUE(r.accepted);
    EXPECT_EQ(r.filled, 0u);
    EXPECT_EQ(r.resting, 10u);
    EXPECT_TRUE(r.trades.empty());

    book.add_limit(2, Side::Buy, 98, 5);
    book.add_limit(3, Side::Buy, 99, 7);
    book.add_limit(4, Side::Sell, 101, 4);
    book.add_limit(5, Side::Sell, 103, 6);

    EXPECT_EQ(book.best_bid(), 99);
    EXPECT_EQ(book.best_ask(), 101);
    EXPECT_EQ(book.depth(Side::Buy, 99), 17u);
    EXPECT_EQ(book.depth(Side::Buy, 98), 5u);
    EXPECT_EQ(book.depth(Side::Sell, 101), 4u);
    EXPECT_EQ(book.depth(Side::Sell, 99), 0u);  // depth is per side
    EXPECT_EQ(book.level_count(Side::Buy), 2u);
    EXPECT_EQ(book.level_count(Side::Sell), 2u);
    EXPECT_EQ(book.order_count(), 5u);
    EXPECT_EQ(book.open_quantity(3), 7u);
}

TEST(OrderBook, IncomingOrderTradesAtRestingPrice) {
    OrderBook book;
    book.add_limit(1, Side::Sell, 100, 10);

    // The buyer is willing to pay 105 but gets the resting price of 100.
    const Result r = book.add_limit(2, Side::Buy, 105, 10);
    EXPECT_TRUE(r.accepted);
    EXPECT_EQ(r.filled, 10u);
    EXPECT_EQ(r.resting, 0u);
    EXPECT_EQ(trades_of(r), (std::vector<Trade>{{1, 2, 100, 10}}));
    EXPECT_TRUE(book.empty());
}

TEST(OrderBook, RemainderOfIncomingOrderRests) {
    OrderBook book;
    book.add_limit(1, Side::Sell, 100, 4);

    const Result r = book.add_limit(2, Side::Buy, 100, 10);
    EXPECT_EQ(r.filled, 4u);
    EXPECT_EQ(r.resting, 6u);
    EXPECT_EQ(trades_of(r), (std::vector<Trade>{{1, 2, 100, 4}}));
    EXPECT_EQ(book.best_bid(), 100);
    EXPECT_EQ(book.depth(Side::Buy, 100), 6u);
    EXPECT_FALSE(book.best_ask().has_value());
}

TEST(OrderBook, RemainderOfRestingOrderStaysAtFrontOfQueue) {
    OrderBook book;
    book.add_limit(1, Side::Sell, 100, 10);
    book.add_limit(2, Side::Sell, 100, 10);

    book.add_limit(3, Side::Buy, 100, 4);
    EXPECT_EQ(book.open_quantity(1), 6u);
    EXPECT_EQ(book.depth(Side::Sell, 100), 16u);

    // Order 1 was partly filled but is still first in line.
    const Result r = book.add_limit(4, Side::Buy, 100, 8);
    EXPECT_EQ(trades_of(r), (std::vector<Trade>{{1, 4, 100, 6}, {2, 4, 100, 2}}));
}

TEST(OrderBook, BetterPriceMatchesFirst) {
    OrderBook book;
    book.add_limit(1, Side::Sell, 102, 5);
    book.add_limit(2, Side::Sell, 100, 5);  // arrives later but is cheaper
    book.add_limit(3, Side::Buy, 97, 5);
    book.add_limit(4, Side::Buy, 99, 5);  // arrives later but bids higher

    EXPECT_EQ(trades_of(book.add_limit(5, Side::Buy, 102, 5)),
              (std::vector<Trade>{{2, 5, 100, 5}}));
    EXPECT_EQ(trades_of(book.add_limit(6, Side::Sell, 97, 5)),
              (std::vector<Trade>{{4, 6, 99, 5}}));
}

TEST(OrderBook, SamePriceMatchesInArrivalOrder) {
    OrderBook book;
    book.add_limit(1, Side::Buy, 100, 3);
    book.add_limit(2, Side::Buy, 100, 3);
    book.add_limit(3, Side::Buy, 100, 3);

    const Result r = book.add_limit(4, Side::Sell, 100, 7);
    EXPECT_EQ(trades_of(r),
              (std::vector<Trade>{{1, 4, 100, 3}, {2, 4, 100, 3}, {3, 4, 100, 1}}));
    EXPECT_EQ(book.open_quantity(3), 2u);
}

TEST(OrderBook, LimitOrderSweepsLevelsOnlyUpToItsLimit) {
    OrderBook book;
    book.add_limit(1, Side::Sell, 100, 5);
    book.add_limit(2, Side::Sell, 101, 5);
    book.add_limit(3, Side::Sell, 102, 5);

    const Result r = book.add_limit(4, Side::Buy, 101, 20);
    EXPECT_EQ(r.filled, 10u);
    EXPECT_EQ(r.resting, 10u);
    EXPECT_EQ(trades_of(r), (std::vector<Trade>{{1, 4, 100, 5}, {2, 4, 101, 5}}));
    EXPECT_EQ(book.best_bid(), 101);
    EXPECT_EQ(book.best_ask(), 102);
}

TEST(OrderBook, MarketOrderSweepsAndDropsUnfilledRemainder) {
    OrderBook book;
    book.add_limit(1, Side::Buy, 100, 5);
    book.add_limit(2, Side::Buy, 98, 5);

    const Result r = book.add_market(3, Side::Sell, 12);
    EXPECT_TRUE(r.accepted);
    EXPECT_EQ(r.filled, 10u);
    EXPECT_EQ(r.resting, 0u);
    EXPECT_EQ(trades_of(r), (std::vector<Trade>{{1, 3, 100, 5}, {2, 3, 98, 5}}));
    EXPECT_TRUE(book.empty());  // the 2 unfilled lots were dropped
}

TEST(OrderBook, MarketOrderOnEmptyBookTradesNothing) {
    OrderBook book;
    const Result r = book.add_market(1, Side::Buy, 5);
    EXPECT_TRUE(r.accepted);
    EXPECT_EQ(r.filled, 0u);
    EXPECT_TRUE(r.trades.empty());
    EXPECT_TRUE(book.empty());
}

TEST(OrderBook, CancelRemovesOrderAndEmptyLevel) {
    OrderBook book;
    book.add_limit(1, Side::Buy, 100, 5);
    book.add_limit(2, Side::Buy, 99, 5);

    EXPECT_TRUE(book.cancel(1));
    EXPECT_EQ(book.best_bid(), 99);
    EXPECT_EQ(book.depth(Side::Buy, 100), 0u);
    EXPECT_EQ(book.level_count(Side::Buy), 1u);
    EXPECT_EQ(book.open_quantity(1), 0u);

    EXPECT_FALSE(book.cancel(1));   // already gone
    EXPECT_FALSE(book.cancel(42));  // never existed
}

TEST(OrderBook, CancelInMiddleOfQueueKeepsOrderOfTheOthers) {
    OrderBook book;
    book.add_limit(1, Side::Sell, 100, 2);
    book.add_limit(2, Side::Sell, 100, 2);
    book.add_limit(3, Side::Sell, 100, 2);
    book.add_limit(4, Side::Sell, 100, 2);

    EXPECT_TRUE(book.cancel(2));
    EXPECT_TRUE(book.cancel(4));  // tail
    EXPECT_EQ(book.depth(Side::Sell, 100), 4u);

    const Result r = book.add_market(5, Side::Buy, 4);
    EXPECT_EQ(trades_of(r), (std::vector<Trade>{{1, 5, 100, 2}, {3, 5, 100, 2}}));
}

TEST(OrderBook, ModifyDownKeepsQueuePosition) {
    OrderBook book;
    book.add_limit(1, Side::Buy, 100, 10);
    book.add_limit(2, Side::Buy, 100, 10);

    const Result m = book.modify(1, 100, 4);
    EXPECT_TRUE(m.accepted);
    EXPECT_EQ(m.resting, 4u);
    EXPECT_EQ(book.depth(Side::Buy, 100), 14u);

    const Result r = book.add_limit(3, Side::Sell, 100, 6);
    EXPECT_EQ(trades_of(r), (std::vector<Trade>{{1, 3, 100, 4}, {2, 3, 100, 2}}));
}

TEST(OrderBook, ModifyUpMovesToBackOfQueue) {
    OrderBook book;
    book.add_limit(1, Side::Buy, 100, 5);
    book.add_limit(2, Side::Buy, 100, 5);

    EXPECT_TRUE(book.modify(1, 100, 8).accepted);
    EXPECT_EQ(book.depth(Side::Buy, 100), 13u);

    const Result r = book.add_limit(3, Side::Sell, 100, 6);
    EXPECT_EQ(trades_of(r), (std::vector<Trade>{{2, 3, 100, 5}, {1, 3, 100, 1}}));
}

TEST(OrderBook, ModifyPriceCanCrossAndTrade) {
    OrderBook book;
    book.add_limit(1, Side::Sell, 101, 5);
    book.add_limit(2, Side::Buy, 99, 8);

    const Result r = book.modify(2, 101, 8);
    EXPECT_TRUE(r.accepted);
    EXPECT_EQ(r.filled, 5u);
    EXPECT_EQ(r.resting, 3u);
    EXPECT_EQ(trades_of(r), (std::vector<Trade>{{1, 2, 101, 5}}));
    EXPECT_EQ(book.best_bid(), 101);
    EXPECT_EQ(book.depth(Side::Buy, 99), 0u);
}

TEST(OrderBook, RejectedRequestsLeaveTheBookUnchanged) {
    OrderBook book;
    book.add_limit(1, Side::Buy, 100, 5);

    EXPECT_FALSE(book.add_limit(1, Side::Sell, 100, 5).accepted);  // duplicate id
    EXPECT_FALSE(book.add_market(1, Side::Sell, 5).accepted);      // duplicate id
    EXPECT_FALSE(book.add_limit(2, Side::Buy, 100, 0).accepted);   // zero quantity
    EXPECT_FALSE(book.add_market(2, Side::Buy, 0).accepted);       // zero quantity
    EXPECT_FALSE(book.modify(9, 100, 5).accepted);                 // unknown id
    EXPECT_FALSE(book.modify(1, 100, 0).accepted);                 // zero quantity

    EXPECT_EQ(book.order_count(), 1u);
    EXPECT_EQ(book.open_quantity(1), 5u);
    EXPECT_EQ(book.depth(Side::Buy, 100), 5u);
}

TEST(OrderBook, IdOfAFilledOrderCanBeUsedAgain) {
    OrderBook book;
    book.add_limit(1, Side::Buy, 100, 5);
    book.add_market(2, Side::Sell, 5);
    EXPECT_TRUE(book.empty());

    EXPECT_TRUE(book.add_limit(1, Side::Sell, 101, 3).accepted);
    EXPECT_EQ(book.best_ask(), 101);
}
