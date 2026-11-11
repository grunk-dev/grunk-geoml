// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// Hand-written escape hatch, needed to faithfully port legacy-geo's own
// geo::internal::GetFace(shape, index)/GetEdge(shape, index) (see legacy-geo's own
// generated code and its consuming recipes, e.g. the old load_carrier/airplane_seats
// examples this plugin's own examples/ ports) - a plain 0-based indexed lookup into
// a shape's own *unique* TopAbs_FACE/TopAbs_EDGE sub-shapes. Neither geoml nor
// grunk-occt's own generator coverage has anything like this today:
// geoml::Shape::direct_subshapes() returns only a shape's *immediate* topological
// children (a Solid's own Shells, not a Solid's 12 Edges), not a recursive same-type
// walk, and grunk-occt's own src/topexp_extras.hpp only goes as far as
// first_face/first_edge (its own comment explains why: "TopExp_Explorer's own
// More()/Next()/Current() iterator pattern has no generator support").
//
// Deliberately built on TopExp::MapShapes / TopTools_IndexedMapOfShape, NOT a plain
// TopExp_Explorer - confirmed the hard way that a plain TopExp_Explorer does NOT
// deduplicate: a shared edge bordering two faces is visited once per ancestor
// context, so a box's own 12 real, unique edges appear as 24 raw explorer
// positions, silently breaking any "index N is out of range" check built on
// .More()/.Next() alone (an index past the true unique count would just land on a
// duplicate of an earlier edge instead of failing). TopExp::MapShapes builds a real
// index of *unique* sub-shapes (OCCT's own canonical, deterministic topological
// numbering for a given shape) with a genuine bounds-checked accessor
// (IndexedMapOfShape::FindKey/Extent) - this is almost certainly closer to what the
// legacy geo::internal::GetFace/GetEdge itself did too, given its own callers relied
// on stable, reproducible indices.
//
// Lua usage: geoml.nth_face(shape, 0), geoml.nth_edge(shape, 5), geoml.count_faces(shape),
// geoml.count_edges(shape) - 0-based, matching the legacy geo::internal::GetFace/
// GetEdge convention these examples are ported from (confirmed against the original
// recipes' own index values, which include a literal 0). NOTE: exact index-to-
// sub-shape correspondence is OCCT-version-specific topological numbering, not a
// portable constant - the *index values* used in a legacy recipe do not necessarily
// select the same edge/face against this OCCT version (7.9) that they did against
// whatever OCCT build the legacy recipe's own indices were originally found against
// (by the legacy recipe's own admission, "indices found by trial and error") - ported
// examples re-derive their own correct indices against this build, they don't just
// copy the old numbers.

#pragma once

#include <TopExp.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopoDS_CompSolid.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <TopoDS.hxx>
#include <TopTools_IndexedMapOfShape.hxx>

#include <grunk/dynamic.hpp>

#include <stdexcept>

namespace geoml_plugin {

inline TopTools_IndexedMapOfShape indexed_map_of(TopoDS_Shape const& shape, TopAbs_ShapeEnum kind)
{
    TopTools_IndexedMapOfShape map;
    TopExp::MapShapes(shape, kind, map);
    return map;
}

} // namespace geoml_plugin

// sol2's own base-class-aware usertype conversion (letting a derived TopoDS_*
// value satisfy a TopoDS_Shape const& parameter) only reliably applies to a
// by-value parameter, not a reference one - confirmed via a real CI run (passed
// on ubuntu-latest/GCC, failed on windows-latest/MSVC) hitting exactly this:
// geoml.count_faces(shell) with shell a TopoDS_Shell. Same restriction, same fix
// shape as codegen/hooks.py's own SHAPE_TOPODS_WIDEN_CLASS for geoml::Shape's
// generated constructor (see that comment for the full story) - here applied by
// hand since these four functions are hand-written, not generated. One exact-
// match overload candidate per concrete TopoDS_Shape subtype, all delegating to
// the same real TopoDS_Shape-taking implementation via a genuine, always-safe
// C++ implicit upcast. Two macro variants (not a variadic/generic one) because
// each sol::overload candidate needs a fixed, non-template operator() - sol2
// inspects a candidate's own function_traits to build its stack-arg checkers,
// so a generic lambda (`auto&&... args`) does not work here (confirmed
// directly: register_function rejected it, "no matching member function").
// extra_params/extra_args: sol::overload candidates must each have a fixed,
// non-template operator() (sol2 inspects each candidate's own function_traits
// to build its stack-arg checkers) - a generic/templated lambda (`auto&&...`)
// does not work here, confirmed directly (register_function rejected it
// outright, "no matching member function"). extra_params/extra_args spell out
// the one non-shape parameter nth_face/nth_edge need (nothing for
// count_faces/count_edges) as literal macro text instead.
#define GEOML_PLUGIN_TOPODS_SHAPE_OVERLOAD_1(fn) \
    sol::overload( \
        [fn](TopoDS_Shape const& shape) { return fn(shape); }, \
        [fn](TopoDS_CompSolid const& shape) { return fn(shape); }, \
        [fn](TopoDS_Compound const& shape) { return fn(shape); }, \
        [fn](TopoDS_Edge const& shape) { return fn(shape); }, \
        [fn](TopoDS_Face const& shape) { return fn(shape); }, \
        [fn](TopoDS_Shell const& shape) { return fn(shape); }, \
        [fn](TopoDS_Solid const& shape) { return fn(shape); }, \
        [fn](TopoDS_Vertex const& shape) { return fn(shape); }, \
        [fn](TopoDS_Wire const& shape) { return fn(shape); } \
    )
#define GEOML_PLUGIN_TOPODS_SHAPE_OVERLOAD_2(fn) \
    sol::overload( \
        [fn](TopoDS_Shape const& shape, int index) { return fn(shape, index); }, \
        [fn](TopoDS_CompSolid const& shape, int index) { return fn(shape, index); }, \
        [fn](TopoDS_Compound const& shape, int index) { return fn(shape, index); }, \
        [fn](TopoDS_Edge const& shape, int index) { return fn(shape, index); }, \
        [fn](TopoDS_Face const& shape, int index) { return fn(shape, index); }, \
        [fn](TopoDS_Shell const& shape, int index) { return fn(shape, index); }, \
        [fn](TopoDS_Solid const& shape, int index) { return fn(shape, index); }, \
        [fn](TopoDS_Vertex const& shape, int index) { return fn(shape, index); }, \
        [fn](TopoDS_Wire const& shape, int index) { return fn(shape, index); } \
    )

inline void register_topexp_extras(grunk::plugin_namespace& ns)
{
    auto nth_face = [](TopoDS_Shape const& shape, int index) -> TopoDS_Face {
        auto map = geoml_plugin::indexed_map_of(shape, TopAbs_FACE);
        if (index < 0 || index >= map.Extent()) {
            throw std::runtime_error(
                "nth_face: index out of range (0-based, shape has " + std::to_string(map.Extent()) + " unique faces)"
            );
        }
        return TopoDS::Face(map.FindKey(index + 1)); // OCCT's own map is 1-based
    };
    auto nth_edge = [](TopoDS_Shape const& shape, int index) -> TopoDS_Edge {
        auto map = geoml_plugin::indexed_map_of(shape, TopAbs_EDGE);
        if (index < 0 || index >= map.Extent()) {
            throw std::runtime_error(
                "nth_edge: index out of range (0-based, shape has " + std::to_string(map.Extent()) + " unique edges)"
            );
        }
        return TopoDS::Edge(map.FindKey(index + 1)); // OCCT's own map is 1-based
    };
    auto count_faces = [](TopoDS_Shape const& shape) -> int {
        return geoml_plugin::indexed_map_of(shape, TopAbs_FACE).Extent();
    };
    auto count_edges = [](TopoDS_Shape const& shape) -> int {
        return geoml_plugin::indexed_map_of(shape, TopAbs_EDGE).Extent();
    };

    ns.register_function("nth_face", GEOML_PLUGIN_TOPODS_SHAPE_OVERLOAD_2(nth_face));
    ns.register_function("nth_edge", GEOML_PLUGIN_TOPODS_SHAPE_OVERLOAD_2(nth_edge));
    ns.register_function("count_faces", GEOML_PLUGIN_TOPODS_SHAPE_OVERLOAD_1(count_faces));
    ns.register_function("count_edges", GEOML_PLUGIN_TOPODS_SHAPE_OVERLOAD_1(count_edges));

#undef GEOML_PLUGIN_TOPODS_SHAPE_OVERLOAD_1
#undef GEOML_PLUGIN_TOPODS_SHAPE_OVERLOAD_2
}
