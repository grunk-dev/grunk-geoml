# SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
#
# SPDX-License-Identifier: MPL-2.0

"""Unit tests for codegen/hooks.py - this plugin's own CodeGenerator subclass
teaching grunk's otherwise library-agnostic shared code generator (grunk.codegen)
every OCCT idiom geoml's own headers use (see hooks.py's own module docstring).
Deliberately a near-verbatim adapted copy of grunk-occt's own tests/python/
test_hooks.py, not a shared/imported module - same reasoning as hooks.py itself
(see DESIGN.md's "hooks.py duplication is intentional").

Run via `pixi run test_hooks` (a plain pytest invocation, no C++ build needed at
all)."""

import importlib.util
import sys
from pathlib import Path

from grunk.codegen import generate

# hooks.py isn't part of an installed Python package (it's loaded dynamically by
# grunk.codegen.generate.load_code_generator() at codegen-run time, via
# codegen/config.yml's own `code_generator: hooks.py` key) - load it here the same
# way, rather than via a package-relative import, so these tests exercise the exact
# file codegen actually uses.
_HOOKS_PATH = Path(__file__).resolve().parent.parent.parent / "codegen" / "hooks.py"
_spec = importlib.util.spec_from_file_location("geoml_codegen_hooks", _HOOKS_PATH)
hooks_module = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = hooks_module
_spec.loader.exec_module(hooks_module)

Hooks = hooks_module.Hooks


def _callable(params, return_spelling="void", is_static=False, class_name="Widget"):
    return generate.Callable(class_name, "method", "Do", params, return_spelling, is_const=False, is_static=is_static)


# ---------------------------------------------------------------------------
# classify_param() - Standard_Real/OCCT typedef passthrough/NCollection_Array1/2 -
# same idioms grunk-occt's own hooks.py teaches, since geoml's public API uses raw
# OCCT types throughout (see hooks.py's own module docstring).
# ---------------------------------------------------------------------------

def test_classify_param_standard_real():
    hooks = Hooks()
    assert generate.Param("Standard_Real", "x").kind(set(), set(), hooks) == "real"
    assert generate.Param("Standard_Real &", "x").kind(set(), set(), hooks) is None  # mutable-ref real rejected


def test_classify_param_occt_typedef_passthrough():
    hooks = Hooks()
    assert generate.Param("Standard_Integer", "x").kind(set(), set(), hooks) == "passthrough"
    assert generate.Param("Standard_Boolean &", "x").kind(set(), set(), hooks) is None


def test_classify_param_array1_array2():
    hooks = Hooks()
    p1 = generate.Param("const TColgp_Array1OfPnt &", "pts", "const NCollection_Array1<gp_Pnt> &")
    assert p1.kind({"gp_Pnt"}, set(), hooks) == "array1"
    # Deliberately DIFFERENT from grunk-occt's own otherwise-identical hooks.py:
    # when gp_Pnt isn't in *this* plugin's own registered_types, classify_param()
    # returns plain None here (not generate.REJECTED) - not "no opinion, reject
    # via the generic fallback anyway" but a genuine, intentional cross-plugin
    # relaxation, mirroring the Handle(X) one below: geoml's own codegen run never
    # registers OCCT types it only references (gp_Pnt is owned entirely by
    # grunk-occt's own separate run), so falling through to the generic "object"
    # fallback lets such a value cross the plugin boundary opaquely instead of
    # being wrongly treated as "definitively unsupported" - see hooks.py's own
    # comment on this exact branch.
    assert p1.kind(set(), set(), hooks) == "object"

    p2 = generate.Param("const TColStd_Array2OfReal &", "m", "const NCollection_Array2<double> &")
    assert p2.kind(set(), set(), hooks) == "array2"  # double is always supported

    # Unlike the unsupported-element case above, a mutable array reference is
    # ALWAYS a definitive rejection, in both this plugin and grunk-occt's own
    # copy of this method - no Lua-side identity to mutate a bare array in place
    # through, a universal correctness concern with no cross-plugin nuance to it.
    # Confirmed this doesn't fall through to the generic "object" fallback either
    # (which would otherwise happily accept a mutable reference to a canonically
    # non-fundamental class).
    p_mut = generate.Param("TColgp_Array1OfPnt &", "pts", "NCollection_Array1<gp_Pnt> &")
    assert p_mut.kind({"gp_Pnt"}, set(), hooks) is None
    assert p_mut.kind(set(), set(), hooks) is None


# ---------------------------------------------------------------------------
# classify_param() - Handle(X): this plugin's own one real, deliberate behavioral
# difference from grunk-occt's own hooks.py (see DESIGN.md's "Handle(X) cross-plugin
# sharing" and hooks.py's own comment on HANDLE_PATTERN) - accepted unconditionally,
# not gated on X being one of *this* plugin's own registered_types.
# ---------------------------------------------------------------------------

def test_classify_param_handle_accepted_even_when_x_is_not_registered_here():
    hooks = Hooks()
    # Unlike grunk-occt's own gated version (which would reject this - X not in
    # registered_types), this plugin's own relaxed HANDLE_PATTERN handling accepts
    # it regardless - Geom_Surface is a real type grunk-occt owns/registers, never
    # this plugin, and Handle(Geom_Surface) values still need to cross freely
    # between geoml.*/occt.* calls in one recipe (Transform::operator(), geom_topo's
    # own CurveToEdge/EdgeToCurve, ...).
    assert generate.Param("opencascade::handle<Geom_Surface>", "x").kind(set(), set(), hooks) == "handle"
    # Still accepted (redundantly) when X *is* locally registered too - the
    # relaxation is additive, not a replacement for the in-run case.
    assert generate.Param("opencascade::handle<Geom_Surface>", "x").kind({"Geom_Surface"}, set(), hooks) == "handle"


def test_classify_param_std_vector_of_handle_is_this_plugins_own_extension():
    # std::vector<Handle(X)> (e.g. interpolate_curve_network's own curve lists) is
    # the one std::vector<T> shape grunk.codegen's own shared core can't know about
    # on its own (OCCT's Handle(X) idiom is out of its library-agnostic scope) - see
    # hooks.py's own STDVECTOR_PATTERN comment for why this is genuinely this
    # plugin's own extension, not a duplicate of core's built-in.
    hooks = Hooks()
    assert generate.Param("std::vector<opencascade::handle<Geom_Curve>>", "x").kind(set(), set(), hooks) == "passthrough"
    assert generate.Param("std::vector<opencascade::handle<Geom_Curve>> &", "x").kind(set(), set(), hooks) is None


def test_std_vector_of_non_handle_element_falls_through_to_core_built_in():
    # A std::vector<T> for a T that ISN'T a Handle(X) - std::string, a fundamental,
    # or a plausible bare class name - is resolved entirely by grunk.codegen's own
    # shared Param.kind() *before* this plugin's own classify_param() is ever
    # called (see DESIGN.md's "two bugs fixed upstream"), so Hooks().classify_param()
    # itself must return None for these - proving the composition (core built-in +
    # this plugin's own Handle(X) extension) actually works end-to-end, not just
    # that neither half individually regressed.
    hooks = Hooks()
    assert hooks.classify_param(generate.Param("std::vector<double>", "x"), set(), set()) is None
    assert hooks.classify_param(generate.Param("std::vector<std::string>", "x"), set(), set()) is None
    # ... but the full Param.kind() path (core + this plugin's hooks together)
    # still correctly classifies it, via core's own built-in.
    assert generate.Param("std::vector<double>", "x").kind(set(), set(), hooks) == "passthrough"


# ---------------------------------------------------------------------------
# classify_ctor_mode() / emit_bases() (both keyed on TRANSIENT_ROOT)
# ---------------------------------------------------------------------------

def test_classify_ctor_mode_transient_base_is_handle():
    hooks = Hooks()
    assert hooks.classify_ctor_mode("Geom_Curve", ["Standard_Transient"]) == "handle"
    assert hooks.classify_ctor_mode("gp_Pnt", []) is None


def test_emit_bases_truncates_at_transient_root():
    hooks = Hooks()
    chain = ["Geom_Geometry", "Standard_Transient", "Some_InternalMixin"]
    assert hooks.emit_bases("Geom_Curve", chain) == ["Geom_Geometry", "Standard_Transient"]


# ---------------------------------------------------------------------------
# emit_param() / emit_constructor() / emit_return_type()
# ---------------------------------------------------------------------------

def test_emit_param_real_primary_variant_falls_back_to_builtin():
    hooks = Hooks()
    assert hooks.emit_param(generate.Param("Standard_Real", "x"), "real") is None


def test_emit_param_real_alternate_variant_uses_to_real():
    hooks = Hooks()
    hooks._ad_variant = True
    decl, arg = hooks.emit_param(generate.Param("Standard_Real", "x"), "real")
    assert decl == "sol::object x"
    assert arg == "geoml_plugin::to_real(x)"


def test_emit_param_array1_real_element_uses_to_real_array1():
    hooks = Hooks()
    p = generate.Param("const TColStd_Array1OfReal &", "xs", "const NCollection_Array1<double> &")
    decl, arg = hooks.emit_param(p, "array1")
    assert decl == "std::vector<double> const& xs"
    assert arg == "geoml_plugin::to_real_array1(xs)"


def test_emit_constructor_handle_mode_heap_allocates():
    hooks = Hooks()
    src = hooks.emit_constructor("Geom_Curve", "handle", [], "")
    assert "opencascade::handle<Geom_Curve>" in src
    assert "new Geom_Curve(" in src


def test_emit_return_type_strips_handle_by_reference():
    hooks = Hooks()
    assert hooks.emit_return_type("const opencascade::handle<Geom_Curve>&") == "opencascade::handle<Geom_Curve>"
    assert hooks.emit_return_type("const gp_Pnt&") is None  # not a Handle - left untouched
    assert hooks.emit_return_type("opencascade::handle<Geom_Curve>") is None  # already by-value


def test_emit_param_handle_kind_is_by_value_not_reference():
    # Mirrors grunk-occt's own identical override/test - see that file's own
    # (longer) comment for the full "why": sol2's own Handle(Derived)->
    # Handle(Base) unique-usertype conversion (needs SOL_BASE_CLASSES/
    # SOL_DERIVED_CLASSES too, see generate.py's own
    # render_sol_base_classes_header) only ever applies to a genuinely
    # by-value parameter - a Handle(X) const& (the verbatim spelling of
    # almost every real geoml/OCCT declaration) must be emitted as a plain
    # opencascade::handle<X> instead. This is what lets e.g.
    # BRepBuilderAPI_MakeFace's Handle(Geom_Surface) parameter accept a
    # Handle(Geom_BSplineSurface) argument natively (see DESIGN.md) - a
    # separate fix (grunk's with_std_vector<VecElement>() gaining the same
    # by-value treatment) is what later let curve_vec_extras.hpp's own
    # exact-type-only workaround be dropped entirely.
    hooks = Hooks()
    param = generate.Param("const opencascade::handle<Geom_Curve>&", "c")
    decl, arg = hooks.emit_param(param, "handle")
    assert decl == "opencascade::handle<Geom_Curve> c"
    assert "&" not in decl
    assert "const" not in decl
    assert arg == "c"


# ---------------------------------------------------------------------------
# emit_callable() - Standard_Real AD dual-emission
# ---------------------------------------------------------------------------

def test_emit_callable_wraps_ad_macro_when_real_present():
    hooks = Hooks()
    c = _callable([generate.Param("Standard_Real", "x")], return_spelling="double")
    src = hooks.emit_callable(c, ["real"], "value")
    assert f"#if defined({hooks_module.AD_MACRO})" in src
    assert "geoml_plugin::to_real(x)" in src  # the alternate (AD) branch
    assert "Standard_Real x" in src  # the primary (plain) branch keeps the bare spelling


def test_emit_callable_no_ad_macro_without_real_kind():
    hooks = Hooks()
    c = _callable([generate.Param("double", "x")], return_spelling="double")
    src = hooks.emit_callable(c, ["passthrough"], "value")
    assert hooks_module.AD_MACRO not in src


# ---------------------------------------------------------------------------
# OVERLOAD_AMBIGUITY_KINDS / REVERSIBLE_OPERAND_KINDS
# ---------------------------------------------------------------------------

def test_overload_ambiguity_prefers_fewer_real_kinds_within_same_arity():
    all_real = (_callable([generate.Param("Standard_Real", "a")]), ["real"])
    concrete = (_callable([generate.Param("gp_Ax2", "a")]), ["object"])
    ordered = generate.ambiguity_safe_overload_order([all_real, concrete], Hooks())
    assert ordered == [concrete, all_real]


def test_reversible_operand_kinds_includes_real():
    assert "real" in Hooks().REVERSIBLE_OPERAND_KINDS
    assert "passthrough" in Hooks().REVERSIBLE_OPERAND_KINDS
