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

#include <grunk/dynamic.hpp>
#include <grunk/plugin.hpp>

#include <gtest/gtest.h>

#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

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
    EXPECT_DOUBLE_EQ(p1.X(), 1.0);
    EXPECT_DOUBLE_EQ(p1.Y(), 2.0);
    EXPECT_DOUBLE_EQ(p1.Z(), 3.0);
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
    EXPECT_DOUBLE_EQ(v1.X(), 1.0);
    EXPECT_DOUBLE_EQ(v1.Y(), 0.0);
    EXPECT_DOUBLE_EQ(v1.Z(), 0.0);
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
    EXPECT_DOUBLE_EQ(p1.X(), 5.0);
}
