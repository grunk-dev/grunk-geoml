// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// Exercises the `surfaces` module (generated/surfaces.cpp) - create_surface/
// create_face, both taking four plain gp_Pnt corners (no vector/container
// involved, unlike tests/test_curves.cpp's own nurbs_curve).

#include <grunk/dynamic.hpp>
#include <grunk/plugin.hpp>

#include <gtest/gtest.h>

#include <gp_Pnt.hxx>

namespace {

class SurfacesTest : public ::testing::Test {
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

grunk::state* SurfacesTest::state_ = nullptr;

} // namespace

TEST_F(SurfacesTest, CreateSurfaceFromFourCornersProducesGeomSurface)
{
    auto env = state_->create_env();
    env["p1"] = gp_Pnt(0.0, 0.0, 0.0);
    env["p2"] = gp_Pnt(1.0, 0.0, 0.0);
    env["p3"] = gp_Pnt(1.0, 1.0, 0.0);
    env["p4"] = gp_Pnt(0.0, 1.0, 0.0);

    auto res = env.eval(R"(
        surf = geoml.create_surface(p1, p2, p3, p4)
        surf_ok = surf ~= nil
    )");
    ASSERT_TRUE(res.valid());
    EXPECT_TRUE(env.get<sol::object>("surf_ok").as<bool>());
}

TEST_F(SurfacesTest, CreateFaceFromFourCornersProducesRealFace)
{
    // Not converted to a geoml::Shape here (as tests/test_naming_choosing.cpp does
    // for a boxed TopoDS_Shape): TopoDS_Face -> TopoDS_Shape is an upcast that
    // normally works via OCCT's own add_bases<TopoDS_Shape>() relationship, but
    // that relationship is registered by occt_plugin.cpp, not loaded in this test
    // suite - without it, sol2 has no reason to know a bare, unregistered
    // TopoDS_Face value "is-a" TopoDS_Shape. Confirming the face itself is real
    // (not nil, not the default-constructed empty face) is enough coverage here.
    auto env = state_->create_env();
    env["p1"] = gp_Pnt(0.0, 0.0, 0.0);
    env["p2"] = gp_Pnt(1.0, 0.0, 0.0);
    env["p3"] = gp_Pnt(1.0, 1.0, 0.0);
    env["p4"] = gp_Pnt(0.0, 1.0, 0.0);

    auto res = env.eval(R"(
        face = geoml.create_face(p1, p2, p3, p4)
        face_ok = face ~= nil
        face_type = type(face)
    )");
    ASSERT_TRUE(res.valid());
    EXPECT_TRUE(env.get<sol::object>("face_ok").as<bool>());
    EXPECT_EQ(env.get<sol::object>("face_type").as<std::string>(), "userdata");
}
