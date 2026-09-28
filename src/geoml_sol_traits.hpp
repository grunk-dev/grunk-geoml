// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <Standard_Handle.hxx>
#include <sol/sol.hpp>

#include <geoml/naming_choosing/Shape.h>

// Lets sol2 treat an OCCT opencascade::handle<T> (i.e. Handle(T)) like any other
// unique-ownership smart pointer, so a Handle can be returned from a registered
// grunk function or pulled back out of a grunk::object via .as<Handle(T)>() without
// a raw-pointer detour - byte-identical to grunk-occt's own src/occt_sol_traits.hpp
// (itself ported verbatim from grunk's cad_autodiff example,
// plugins/occt_sol_traits.hpp). Deliberately duplicated, not shared across repos -
// see DESIGN.md's "hooks.py duplication is intentional": this is a template
// specialization for opencascade::handle<T>, safe to define identically in more than
// one independently-compiled plugin (no ODR issue - each plugin's own copy is only
// ever instantiated within its own translation units), and per DESIGN.md's own
// Handle(X) cross-plugin investigation, this plugin never needs to coordinate with
// grunk-occt's copy at all - Handle(X) values cross the plugin boundary via sol2's
// own C++-type-identity-keyed usertype registry, not via anything in this header.
namespace sol {
    template <typename T>
    struct unique_usertype_traits<opencascade::handle<T>> {
        using type = T;
        using actual_type = opencascade::handle<T>;
        static const bool value = true;

        static bool is_null(const actual_type& ptr) {
            return ptr.IsNull();
        }

        static type* get(const actual_type& ptr) {
            return ptr.get();
        }
    };
}

// geoml::Shape genuinely declares begin()/end()/const_iterator (a real, hand-written
// iteration API for its own subshape tree - see geoml/naming_choosing/Shape.h), which
// makes sol2's own is_container<T> trait (a structural has_begin_end_v<T> check, sol/
// types.hpp) treat it as container-shaped at compile time, regardless of anything
// this plugin does or doesn't register (automagic_flags::none does not suppress
// this - confirmed empirically, see DESIGN.md's Handle(X) spike for the same finding
// applied to a different sol2 code path). This breaks EVERY member function taking a
// Shape (including `self`) as a parameter, not just begin/end themselves (blacklisted
// separately in codegen/config.yml for a different, narrower reason - their own
// nested-typedef return spelling): sol2's argument-marshaling for a container-shaped
// type routes through sol::nested<T>, which requires a T::value_type typedef Shape
// doesn't have ("no type named 'value_type'" - a real compile error, confirmed).
// sol2's own idiomatic opt-out (precedent: sol/variadic_results.hpp's identical
// specialization for basic_variadic_results) is a false is_container specialization,
// not a registration-time flag - this is the correct fix, not a workaround.
namespace sol {
    template <>
    struct is_container<geoml::Shape> : std::false_type {};
}

