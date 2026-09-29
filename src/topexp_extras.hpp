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
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
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

inline void register_topexp_extras(grunk::plugin_namespace& ns)
{
    ns.register_function(
        "nth_face",
        [](TopoDS_Shape const& shape, int index) -> TopoDS_Face {
            auto map = geoml_plugin::indexed_map_of(shape, TopAbs_FACE);
            if (index < 0 || index >= map.Extent()) {
                throw std::runtime_error(
                    "nth_face: index out of range (0-based, shape has " + std::to_string(map.Extent()) + " unique faces)"
                );
            }
            return TopoDS::Face(map.FindKey(index + 1)); // OCCT's own map is 1-based
        }
    );
    ns.register_function(
        "nth_edge",
        [](TopoDS_Shape const& shape, int index) -> TopoDS_Edge {
            auto map = geoml_plugin::indexed_map_of(shape, TopAbs_EDGE);
            if (index < 0 || index >= map.Extent()) {
                throw std::runtime_error(
                    "nth_edge: index out of range (0-based, shape has " + std::to_string(map.Extent()) + " unique edges)"
                );
            }
            return TopoDS::Edge(map.FindKey(index + 1)); // OCCT's own map is 1-based
        }
    );
    ns.register_function("count_faces", [](TopoDS_Shape const& shape) -> int {
        return geoml_plugin::indexed_map_of(shape, TopAbs_FACE).Extent();
    });
    ns.register_function("count_edges", [](TopoDS_Shape const& shape) -> int {
        return geoml_plugin::indexed_map_of(shape, TopAbs_EDGE).Extent();
    });
}
