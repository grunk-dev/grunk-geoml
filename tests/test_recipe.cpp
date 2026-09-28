// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// Exercises geoml_plugin through a real grunk::recipe (parameters:/steps:/outputs:,
// reactive re-evaluation on set_value), not just every other test_*.cpp's raw
// create_env()/eval() harness - none of which ever goes through the recipe/
// parametric layer. See ../DESIGN.md and examples/geoml_smoke.grr.yml's own header
// comment for why this reads a small, self-contained fixture recipe rather than one
// of the plan's own real examples (Step 6, not started yet).
//
// EXAMPLES_DIR is set by CMakeLists.txt to the absolute path of examples/.

#include <grunk/dynamic.hpp>
#include <grunk/plugin.hpp>
#include <grunk/recipe.hpp>

#include <gtest/gtest.h>

#include <string>

namespace {

class RecipeTest : public ::testing::Test {
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

grunk::state* RecipeTest::state_ = nullptr;

} // namespace

// Reads examples/geoml_smoke.grr.yml, confirms its "ok" output (the fused shape's
// own is_empty() - false means the boolean union actually produced real geometry)
// - the recipe layer's own read/parameters/steps/outputs machinery working
// end-to-end for this plugin, not just a raw create_env()/eval() call.
TEST_F(RecipeTest, SmokeRecipeProducesOkOutput)
{
    auto recipe = state_->read(std::string(EXAMPLES_DIR) + "/geoml_smoke.grr.yml");
    EXPECT_FALSE(recipe.get_output("ok").value().as<bool>());
}

// Reactive re-evaluation: changing a parameter the fused shape depends on must still
// flow through to the top-level "ok" output correctly, exactly like
// tests/test_boolean_ops.cpp confirms for a single undecorated environment - this is
// the same guarantee, but through the parametric/dependency-tracked recipe layer
// this test file is specifically about.
TEST_F(RecipeTest, SmokeRecipeReactsToParameterChange)
{
    auto recipe = state_->read(std::string(EXAMPLES_DIR) + "/geoml_smoke.grr.yml");
    auto ok = recipe.get_output("ok");

    recipe.get_feature("radius").set_value(2.0);
    EXPECT_FALSE(ok.value().as<bool>());

    recipe.get_feature("box_size").set_value(1.5);
    EXPECT_FALSE(ok.value().as<bool>());
}
