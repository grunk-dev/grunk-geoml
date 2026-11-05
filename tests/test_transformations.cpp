// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// Exercises the `transformations` module (generated/transformations.cpp) plus
// src/transform_extras.hpp's own hand-written Transform constructor + operator()
// escape hatch (see that file's own comment for why both need one). Confirmed live
// during development (see DESIGN.md), turned into a permanent regression test here:
// a translation built from a gp_Vec, applied to a gp_Pnt (moves it) and to a gp_Vec
// (a free vector - correctly unaffected by a pure translation, a real geometric
// property, not an arbitrary assertion).
//
// TranslationLeavesAFreeVectorUnaffected below also doubles as a real regression
// test for a genuine sol2 crash found while adding the Handle(Geom_Curve) overload
// (wing_loft.grr.yml's own real re-port need - see DESIGN.md): calling xf(a_gp_Vec)
// against an overload set that lists a by-value Handle(X) candidate BEFORE the
// gp_Vec one segfaulted outright (sol2's own unique-usertype-aware checker does not
// cleanly reject a plain-value-usertype argument) - fixed by ordering every plain
// value-type candidate ahead of both Handle(X) ones (see transform_extras.hpp's own
// comment on the overload set).

#include <grunk/dynamic.hpp>
#include <grunk/plugin.hpp>

#include <gtest/gtest.h>

#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>
#include <Geom_Curve.hxx>

#include "../src/geoml_ad_support.hpp"

// This test file's own .as<opencascade::handle<Geom_Curve>>() call below (to
// inspect the transformed curve via plain C++ instead of Lua, see
// TranslationAppliesToAnInterpolatedCurve's own comment) instantiates sol2's
// unique-usertype machinery for opencascade::handle<Geom_Curve> INSIDE THIS
// translation unit - which is part of the runUnitTests EXECUTABLE, not
// geoml_plugin.so. Without src/geoml_sol_traits.hpp's own rebind_actual_type/
// SOL_BASE_CLASSES visible right here too, that instantiation happens with the
// stock, uninformed sol2 defaults - and since ELF symbol resolution gives the
// main executable's own global symbols priority over a dlopen'd shared library's
// weak symbols for the same template instantiation, an uninformed version
// compiled into runUnitTests itself can silently override geoml_plugin.so's own,
// correctly-informed one for the rest of the *whole process* (confirmed the hard
// way: omitting this include here caused a segfault, but only when this test ran
// as part of the full runUnitTests binary - see this file's own comment on
// TranslationLeavesAFreeVectorUnaffected for the related, but distinct, sol2
// dispatch crash this project already worked around once before). Same rule as
// everywhere else in this project: whichever translation unit touches a
// Handle(X) unique-usertype conversion needs its own copy of these declarations.
#include "../src/geoml_sol_traits.hpp"

namespace {

class TransformationsTest : public ::testing::Test {
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

grunk::state* TransformationsTest::state_ = nullptr;

} // namespace

TEST_F(TransformationsTest, TranslationMovesAPointByTheTranslationVector)
{
    auto env = state_->create_env();
    env["v"] = gp_Vec(1.0, 2.0, 3.0);
    env["p0"] = gp_Pnt(0.0, 0.0, 0.0);

    auto res = env.eval(R"(
        local xf = geoml.Transform.new(v)
        p1 = xf(p0)
    )");
    ASSERT_TRUE(res.valid());

    gp_Pnt p1 = env.get<sol::object>("p1").as<gp_Pnt>();
    EXPECT_DOUBLE_EQ(geoml_plugin::to_double(p1.X()), 1.0);
    EXPECT_DOUBLE_EQ(geoml_plugin::to_double(p1.Y()), 2.0);
    EXPECT_DOUBLE_EQ(geoml_plugin::to_double(p1.Z()), 3.0);
}

TEST_F(TransformationsTest, TranslationLeavesAFreeVectorUnaffected)
{
    auto env = state_->create_env();
    env["v"] = gp_Vec(1.0, 2.0, 3.0);
    env["v0"] = gp_Vec(1.0, 0.0, 0.0);

    auto res = env.eval(R"(
        local xf = geoml.Transform.new(v)
        v1 = xf(v0)
    )");
    ASSERT_TRUE(res.valid());

    gp_Vec v1 = env.get<sol::object>("v1").as<gp_Vec>();
    EXPECT_DOUBLE_EQ(geoml_plugin::to_double(v1.X()), 1.0);
    EXPECT_DOUBLE_EQ(geoml_plugin::to_double(v1.Y()), 0.0);
    EXPECT_DOUBLE_EQ(geoml_plugin::to_double(v1.Z()), 0.0);
}

// pre_multiply/copy constructor - ordinary generated coverage (no escape hatch
// needed), included here so this file's own scope is "everything about Transform",
// not just the hand-written half.
TEST_F(TransformationsTest, CopyConstructedTransformBehavesTheSame)
{
    auto env = state_->create_env();
    env["v"] = gp_Vec(5.0, 0.0, 0.0);
    env["p0"] = gp_Pnt(0.0, 0.0, 0.0);

    auto res = env.eval(R"(
        local xf = geoml.Transform.new(v)
        local xf_copy = geoml.Transform.new(xf)
        p1 = xf_copy(p0)
    )");
    ASSERT_TRUE(res.valid());

    gp_Pnt p1 = env.get<sol::object>("p1").as<gp_Pnt>();
    EXPECT_DOUBLE_EQ(geoml_plugin::to_double(p1.X()), 5.0);
}

// wing_loft.grr.yml's own real re-port need (see DESIGN.md): geoml::Transform has
// no Handle(Geom_Curve) overload of its own (only TopoDS_Shape/gp_Pnt/Handle(Geom_
// Surface)/gp_Vec) - transform_extras.hpp fills this in using the same "wrap in a
// topological shape, transform that, extract the result back out" technique
// geoml's own Transformation::Transform(Handle(Geom_Surface)) already uses for the
// sibling 2D case. A translation applied to a real interpolated curve here (the
// general-affine-matrix/non-uniform-scale case this overload exists for in the
// first place is exercised for real by examples/wing_loft.grr.yml itself, via
// occt.gp_GTrsf - not duplicated here, since gp_GTrsf isn't registered as a
// usertype by geoml_plugin.so on its own, only by occt_plugin.so, which this test
// suite deliberately never loads, see SetUpTestSuite's own comment) - the original
// curve must also stay unmutated (Copy=Standard_True inside geoml::Transformation::
// Transform(TopoDS_Shape) makes this the caller's responsibility to verify, not
// just assume).
TEST_F(TransformationsTest, TranslationAppliesToAnInterpolatedCurve)
{
    auto env = state_->create_env();
    env["pts"] = std::vector<gp_Pnt>{gp_Pnt(0.0, 0.0, 0.0), gp_Pnt(1.0, 0.0, 0.0)};
    env["v"] = gp_Vec(0.0, 0.0, 50.0);

    // Geom_Curve/Geom_BSplineCurve are never registered as usertypes in
    // geoml_plugin.so on their own (occt_plugin.so's own job), so the transformed
    // curve's own :Value()/:FirstParameter() aren't callable from Lua here -
    // extract the Handle(Geom_Curve) back into C++ and call OCCT's own real
    // methods directly instead, same pattern every other round-trip check in this
    // test suite already uses.
    auto res = env.eval(R"(
        curve = geoml.interpolate_points_to_b_spline_curve(pts)
        xf = geoml.Transform.new(v)
        transformed = xf(curve)
    )");
    ASSERT_TRUE(res.valid());

    auto orig_curve = env.get<sol::object>("curve").as<opencascade::handle<Geom_Curve>>();
    auto transformed_curve = env.get<sol::object>("transformed").as<opencascade::handle<Geom_Curve>>();

    gp_Pnt p_first = transformed_curve->Value(transformed_curve->FirstParameter());
    gp_Pnt p_last = transformed_curve->Value(transformed_curve->LastParameter());
    gp_Pnt orig_first = orig_curve->Value(orig_curve->FirstParameter());

    EXPECT_DOUBLE_EQ(geoml_plugin::to_double(p_first.X()), 0.0);
    EXPECT_DOUBLE_EQ(geoml_plugin::to_double(p_first.Z()), 50.0);
    EXPECT_DOUBLE_EQ(geoml_plugin::to_double(p_last.X()), 1.0);
    EXPECT_DOUBLE_EQ(geoml_plugin::to_double(p_last.Z()), 50.0);
    // The original curve must be unaffected by the transform.
    EXPECT_DOUBLE_EQ(geoml_plugin::to_double(orig_first.X()), 0.0);
    EXPECT_DOUBLE_EQ(geoml_plugin::to_double(orig_first.Z()), 0.0);
}
