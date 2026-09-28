// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// Exercises the `boolean_ops` module (generated/boolean_ops.cpp) -
// boolean_union/boolean_subtract, built directly on `primitives`'s own
// create_box/create_cylinder (see tests/test_primitives.cpp) - the smallest
// meaningful cross-module composition this plugin's own coverage supports.

#include <grunk/dynamic.hpp>
#include <grunk/plugin.hpp>

#include <gtest/gtest.h>

namespace {

class BooleanOpsTest : public ::testing::Test {
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

grunk::state* BooleanOpsTest::state_ = nullptr;

} // namespace

TEST_F(BooleanOpsTest, UnionOfTwoBoxesIsNonEmpty)
{
    auto env = state_->create_env();
    auto res = env.eval(R"(
        local a = geoml.create_box(2.0, 2.0, 2.0)
        local b = geoml.create_box(1.0, 1.0, 1.0)
        fused = geoml.boolean_union(a, b)
        fused_empty = fused:is_empty()
    )");
    ASSERT_TRUE(res.valid());
    EXPECT_FALSE(env.get<sol::object>("fused_empty").as<bool>());
}

TEST_F(BooleanOpsTest, SubtractOfTwoBoxesIsNonEmpty)
{
    auto env = state_->create_env();
    auto res = env.eval(R"(
        local a = geoml.create_box(2.0, 2.0, 2.0)
        local b = geoml.create_box(1.0, 1.0, 1.0)
        cut = geoml.boolean_subtract(a, b)
        cut_empty = cut:is_empty()
    )");
    ASSERT_TRUE(res.valid());
    EXPECT_FALSE(env.get<sol::object>("cut_empty").as<bool>());
}
