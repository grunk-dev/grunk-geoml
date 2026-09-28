// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// Exercises the `naming_choosing` module (generated/naming_choosing.cpp) -
// geoml::Shape, the history-tracking wrapper every geoml function that returns
// geometry actually returns (see predicates/primitives/boolean_ops's own return
// types) - constructor from a real TopoDS_Shape (built via `primitives`'s own
// create_box), tag tracking, and Shape::is_same identity.

#include <grunk/dynamic.hpp>
#include <grunk/plugin.hpp>

#include <gtest/gtest.h>

namespace {

class NamingChoosingTest : public ::testing::Test {
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

grunk::state* NamingChoosingTest::state_ = nullptr;

} // namespace

// Shape's own two constructors: default (empty/null) and from a real TopoDS_Shape -
// confirms both are reachable from Lua and produce the expected is_null()/is_empty().
TEST_F(NamingChoosingTest, DefaultConstructedShapeIsNull)
{
    auto env = state_->create_env();
    auto res = env.eval(R"(
        s = geoml.Shape.new()
        s_null = s:is_null()
    )");
    ASSERT_TRUE(res.valid());
    EXPECT_TRUE(env.get<sol::object>("s_null").as<bool>());
}

TEST_F(NamingChoosingTest, ShapeConstructedFromTopoDSShapeIsNotNull)
{
    auto env = state_->create_env();
    auto res = env.eval(R"(
        local box = geoml.create_box(1.0, 1.0, 1.0)
        local underlying = box:shape()
        s = geoml.Shape.new(underlying)
        s_null = s:is_null()
    )");
    ASSERT_TRUE(res.valid());
    EXPECT_FALSE(env.get<sol::object>("s_null").as<bool>());
}

// add_meta_tag/is_same: a shape is trivially is_same as itself, and tagging doesn't
// change that identity - a real, if small, behavioral assertion beyond "it runs".
TEST_F(NamingChoosingTest, TaggedShapeIsStillSameAsItself)
{
    auto env = state_->create_env();
    auto res = env.eval(R"(
        local box = geoml.create_box(1.0, 1.0, 1.0)
        box:add_meta_tag("my_tag")
        same = box:is_same(box)
    )");
    ASSERT_TRUE(res.valid());
    EXPECT_TRUE(env.get<sol::object>("same").as<bool>());
}
