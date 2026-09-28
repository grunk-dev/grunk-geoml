// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// Hand-written escape hatch, same shape and same underlying reason as grunk-occt's
// own src/vector_extras.hpp (real_vec/int_vec) and grunk's own generic
// with_std_vector() (occt.gp_Pnt.as_vec(...)): a std::vector<T> function parameter
// taken by reference (every std::vector<T> parameter this generator ever emits is -
// see Param.kind()'s own "object"/"passthrough" built-ins) does not accept a plain
// Lua table directly - sol2 has no automatic table->std::vector<T> conversion for a
// *reference* parameter, confirmed repeatedly throughout this project (grunk-occt's
// own real_vec/int_vec, this plugin's own predicate/nurbs_curve examples). A
// std::vector<Handle(X)> - needed here for interpolate_curves/
// interpolate_curve_network's own curve-list parameters (geoml/surfaces/surfaces.h)
// - is exactly this shape, and grunk's own with_std_vector() only ever builds a
// vector of a *value* usertype (a registered gp_Pnt, say), not a Handle(X) smart
// pointer - so this needs its own small, explicit helper rather than an existing
// mechanism reused as-is.
//
// A single Handle(Derived)->Handle(Base) *argument* (e.g. a
// Handle(Geom_BSplineCurve) passed where a lone Handle(Geom_Curve) parameter is
// expected) already works fine today - confirmed empirically via geoml.CurveToEdge
// - so each variadic argument below converts correctly regardless of which concrete
// Geom_Curve subtype produced it (Geom_BSplineCurve, Geom_TrimmedCurve, ...).
//
// Lua usage: geoml.curve_vec(curve1, curve2, curve3) - compose freely with
// geoml.interpolate_curves(...)/geoml.interpolate_curve_network(...).

#pragma once

#include <grunk/dynamic.hpp>

#include <Geom_Curve.hxx>

#include "geoml_sol_traits.hpp"

#include <vector>

inline void register_curve_vec_extras(grunk::plugin_namespace& ns)
{
    ns.register_function("curve_vec", [](sol::variadic_args va) {
        std::vector<opencascade::handle<Geom_Curve>> v;
        v.reserve(va.size());
        for (auto const& a : va) {
            v.push_back(a.get<opencascade::handle<Geom_Curve>>());
        }
        return v;
    });
}
