// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// Hand-written escape hatch for geoml::Transform's own constructors and its
// operator() overload set - see codegen/config.yml's own "transformations" module
// comment for the full explanation of why the generator can't do the constructors on
// its own (not the reason legacy-geo/custom_init.cpp's own near-identical
// hand-written registration cited - this generator has no problem
// discovering/emitting a move-constructor overload at all - but a different,
// newly-found one: sol2 itself doesn't correctly dispatch a bound function with an
// rvalue-reference parameter). The move constructor itself is simply never
// registered - Lua has no move-semantics concept to lose, and the copy constructor
// below gives a Lua script the same observable behavior either way.
//
// operator() itself needs its own hand-written escape hatch for an unrelated reason:
// grunk.codegen has no mapping from C++'s call operator to Lua's __call metamethod at
// all (BINARY_OPERATOR_META/UNARY_OPERATOR_META, generate.py's own module-level
// comment, only covers the small, fixed set of operators with a real Lua metamethod
// equivalent - +, -, *, /, ==, <, <= - operator() isn't among them, deliberately: a
// generic "any operator() overload becomes __call" rule would need arity-based
// overload discrimination the shared generator's own operator-handling code doesn't
// attempt). legacy-geo/custom_init.cpp hand-registered this same overload set for
// the same underlying reason (its own `reflect`-based generator had no
// operator()-to-callable mapping either) - this is a real, recurring generator gap,
// not something specific to this generator's own implementation, so not worth
// generalizing for four overloads used by exactly one class.
//
// Called from src/geoml_plugin.cpp, after register_transformations(ns) (which
// registers Transform itself, minus its constructors - config.yml's own
// `blacklist: [Transform::Transform]` - plus its member functions, unaffected).

#pragma once

#include <iostream>

#include <grunk/dynamic.hpp>

#include <geoml/transformations/Transform.h>

// Transform.h forward-declares `class Transformation;` and holds one by
// `std::unique_ptr<Transformation> pImpl` (a PIMPL) - Transform's own (implicitly
// generated) destructor needs Transformation complete wherever it's instantiated,
// which sol2's "value" ctor-mode registration below does (constructing/destroying
// Transform instances as part of its own usertype machinery) - confirmed the hard
// way ("invalid application of 'sizeof' to incomplete type 'geoml::Transformation'"),
// same completeness need as any other extra_includes entry elsewhere in this
// project, just triggered by a PIMPL's own destructor rather than a Handle(X)
// return or a forward-declared parameter.
#include <geoml/internal/geometry/Transformation.h>

// occt_sol_traits.hpp-equivalent (this plugin's own copy, src/geoml_sol_traits.hpp) -
// operator()'s Handle(Geom_Surface) overload below needs sol2's own
// unique_usertype_traits<opencascade::handle<T>> specialization in scope, same as any
// other Handle(X) crossing the sol2 boundary (see DESIGN.md's "Handle(X) cross-plugin
// sharing"). Geom_Surface.hxx: Handle(X) completeness, same reasoning as every other
// extra_includes entry in this project - Transform.h only forward-declares it.
#include "geoml_sol_traits.hpp"
#include <Geom_Surface.hxx>
#include <Geom_Curve.hxx>

#include <gp_GTrsf.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS.hxx>
#include <TopLoc_Location.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRep_Tool.hxx>

namespace geoml_plugin {

// geoml::Transform has no Handle(Geom_Curve) overload of its own (confirmed by
// inspection of geoml/transformations/Transform.h and geoml/geometry/
// Transformation.h/.cpp - only TopoDS_Shape/PNamedShape/gp_Pnt/Handle(Geom_Surface)/
// gp_Vec) - wing_loft.grr.yml (ported from legacy-geo's own wing.grr, whose
// geo::transform free function DID support curves, in a since-removed ancestor of
// this API) needs exactly this. Filled in here using the identical technique
// geoml's own Transformation::Transform(Handle(Geom_Surface)) already uses for the
// sibling 2D case (geoml/src/geometry/Transformation.cpp): wrap the curve in a
// topological shape (here, a TopoDS_Edge instead of a TopoDS_Face), transform that
// shape via the already-correct, already-public Transform::operator()(TopoDS_Shape)
// (internally Copy=Standard_True, so the source geometry is never mutated - verified
// directly, see scratchpad test_transform_curve.cpp), then extract the transformed
// curve back out. BRep_Tool::Curve returns the edge's own curve relative to a
// TopLoc_Location (an OCCT placement-sharing optimization, not baked into the
// geometry itself) - the final ->Transform(loc.Transformation()) bakes that location
// into the returned curve's own control points, exactly mirroring
// Transformation::Transform(Handle(Geom_Surface))'s own final
// `surfCopy->Transform(L.Transformation())` step. Without this last step the
// returned curve would silently NOT reflect the transform at all from Lua's own
// point of view (a Handle(Geom_Curve) pushed to Lua carries no Location).
inline opencascade::handle<Geom_Curve> transform_curve(geoml::Transform const& self, opencascade::handle<Geom_Curve> const& curve)
{
    TopoDS_Edge tmp_edge = BRepBuilderAPI_MakeEdge(curve);
    TopoDS_Edge transformed_edge = TopoDS::Edge(self(static_cast<TopoDS_Shape const&>(tmp_edge)));
    TopLoc_Location loc;
    Standard_Real first, last;
    opencascade::handle<Geom_Curve> result = BRep_Tool::Curve(transformed_edge, loc, first, last);
    // TEMP DEBUG (remove once the macOS-only as_vec failure is root-caused -
    // see DESIGN.md/the grunk-geoml CI issue this is chasing): confirm what
    // transform_curve() actually hands back to Lua before as_vec ever sees it.
    std::cerr << "[DEBUG transform_curve] input.IsNull()=" << curve.IsNull()
              << " result.IsNull()=" << result.IsNull()
              << " result.DynamicType="
              << (result.IsNull() ? "<null>" : result->DynamicType()->Name())
              << std::endl;
    result->Transform(loc.Transformation());
    return result;
}

} // namespace geoml_plugin

// TEMP DEBUG (remove once the macOS-only as_vec failure is root-caused): a
// hand-rolled copy of grunk's own with_std_vector<Handle<T>>() mechanism
// (usertype_proxy.hpp's convert_one/from_varargs - a per-element nested
// sol::protected_function identity-cast call made while iterating a raw
// sol::variadic_args), fully instrumented. grunk itself is a pinned, freshly-
// cloned dependency in CI, so it can't be patched in place here - this
// reproduces the exact same code path from geoml's own side instead, to see
// where the macOS-only divergence actually happens: per-value as_vec(curveN)
// already succeeds individually on both platforms, but the combined 3-arg
// as_vec(curve1, curve2, curve3) only fails on macOS - so the loop itself
// (iterating raw variadic_args while making nested Lua calls) is the prime
// suspect, not any single value's conversion.
inline void register_debug_as_vec3(grunk::plugin_namespace& ns)
{
    ns.register_function("debug_as_vec3", [](sol::variadic_args va) -> std::vector<opencascade::handle<Geom_Curve>> {
        lua_State* L = va.lua_state();
        sol::state_view lua(L);
        sol::protected_function cast_to_element = sol::make_object(
            lua, [](opencascade::handle<Geom_Curve> x) -> opencascade::handle<Geom_Curve> { return x; }
        );

        std::vector<opencascade::handle<Geom_Curve>> ret;
        int i = 0;
        for (auto const& v : va) {
            ++i;
            std::cerr << "[DEBUG debug_as_vec3] iter=" << i
                      << " top_before=" << lua_gettop(L)
                      << " get_type=" << static_cast<int>(v.get_type())
                      << std::endl;
            sol::protected_function_result res = cast_to_element(v);
            std::cerr << "[DEBUG debug_as_vec3] iter=" << i
                      << " top_after=" << lua_gettop(L)
                      << " valid=" << res.valid()
                      << std::endl;
            if (!res.valid()) {
                sol::error err = res;
                std::cerr << "[DEBUG debug_as_vec3] iter=" << i << " error=" << err.what() << std::endl;
                throw std::runtime_error("debug_as_vec3 failed at iter " + std::to_string(i));
            }
            ret.push_back(res.get<opencascade::handle<Geom_Curve>>());
        }
        return ret;
    });
}

inline void register_transform_extras(grunk::plugin_namespace& ns)
{
    register_debug_as_vec3(ns);

    ns.modify_type<geoml::Transform>("Transform")
        .add_constructors(
            []() { return geoml::Transform(); },
            [](const gp_GTrsf& ocMatrix) { return geoml::Transform(ocMatrix); },
            [](const gp_Trsf& trans) { return geoml::Transform(trans); },
            [](const gp_Vec& translation) { return geoml::Transform(translation); },
            [](const geoml::Transform& other) { return geoml::Transform(other); }
        )
        // operator() overload set - see this file's own top comment for why the
        // generator can't reach this. A Transform instance is called directly,
        // exactly like in C++ - `local moved_pnt = xf(pt)`. This used to need a
        // second, duplicate "apply" member function as a colon-call workaround
        // (`xf:apply(pt)`), since grunk's own decorated/parametric recipe
        // environment (create_decorated_environment, wrapping every value in a
        // Feature<object> for dependency tracking) never forwarded Lua's __call
        // metamethod on an arbitrary userdata through that wrapping - a real,
        // generic grunk-core/dynamic-layer gap, fixed upstream in grunk itself
        // (DynamicFeature now registers sol::meta_function::call generically, the
        // same make_dynamic_action pattern already used for +/-/*// etc. - see
        // grunk commit c0cc16d, and this repo's own DESIGN.md "Real recipe-API bug"
        // entries). Confirmed live against the current grunk pin: `xf(pt)` now
        // works correctly inside a real recipe's steps: block, not just a plain
        // script - see examples/wing_loft.grr.yml/load_carrier.grr.yml, both
        // updated to call xf(...) directly instead of xf:apply(...).
        // Order matters here, and is not just cosmetic: sol2's own unique-usertype-
        // aware checker (invoked for a by-value Handle(X) candidate, needed for
        // Handle(Derived)->Handle(Base) conversion - see DESIGN.md) crashes outright
        // (does not cleanly reject) when checked against an argument that is a
        // plain VALUE usertype (gp_Pnt/gp_Vec/TopoDS_Shape), not any kind of unique
        // usertype at all - confirmed directly (xf(a_gp_Vec) segfaulted with the
        // Handle(Geom_Curve) candidate listed before gp_Vec, a real, reproducible
        // sol2 dispatch bug, not a fluke). sol::overload tries candidates in
        // declaration order and stops at the first that matches without crashing,
        // so every plain-value-type candidate (TopoDS_Shape/gp_Pnt/gp_Vec) is listed
        // BEFORE both Handle(X) candidates (Handle(Geom_Surface)/Handle(Geom_Curve))
        // - this only protects calls whose argument matches one of the 3 plain
        // types; a genuinely ambiguous/unsupported argument would still risk this
        // same crash reaching the Handle(X) candidates, but that's true of any
        // Handle(X)-mixed overload set throughout this project (e.g. grunk-occt's
        // own generated BRepBuilderAPI_MakeEdge overload set has the identical
        // shape/ordering, apparently never hit by its own test suite).
        .add_member_function(sol::meta_function::call, sol::overload(
            [](geoml::Transform const& self, const TopoDS_Shape& shape) -> TopoDS_Shape { return self(shape); },
            [](geoml::Transform const& self, const gp_Pnt& point) -> gp_Pnt { return self(point); },
            [](geoml::Transform const& self, const gp_Vec& vec) -> gp_Vec { return self(vec); },
            [](geoml::Transform const& self, opencascade::handle<Geom_Curve> curve) -> opencascade::handle<Geom_Curve> { return geoml_plugin::transform_curve(self, curve); },
            [](geoml::Transform const& self, const opencascade::handle<Geom_Surface>& surf) -> opencascade::handle<Geom_Surface> { return self(surf); }
        ));
}
