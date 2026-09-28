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

#include <gp_GTrsf.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include <TopoDS_Shape.hxx>

inline void register_transform_extras(grunk::plugin_namespace& ns)
{
    ns.modify_type<geoml::Transform>("Transform")
        .add_constructors(
            []() { return geoml::Transform(); },
            [](const gp_GTrsf& ocMatrix) { return geoml::Transform(ocMatrix); },
            [](const gp_Trsf& trans) { return geoml::Transform(trans); },
            [](const gp_Vec& translation) { return geoml::Transform(translation); },
            [](const geoml::Transform& other) { return geoml::Transform(other); }
        )
        // operator() overload set - see this file's own top comment for why the
        // generator can't reach this. Lua usage: `local moved_shape = xf(shape)`,
        // `local moved_pnt = xf(pt)`, etc. - a Transform instance is called directly,
        // exactly like in C++.
        .add_member_function(sol::meta_function::call, sol::overload(
            [](geoml::Transform const& self, const TopoDS_Shape& shape) -> TopoDS_Shape { return self(shape); },
            [](geoml::Transform const& self, const gp_Pnt& point) -> gp_Pnt { return self(point); },
            [](geoml::Transform const& self, const opencascade::handle<Geom_Surface>& surf) -> opencascade::handle<Geom_Surface> { return self(surf); },
            [](geoml::Transform const& self, const gp_Vec& vec) -> gp_Vec { return self(vec); }
        ));
}
