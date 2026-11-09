// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <Standard_Handle.hxx>
#include <sol/sol.hpp>

#include <geoml/naming_choosing/Shape.h>

#include <Geom_Curve.hxx>
#include <Geom_BoundedCurve.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_Surface.hxx>
#include <Geom_BoundedSurface.hxx>
#include <Geom_BSplineSurface.hxx>

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
//
// rebind_actual_type (added 2026-09-28, mirroring the identical fix in grunk-occt's
// own occt_sol_traits.hpp - see that file's own, longer comment for the full
// forensic writeup) is what makes sol2's inheritance<T>::type_unique_cast
// (sol/inheritance.hpp) actually walk a type's own add_bases<...>() list when a
// Handle(Derived) value needs to satisfy a Handle(Base) parameter/getter -
// without it, sol2 unconditionally treats that base list as empty and the
// conversion always "fails" (silently null, or "no matching function call" during
// overload resolution), independent of what add_bases<...>() actually declares.
// This copy needs the identical fix because a Handle(Geom_BSplineCurve)/
// Handle(Geom_BSplineSurface) value produced by *this* plugin's own generated code
// (interpolate_points_to_b_spline_curve, interpolate_curves, ...) has its sol2
// "unique_tag" conversion callback baked in at push time using whichever
// unique_usertype_traits specialization was visible in *this* plugin's own
// translation units - grunk-occt's fix alone does not reach values this plugin
// itself creates (this is also why the SOL_BASE_CLASSES/SOL_DERIVED_CLASSES
// declarations below have to be duplicated here too, not just in grunk-occt - see
// their own comment).
namespace sol {
    template <typename T>
    struct unique_usertype_traits<opencascade::handle<T>> {
        using type = T;
        using actual_type = opencascade::handle<T>;
        static const bool value = true;

        template <typename X>
        using rebind_actual_type = opencascade::handle<X>;

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

// SOL_BASE_CLASSES/SOL_DERIVED_CLASSES for the OCCT Geom_Curve/Geom_Surface family
// this plugin's own return values actually need (interpolate_points_to_b_spline_curve/
// nurbs_curve -> Handle(Geom_BSplineCurve); interpolate_curves/interpolate_curve_network
// -> Handle(Geom_BSplineSurface)) - see grunk-occt/DESIGN.md's "Handle(Derived)->
// Handle(Base) argument conversion: real root cause and fix" for what these two macros
// are and why sol2 needs them (a *separate*, compile-time-only mechanism from
// .add_bases<...>()) for its own Handle(Derived)->Handle(Base) unique-usertype
// conversion to work at all.
//
// Hand-written here, not generated: `grunk.codegen`'s new sol_base_classes.hpp
// generation (this project's own generated/sol_base_classes.hpp) only ever covers
// classes *this run's own* codegen discovers - i.e. classes actually *defined*
// somewhere under this plugin's own --include-dir. Geom_Curve/Geom_Surface and their
// concrete subtypes are OCCT types this plugin only *references* (as return types),
// never defines - they live entirely under grunk-occt's own, completely separate
// --include-dir/codegen run, which never sees this plugin's own translation units
// either. Per this plugin's own "Handle(X) cross-plugin sharing" finding, that's
// normally fine (sol2's ordinary Handle(X) push/pull needs no shared registration
// at all) - but SOL_BASE_CLASSES/SOL_DERIVED_CLASSES are explicit, compile-time-only
// template specializations that must be visible in *this* plugin's own translation
// units specifically, wherever *this* plugin's own code pushes/checks a
// Handle(Geom_BSplineCurve)/Handle(Geom_BSplineSurface) value - grunk-occt's own
// (separately generated, separately compiled) sol_base_classes.hpp cannot reach
// this plugin's own .so at all. Safe to declare independently here: like
// unique_usertype_traits above, these are self-contained template specializations,
// each plugin's own copy only ever instantiated within its own translation units -
// no ODR conflict with grunk-occt's own, separately-compiled copy of the same
// declarations.
SOL_BASE_CLASSES(Geom_BSplineCurve, Geom_BoundedCurve, Geom_Curve);
SOL_BASE_CLASSES(Geom_BSplineSurface, Geom_BoundedSurface, Geom_Surface);
SOL_DERIVED_CLASSES(Geom_Curve, Geom_BSplineCurve);
SOL_DERIVED_CLASSES(Geom_Surface, Geom_BSplineSurface);

