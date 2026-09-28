// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// Exercises the `geom_topo` module (generated/geom_topo.cpp) - CurveToEdge/
// EdgeToCurve, a Handle(Geom_Curve) <-> TopoDS_Edge round trip. The Handle(Geom_Line)
// -> Handle(Geom_Curve) upcast happens in C++ here, before the value ever crosses
// into Lua - sol2 3.5.0's own Handle(Derived)->Handle(Base) argument checking is
// known broken at the function-parameter dispatch layer (see grunk-occt's own
// DESIGN.md, "sol2 3.5.0 limitation"), so passing a Handle(Geom_Line) directly to a
// Lua call expecting Handle(Geom_Curve) wouldn't work even with occt_plugin.so's own
// add_bases<...>() relationship loaded (which this test suite doesn't load anyway -
// see tests/test_surfaces.cpp's own comment on why). Doing the upcast in plain C++
// (opencascade::handle<T>'s own converting constructor) sidesteps this entirely.
//
// geoml_sol_traits.hpp's own sol::unique_usertype_traits<opencascade::handle<T>>
// specialization must be in scope in *this* translation unit too, same as any
// generated module that constructs a Handle(X) - without it, sol2 has no reason to
// treat Handle(Geom_Curve) as unique-ownership/pointer-like at all, and instead
// tries to push it as an ordinary *value* type needing full comparison-operator
// automagic, which fails to compile the same "no match for operator<" way
// DESIGN.md's "Handle(X) cross-plugin sharing" section describes for a genuinely
// different reason (there, X itself was never registered anywhere; here, the trait
// specialization making Handle(X) push correctly at all was simply missing from this
// TU) - found the hard way while writing this test, not a new sol2/generator finding.

#include <grunk/dynamic.hpp>
#include <grunk/plugin.hpp>

#include <gtest/gtest.h>

#include "../src/geoml_sol_traits.hpp"

#include <Geom_Curve.hxx>
#include <Geom_Line.hxx>
#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

namespace {

class GeomTopoTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        state_ = new grunk::state();
        grunk::plugin::load_native(*state_, GEOML_PLUGIN_SO_PATH);
    }

    static void TearDownTestSuite()
    {
        delete state_;
        state_ = nullptr;
    }

    static grunk::state* state_;
};

grunk::state* GeomTopoTest::state_ = nullptr;

} // namespace

TEST_F(GeomTopoTest, CurveToEdgeAndBackRoundTrips)
{
    auto env = state_->create_env();

    Handle(Geom_Line) line = new Geom_Line(gp_Ax1(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(1.0, 0.0, 0.0)));
    Handle(Geom_Curve) curve = line; // native C++ upcast, not a sol2/Lua-side one

    env["curve"] = curve;
    auto res = env.eval(R"(
        edge = geoml.CurveToEdge(curve)
        edge_ok = edge ~= nil
        curve_back = geoml.EdgeToCurve(edge)
        curve_back_ok = curve_back ~= nil
    )");
    ASSERT_TRUE(res.valid());
    EXPECT_TRUE(env.get<sol::object>("edge_ok").as<bool>());
    EXPECT_TRUE(env.get<sol::object>("curve_back_ok").as<bool>());
}
