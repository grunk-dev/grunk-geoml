// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// Exercises the `predicates` module (generated/predicates.cpp) plus
// src/predicate_extras.hpp's own hand-written ShapePredicate combinator escape hatch
// (operator&&/||/! have no generator-reachable Lua metamethod mapping - see that
// file's own comment) - has_tag/is_type as ShapePredicate factories, and the
// bitwise-metamethod combinator syntax (&/|/~) itself, confirmed here as a real
// regression test (already hand-verified once during development, see DESIGN.md).
//
// Note: ShapePredicate's own operator()(Shape const&) - actually *applying* a
// predicate to a shape - is NOT yet bound to Lua at all (same class of gap
// Transform::operator() had before its own escape hatch; ShapePredicate's own
// hasn't been added yet) - these tests only cover predicate *construction* and
// *combination*, not evaluation, until that lands.

#include <grunk/dynamic.hpp>
#include <grunk/plugin.hpp>

#include <gtest/gtest.h>

#include <TopAbs_ShapeEnum.hxx>

namespace {

class PredicatesTest : public ::testing::Test {
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

grunk::state* PredicatesTest::state_ = nullptr;

} // namespace

TEST_F(PredicatesTest, HasTagProducesShapePredicate)
{
    auto env = state_->create_env();
    auto res = env.eval(R"(
        pred = geoml.has_tag("wing")
        pred_type = type(pred)
        pred_str = tostring(pred)
    )");
    ASSERT_TRUE(res.valid());
    EXPECT_EQ(env.get<sol::object>("pred_type").as<std::string>(), "userdata");
    EXPECT_NE(env.get<sol::object>("pred_str").as<std::string>().find("ShapePredicate"), std::string::npos);
}

// The combinator syntax this plugin's own predicate_extras.hpp adds - confirmed live
// during development (see DESIGN.md's "geoml::ShapePredicate combinators: Lua
// syntax"), turned into a permanent regression test here: & / | / ~ each produce a
// genuine new ShapePredicate (userdata, chainable), not a flattened boolean, exactly
// matching geoml's own C++ semantics for operator&&/||/!.
TEST_F(PredicatesTest, CombinatorsProduceChainableShapePredicates)
{
    auto env = state_->create_env();
    auto res = env.eval(R"(
        local combined = geoml.is_vertex & geoml.is_edge
        combined_type = type(combined)

        local negated = ~geoml.is_solid
        negated_type = type(negated)

        local chained = (geoml.is_vertex & geoml.is_edge) | ~geoml.is_solid
        chained_type = type(chained)
        chained_str = tostring(chained)
    )");
    ASSERT_TRUE(res.valid());
    EXPECT_EQ(env.get<sol::object>("combined_type").as<std::string>(), "userdata");
    EXPECT_EQ(env.get<sol::object>("negated_type").as<std::string>(), "userdata");
    EXPECT_EQ(env.get<sol::object>("chained_type").as<std::string>(), "userdata");
    EXPECT_NE(env.get<sol::object>("chained_str").as<std::string>().find("ShapePredicate"), std::string::npos);
}

// is_type(TopAbs_ShapeEnum) - TopAbs_ShapeEnum is an OCCT enum this plugin's own
// config.yml never discovers (it's declared in OCCT's own header, not geoml's), so
// it classifies as a plain "object" parameter, not "passthrough" - a real
// TopAbs_ShapeEnum *value* has to be pushed from C++ directly (env["..."] = ...),
// a plain Lua number won't implicitly convert. TopAbs_SOLID's own numeric value
// isn't asserted here - only that a real enum value round-trips through the call.
TEST_F(PredicatesTest, IsTypeAcceptsRealEnumValuePushedFromCxx)
{
    auto env = state_->create_env();
    env["solid_kind"] = TopAbs_SOLID;
    auto res = env.eval(R"(
        pred = geoml.is_type(solid_kind)
        pred_type = type(pred)
    )");
    ASSERT_TRUE(res.valid());
    EXPECT_EQ(env.get<sol::object>("pred_type").as<std::string>(), "userdata");
}
