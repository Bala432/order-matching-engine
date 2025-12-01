#pragma once
#include <memory>
#include <string>
#include <optional>
#include <type_traits>

#include "Order.h"
#include "Orderbook.h"

// Detect if Orderbook has bool ctor
constexpr bool orderbook_has_bool_ctor = std::is_constructible_v<Orderbook, bool>;

// Factory helpers
inline std::shared_ptr<Order> MakeOrder(OrderType type, OrderId id, Side side, Price price, Quantity qty) {
    if(type == OrderType::Market)
        return std::make_shared<Order>(id, side, qty);
    else
        return std::make_shared<Order>(type, id, side, price, qty);
}

inline std::shared_ptr<Order> MakeGTC(OrderId id, Side side, Price price, Quantity qty) {
    return MakeOrder(OrderType::GoodTillCancel, id, side, price, qty);
}

// Test-orderbook factory that disables background threads when possible
inline Orderbook MakeTestOrderbook() {
    return Orderbook();   // your Orderbook has no bool constructor
}


// Safe wrappers to catch exceptions and return diagnostic messages.
// Tests should use these to avoid crashing the whole runner.
inline bool TryAddOrder(Orderbook &ob, const std::shared_ptr<Order> &o, std::string &err) {
    try { ob.AddOrder(o); return true; }
    catch (const std::exception &e) { err = e.what(); return false; }
    catch (...) { err = "non-std exception"; return false; }
}

inline bool TryCancelOrder(Orderbook &ob, OrderId id, std::string &err) {
    try { ob.CancelOrder(id); return true; }
    catch (const std::exception &e) { err = e.what(); return false; }
    catch (...) { err = "non-std exception"; return false; }
}

// Try to get the infos. Returns std::optional containing the value on success,
// or std::nullopt on exception (err populated with message).
inline std::optional<decltype(std::declval<Orderbook>().GetOrderInfos())>
TryGetInfos(Orderbook &ob, std::string &err) {
    try {
        return ob.GetOrderInfos();
    } catch (const std::exception &e) {
        err = e.what();
        return std::nullopt;
    } catch (...) {
        err = "non-std exception";
        return std::nullopt;
    }
}
