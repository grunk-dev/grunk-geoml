// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// AD-portability rule (see DESIGN.md and grunk-occt's own occt_ad_support.hpp, which
// this is adapted from): every constructor/function parameter of type Standard_Real
// is registered as an #ifdef GRUNK_GEOML_WITH_ADOLC-gated pair - a plain Standard_Real
// overload for ordinary builds, and a sol::object-based mixed-conversion overload for
// the AD-enabled build.
//
// Unlike grunk-occt's own occt_ad_support.hpp, this file does NOT provide a
// register_standard_adouble() - Standard_Adouble's own sol2 usertype registration
// stays owned by grunk-occt alone (loaded alongside this plugin in an AD build), not
// duplicated here - see grunk-occt/DESIGN.md's "Reusing one shared Standard_Real/
// adtl::adouble registration, not two". This plugin only ever *consumes* an
// already-registered Standard_Real/Standard_Adouble/adtl::adouble value via to_real()
// below, never registers the type itself.

#pragma once

#include <grunk/dynamic.hpp>

#include <sol/sol.hpp>

#include <utility>

#include <Standard_TypeDef.hxx>

#if defined(GRUNK_GEOML_WITH_ADOLC)
#include <adolc/adtl.h>
#endif

namespace geoml_plugin {

// See grunk-occt's own occt_plugin::to_real() for the full rationale (identical
// logic, only the namespace differs) - converts a Lua argument to Standard_Real,
// preserving an AD-tracked value if one is present.
inline Standard_Real to_real(sol::object o)
{
#if defined(GRUNK_GEOML_WITH_ADOLC)
    if (o.is<Standard_Real>()) {
        return o.as<Standard_Real>();
    }
    if (o.is<adtl::adouble>()) {
        return Standard_Real(o.as<adtl::adouble>());
    }
#endif
    return Standard_Real(o.as<double>());
}

// See grunk-occt's own occt_plugin::to_double(sol::object) - same rationale.
inline double to_double(sol::object o)
{
#if defined(GRUNK_GEOML_WITH_ADOLC)
    return to_real(std::move(o)).getValue();
#else
    return to_real(std::move(o));
#endif
}

// See grunk-occt's own occt_plugin::to_double(Standard_Real) - same rationale.
inline double to_double(Standard_Real v)
{
#if defined(GRUNK_GEOML_WITH_ADOLC)
    return v.getValue();
#else
    return v;
#endif
}

} // namespace geoml_plugin
