// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// Loads geoml_plugin.so via grunk::plugin::load_native and confirms it's reported as
// loaded, and that its "geoml" namespace table is visible to plain Lua - same shape
// as grunk-occt's own identical test_load.cpp. GEOML_PLUGIN_SO_PATH is set by
// CMakeLists.txt to the built path of the geoml_plugin shared library target.

#include <grunk/dynamic.hpp>
#include <grunk/plugin.hpp>

#include <gtest/gtest.h>

TEST(Load, PluginLoadsAndReportsItself)
{
    grunk::state state;

    grunk::PluginInfo info = grunk::plugin::load_native(state, GEOML_PLUGIN_SO_PATH);

    EXPECT_EQ(info.name, "geoml");

    ASSERT_EQ(state.plugins().size(), 1u);
    EXPECT_EQ(state.plugins()[0].name, "geoml");

    // create_env()'s environment falls back to grunk's *registered-symbol* table
    // (original_env), not to Lua's base library - see grunk-occt's own identical
    // test_load.cpp comment. Check the namespace table directly via the
    // environment's own indexing instead of evaluating a Lua script.
    auto env = state.create_env();
    sol::object geoml_ns = env["geoml"];
    EXPECT_EQ(geoml_ns.get_type(), sol::type::table);
}
