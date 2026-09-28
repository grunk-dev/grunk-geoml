// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// Exercises the `primitives` module (generated/primitives.cpp) - create_cylinder/
// create_box, the two simplest real geoml functions in this plugin's whole coverage
// (plain double parameters, no OCCT value construction needed at all) - deliberately
// the first real module test written, since it needs nothing from any other module
// or from occt_plugin.so (this plugin's own test suite never loads occt_plugin.so at
// all - grunk-geoml has no build-time dependency on grunk-occt, and doesn't want
// one for its own tests either; see naming_choosing/predicates/transformations/
// curves/surfaces/geom_topo's own test files for how OCCT values (gp_Pnt, Handle(X),
// ...) get pushed into Lua directly from C++ instead of via occt.*.new(...) calls).

#include <grunk/dynamic.hpp>
#include <grunk/plugin.hpp>

#include <gtest/gtest.h>

#include <geoml/naming_choosing/Shape.h>

namespace {

class PrimitivesTest : public ::testing::Test {
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

grunk::state* PrimitivesTest::state_ = nullptr;

} // namespace

TEST_F(PrimitivesTest, CreateCylinderProducesNonEmptyShape)
{
    auto env = state_->create_env();
    auto res = env.eval(R"(
        cyl = geoml.create_cylinder(5.0, 10.0)
        cyl_empty = cyl:is_empty()
        cyl_null = cyl:is_null()
    )");
    ASSERT_TRUE(res.valid());
    EXPECT_FALSE(env.get<sol::object>("cyl_empty").as<bool>());
    EXPECT_FALSE(env.get<sol::object>("cyl_null").as<bool>());
}

TEST_F(PrimitivesTest, CreateBoxProducesNonEmptyShape)
{
    auto env = state_->create_env();
    auto res = env.eval(R"(
        box = geoml.create_box(1.0, 2.0, 3.0)
        box_empty = box:is_empty()
    )");
    ASSERT_TRUE(res.valid());
    EXPECT_FALSE(env.get<sol::object>("box_empty").as<bool>());
}

// create_cylinder/create_box both return a geoml::Shape (naming_choosing's own
// history-tracking wrapper, not a bare TopoDS_Shape) - confirms the return type
// crosses the sol2 boundary as a genuine, method-callable Shape object, not just an
// opaque blob (Shape::shape() unwraps back to the real TopoDS_Shape, also usable
// from Lua directly, same object identity story as any other OCCT-shared type).
TEST_F(PrimitivesTest, CreatedShapeUnwrapsToRealTopoDSShape)
{
    auto env = state_->create_env();
    auto res = env.eval(R"(
        box = geoml.create_box(1.0, 2.0, 3.0)
        underlying = box:shape()
        underlying_type = type(underlying)
    )");
    ASSERT_TRUE(res.valid());
    EXPECT_EQ(env.get<sol::object>("underlying_type").as<std::string>(), "userdata");
}
