// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// Exercises the `curves` module (generated/curves.cpp) - nurbs_curve/
// interpolate_points_to_b_spline_curve, both std::vector<gp_Pnt>-taking. sol2 has no
// automatic Lua-table -> std::vector<Usertype> conversion for a by-reference
// parameter (a real, pre-existing sol2 limitation, not specific to this plugin - see
// DESIGN.md's "the std::vector<gp_Pnt> binding issue" and grunk-occt's own
// tests/test_geom.cpp comment for the identical finding there), so the vector has to
// already be a real C++ object when it crosses into Lua - built directly here rather
// than via grunk-occt's own occt.gp_Pnt.as_vec(...)/occt.real_vec(...) helpers (this
// test suite never loads occt_plugin.so at all).
//
// Both functions return Handle(Geom_BSplineCurve) - genuinely unregistered in this
// process (occt_plugin.so, which alone would register Geom_BSplineCurve/Geom_Curve
// and their methods, is never loaded here). Per Step 0's own findings (DESIGN.md's
// "Handle(X) cross-plugin sharing"), an opaque, never-registered Handle(X) can be
// held/passed/returned correctly, but *calling a method* on it needs some loaded
// plugin to have actually registered that type - these tests only confirm a real,
// non-nil curve came back, not its geometric properties (grunk-occt's own
// tests/test_geom.cpp already covers the identical B-spline math, with occt_plugin
// loaded, so re-proving Value()/Degree() here would need this plugin's own tests to
// take a dependency on grunk-occt it deliberately doesn't have).

#include <grunk/dynamic.hpp>
#include <grunk/plugin.hpp>

#include <gtest/gtest.h>

#include <Geom_BSplineCurve.hxx>
#include <gp_Pnt.hxx>

#include <vector>

namespace {

class CurvesTest : public ::testing::Test {
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

grunk::state* CurvesTest::state_ = nullptr;

} // namespace

TEST_F(CurvesTest, InterpolatePointsToBSplineCurveConstructsSuccessfully)
{
    auto env = state_->create_env();
    env["pts"] = std::vector<gp_Pnt>{
        gp_Pnt(0.0, 0.0, 0.0),
        gp_Pnt(1.0, 1.0, 0.0),
        gp_Pnt(2.0, 0.0, 0.0),
    };

    auto res = env.eval(R"(
        curve = geoml.interpolate_points_to_b_spline_curve(pts)
        curve_ok = curve ~= nil
        curve_type = type(curve)
    )");
    ASSERT_TRUE(res.valid());
    EXPECT_TRUE(env.get<sol::object>("curve_ok").as<bool>());
    EXPECT_EQ(env.get<sol::object>("curve_type").as<std::string>(), "userdata");
}

// Same 5-pole, degree-2 explicit-knots-and-multiplicities case grunk-occt's own
// test_geom.cpp uses for Geom_BSplineCurve directly (mults sum to
// n_poles + degree + 1 = 5 + 2 + 1 = 8, a real B-spline validity constraint, not an
// arbitrary choice) - here built through geoml's own higher-level nurbs_curve
// instead of raw OCCT construction. Only confirms construction succeeds with a
// valid knot vector (see this file's own top comment for why Degree()/Value()
// aren't callable here) - grunk-occt's own identical test already confirms the
// closed-form clamping property itself, with occt_plugin loaded.
TEST_F(CurvesTest, NurbsCurveWithExplicitKnotsAndMultiplicitiesConstructsSuccessfully)
{
    auto env = state_->create_env();
    env["pts"] = std::vector<gp_Pnt>{
        gp_Pnt(0.0, 0.0, 0.0),
        gp_Pnt(0.0, 1.0, 2.0),
        gp_Pnt(0.0, 2.0, -1.0),
        gp_Pnt(0.0, 0.0, 3.0),
        gp_Pnt(0.0, 4.0, 0.0),
    };
    // Standard_Real, not double - nurbs_curve's own generated binding takes
    // std::vector<Standard_Real> (== std::vector<double> for the stock build,
    // std::vector<Standard_Adouble> for the AD one) for its weights/knots
    // parameters, and sol2 has no automatic std::vector<double>-to-
    // std::vector<Standard_Adouble> conversion - this needs to already be the
    // right vector element type when it crosses into Lua, same reasoning as
    // this file's own top comment about std::vector<gp_Pnt>.
    env["weights"] = std::vector<Standard_Real>{1.0, 1.0, 1.0, 1.0, 1.0};
    env["knots"] = std::vector<Standard_Real>{0.0, 1.0, 2.0, 3.0};
    env["mults"] = std::vector<int>{3, 1, 1, 3};

    auto res = env.eval(R"(
        curve = geoml.nurbs_curve(pts, weights, knots, mults, 2)
        curve_ok = curve ~= nil
    )");
    ASSERT_TRUE(res.valid());
    EXPECT_TRUE(env.get<sol::object>("curve_ok").as<bool>());
}
