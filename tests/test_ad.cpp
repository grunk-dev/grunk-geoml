// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// Only built when GRUNK_GEOML_ENABLE_AD is ON (see ../CMakeLists.txt), in which case
// geoml_plugin was compiled against geoml's own `feature/autodiff` branch, itself
// built against an AD-enabled OCCT fork (adOCCT) - checks a real forward-mode AD
// derivative through one of this plugin's own *generated* geoml.* functions
// (geoml.translate), mirroring grunk-occt's own tests/test_ad.cpp (the closed-form
// methodology, not its specific figures).
//
// Same "no occt_plugin.so" convention as every other test file here (see e.g.
// tests/test_curves.cpp's own comment): gp_Pnt/gp_Vec are built directly in C++ and
// pushed into Lua as opaque objects, geoml.translate (geoml_plugin's own registered
// function) accepts them by reference without needing them registered anywhere, and
// its own gp_Pnt result is pulled back out into C++ the same way - no Lua-side
// gp_Pnt method (`:X()` etc.) is ever called, since nothing in this process
// registers them. This also means no adolc_plugin.so needs loading either: the
// AD-tracked coordinate is seeded directly via adtl::adouble's own C++ API
// (Standard_Real == Standard_Adouble : public adtl::adouble under this build, see
// ../.deps/adocct/include/opencascade/Standard_Adouble.hxx), not via Lua's
// adolc.adouble binding - simpler, and sufficient to prove the thing this test
// actually needs to prove: that geoml's own AD-enabled code, built against adOCCT,
// correctly propagates a derivative through a real geoml.* call.
//
// geoml.translate(gp_Pnt, gp_Vec) is geoml::translate(origin, direction) - a plain
// coordinate-wise addition (see geoml/transformations/transformations.cpp) - so the
// expected derivative is known in closed form: translating by a constant vector is
// affine in each coordinate of the origin point, so d(moved.X())/d(origin.X()) == 1
// exactly, and the derivative never leaks into Y (geoml::translate never mixes
// coordinates) - the same "affine, so an exact derivative is known and a leak across
// coordinates would be a real, distinguishable bug" reasoning grunk-occt's own
// BezierAdTest uses.

#include <grunk/dynamic.hpp>
#include <grunk/plugin.hpp>

#include <gtest/gtest.h>

#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

namespace {

class GeomlAdTest : public ::testing::Test {
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

grunk::state* GeomlAdTest::state_ = nullptr;

} // namespace

TEST_F(GeomlAdTest, TranslateDerivativeMatchesClosedForm)
{
    Standard_Real x0(1.0);
    x0.setADValue(0, 1.0); // seed forward-mode direction 0

    auto env = state_->create_env();
    env["p"] = gp_Pnt(x0, 0.0, 0.0);
    env["dir"] = gp_Vec(2.0, 3.0, 0.0);

    auto res = env.eval(R"(
        moved = geoml.translate(p, dir)
    )");
    ASSERT_TRUE(res.valid());

    gp_Pnt moved = env.get<gp_Pnt>("moved");

    // x(moved) = x0 + 2.0, x0 = 1.0 -> 3.0, dx/dx0 = 1
    EXPECT_NEAR(moved.X().getValue(), 3.0, 1e-9);
    EXPECT_NEAR(moved.X().getADValue(0), 1.0, 1e-9);

    // y(moved) = 0.0 + 3.0 = 3.0, constant in x0
    EXPECT_NEAR(moved.Y().getValue(), 3.0, 1e-9);
    EXPECT_NEAR(moved.Y().getADValue(0), 0.0, 1e-9);
}
