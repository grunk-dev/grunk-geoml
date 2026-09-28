// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// Hand-written escape hatch for geoml::Transform's own constructors - see
// codegen/config.yml's own "transformations" module comment for the full
// explanation of why the generator can't do this on its own (not the reason
// legacy-geo/custom_init.cpp's own near-identical hand-written registration cited -
// this generator has no problem discovering/emitting a move-constructor overload at
// all - but a different, newly-found one: sol2 itself doesn't correctly dispatch a
// bound function with an rvalue-reference parameter). The move constructor itself is
// simply never registered - Lua has no move-semantics concept to lose, and the copy
// constructor below gives a Lua script the same observable behavior either way.
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

#include <gp_GTrsf.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

inline void register_transform_extras(grunk::plugin_namespace& ns)
{
    ns.modify_type<geoml::Transform>("Transform")
        .add_constructors(
            []() { return geoml::Transform(); },
            [](const gp_GTrsf& ocMatrix) { return geoml::Transform(ocMatrix); },
            [](const gp_Trsf& trans) { return geoml::Transform(trans); },
            [](const gp_Vec& translation) { return geoml::Transform(translation); },
            [](const geoml::Transform& other) { return geoml::Transform(other); }
        );
}
