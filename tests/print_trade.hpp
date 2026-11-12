#pragma once

#include "lob/types.hpp"

#include <ostream>

namespace lob {

// Lets GoogleTest print a Trade in failure messages.
inline void PrintTo(const Trade& t, std::ostream* os) {
    *os << "{maker " << t.maker_id << ", taker " << t.taker_id << ", price " << t.price
        << ", qty " << t.quantity << "}";
}

}  // namespace lob
