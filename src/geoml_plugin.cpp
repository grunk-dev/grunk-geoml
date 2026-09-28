// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// register_geoml() (below) wires together every generated geoml module - see
// codegen/config.yml for the full module list/reasoning and DESIGN.md for the
// architectural decisions this plugin follows (Handle(X) cross-plugin sharing,
// hooks.py duplication). This *exact same* source - unmodified below this comment -
// also compiles against geoml's own feature/autodiff branch + an AD-enabled OCCT
// (adOCCT), with GRUNK_GEOML_WITH_ADOLC defined, when configured with
// -DGRUNK_GEOML_ENABLE_AD=ON (see CMakeLists.txt and README.md's "Algorithmic
// differentiation (AD)"), mirroring grunk-occt's own occt_plugin.cpp exactly.

#include <grunk/dynamic.hpp>
#include <grunk/plugin/loader.hpp>

#include "../generated/surfaces.hpp"
#include "../generated/curves.hpp"
#include "../generated/utilities.hpp"
#include "../generated/boolean_ops.hpp"
#include "../generated/geom_topo.hpp"
#include "../generated/data_structures.hpp"
#include "../generated/naming_choosing.hpp"
#include "../generated/predicates.hpp"
#include "../generated/primitives.hpp"
#include "../generated/transformations.hpp"
#include "../generated/internal_boolean_operations.hpp"
#include "../generated/internal_common.hpp"
#include "../generated/internal_contrib.hpp"
#include "../generated/internal_exports.hpp"
#include "../generated/internal_geometry.hpp"
#include "../generated/internal_imports.hpp"
#include "../generated/internal_logging.hpp"
#include "../generated/internal_math.hpp"
#include "../generated/internal_system.hpp"
#include "../generated/internal_topology.hpp"

#include "predicate_extras.hpp"
#include "transform_extras.hpp"

namespace {

void register_geoml(grunk::state& state, grunk::PluginInfo const& info)
{
    auto ns = state.begin_plugin(info);
    register_surfaces(ns);
    register_curves(ns);
    register_utilities(ns);
    register_boolean_ops(ns);
    register_geom_topo(ns);
    register_data_structures(ns);
    register_naming_choosing(ns);
    register_predicate_extras(ns);
    register_predicates(ns);
    register_primitives(ns);
    register_transformations(ns);
    register_transform_extras(ns);
    register_internal_boolean_operations(ns);
    register_internal_common(ns);
    register_internal_contrib(ns);
    register_internal_exports(ns);
    register_internal_geometry(ns);
    register_internal_imports(ns);
    register_internal_logging(ns);
    register_internal_math(ns);
    register_internal_system(ns);
    register_internal_topology(ns);
}

} // anonymous namespace

GRUNK_PLUGIN_INFO("geoml", "0.1.0")
GRUNK_PLUGIN_REGISTER(register_geoml)
