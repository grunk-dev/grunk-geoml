// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// Hand-written escape hatch for geoml::ShapePredicate's own combinator API
// (geoml/predicates/predicate_functions.h), which the generator can't reach on its
// own for two distinct reasons - see DESIGN.md's "geoml::ShapePredicate combinators:
// Lua syntax":
//
// 1. operator&&/operator||/operator! are free functions (not members), and
//    grunk.codegen's shared generator explicitly skips any free function named
//    "operator*" (collect_namespace_callables's own "operators not handled yet").
//    Even if it didn't, Lua's `and`/`or`/`not` are language keywords with no
//    metamethod hook in any Lua version - grunk's own Lua runtime is confirmed to be
//    real Lua 5.4 (not LuaJIT), so its native bitwise operators/metamethods
//    (__band/__bor/__bnot) are used instead: p1 & p2, p1 | p2, ~p1.
// 2. is_vertex/is_edge/is_face/is_solid/has_origin are `inline ShapePredicate const`
//    global *objects*, not functions - entirely invisible to a generator that only
//    ever discovers callables.
//
// Called from src/geoml_plugin.cpp, after register_naming_choosing(ns) (which
// registers ShapePredicate itself, generated/naming_choosing.cpp) - modify_type
// requires the usertype to already exist.

#pragma once

#include <grunk/dynamic.hpp>

#include <geoml/naming_choosing/Shape.h>
#include <geoml/predicates/predicate_functions.h>

inline void register_predicate_extras(grunk::plugin_namespace& ns)
{
    ns.modify_type<geoml::ShapePredicate>("ShapePredicate")
        .add_member_function(sol::meta_function::bitwise_and, [](geoml::ShapePredicate const& l, geoml::ShapePredicate const& r) -> geoml::ShapePredicate { return l && r; })
        .add_member_function(sol::meta_function::bitwise_or, [](geoml::ShapePredicate const& l, geoml::ShapePredicate const& r) -> geoml::ShapePredicate { return l || r; })
        .add_member_function(sol::meta_function::bitwise_not, [](geoml::ShapePredicate const& p) -> geoml::ShapePredicate { return !p; });

    // Named predicate constants - plain table entries, not functions/constructors.
    // ShapePredicate has its own operator() (it's a predicate, after all), which
    // makes sol2's table_proxy::operator=(T&&) automagically route a plain
    // `table[key] = value` assignment to set_function(...) instead of set(...)
    // (table_proxy.hpp: `if constexpr (is_invocable_v<Tu>) return
    // set_function(...)`) - calling .set(...) directly bypasses that "is this
    // secretly a callable I should wrap as a function?" detection entirely, storing
    // the value as plain table data instead, which is what these actually are.
    // set() isn't const-qualified (it does mutate the underlying Lua table), so a
    // local non-const copy is needed - table() itself returns a const&, and sol2's
    // sol::table is a lightweight reference handle (copying it doesn't copy the
    // underlying Lua table).
    sol::table table = ns.table();
    table.set("is_vertex", geoml::is_vertex);
    table.set("is_edge", geoml::is_edge);
    table.set("is_face", geoml::is_face);
    table.set("is_solid", geoml::is_solid);
    table.set("has_origin", geoml::has_origin);
}
