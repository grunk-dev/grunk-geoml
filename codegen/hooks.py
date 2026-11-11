# SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
#
# SPDX-License-Identifier: MPL-2.0

"""This plugin's own CodeGenerator subclass (see grunk.codegen.generate.CodeGenerator's
own docstring for the extension mechanism) - teaches the otherwise library-agnostic
shared generator every OpenCASCADE (OCCT)-specific idiom geoml's own headers use, since
geoml's public API takes/returns raw OCCT types throughout: Standard_Real's
AD-portability dual-emission (gated by GRUNK_GEOML_WITH_ADOLC), Standard_Transient/
opencascade::handle<T>'s heap-allocated, reference-counted construction, and
NCollection_Array1/2<T>'s conversion to/from a plain std::vector<T>.

Deliberately a near-verbatim adapted copy of grunk-occt's own codegen/hooks.py, not a
shared/imported module - see DESIGN.md's "hooks.py duplication is intentional": grunk
core is meant to stay entirely library-agnostic, and these are OCCT idioms, not
generic ones, so they belong at the plugin level, duplicated across every OCCT-based
plugin (grunk-occt, grunk-adolc, this one) rather than centralized into grunk itself.

One real, deliberate difference from grunk-occt's own copy: the Handle(X) classify_param
gate below does NOT require X to be a type this plugin's own config registers - see its
own comment for why (DESIGN.md's "Handle(X) cross-plugin question", empirically settled
by a real build-and-run spike before this file was written).

See src/collections.hpp/src/geoml_ad_support.hpp for the hand-written
geoml_plugin::to_real/to_array1/to_array2/to_real_array1/to_real_array2 helper
functions this file's own emit_param() calls into, and src/geoml_sol_traits.hpp for the
sol::unique_usertype_traits<opencascade::handle<T>> specialization that makes
emit_constructor()'s own "handle" mode actually work at the sol2 binding level -
codegen/config.yml's own `includes_for_kinds`/`includes_for_ctor_modes` keys pull all
three headers into a module's generated .cpp only when this file's own classification
actually produced a kind/ctor-mode needing them.
"""

import re

from grunk.codegen import generate

# OCCT's own reference-counted-object root - a class deriving from this
# (anywhere in its base chain) is always heap-allocated and refcounted via
# Handle(T), never value-constructed (see classify_ctor_mode/emit_bases below) -
# grunk's own generator has no such concept at all, this is entirely this
# plugin's own idiom to recognize and act on.
TRANSIENT_ROOT = "Standard_Transient"

# geoml::Shape's own real, single non-default constructor takes a plain
# `TopoDS_Shape const&` (see geoml/naming_choosing/Shape.h) - geoml's own public
# API wraps raw OCCT shapes this way throughout. A const-reference parameter
# whose static type is TopoDS_Shape cannot reliably accept a *derived* TopoDS_*
# value (TopoDS_Compound, TopoDS_Edge, ...) through sol2's own base-class-aware
# usertype conversion: confirmed broken via a real CI run (ubuntu-latest/GCC
# passed, macos-latest/AppleClang and windows-latest/MSVC both failed with
# sol2's own generic "no matching function call") on exactly this call -
# load_carrier.grr.yml's geoml.Shape.new(sphere_compound), sphere_compound
# being a TopoDS_Compound, not a TopoDS_Shape itself. This is the same sol2
# restriction grunk-occt's own emit_param() Handle(X)-by-value override
# documents elsewhere ("sol2's own base-class-aware ... conversion ... only
# ever applies to a *by-value* parameter" - reference parameters are
# excluded from that code path), just hit here for a plain value type rather
# than a Handle(X). classify_ctor_mode/emit_constructor below special-case
# this one class: rather than relying on sol2's own (here unreliable)
# derived-to-base dispatch for a reference parameter, every concrete
# TopoDS_Shape subtype grunk-occt's own generated/sol_base_classes.hpp
# registers (SOL_DERIVED_CLASSES(TopoDS_Shape, ...)) gets its own exact-match
# constructor candidate, all calling the SAME real Shape(TopoDS_Shape const&)
# constructor via a genuine, always-safe C++ implicit upcast - this isn't
# inventing API surface, only giving sol2 an exact type to match per
# candidate instead of a base-class lookup to perform.
SHAPE_TOPODS_WIDEN_CLASS = "Shape"
SHAPE_TOPODS_WIDEN_CTOR_MODE = "shape_topods_subtype_overloads"
TOPODS_SHAPE_SUBTYPES = (
    "TopoDS_CompSolid",
    "TopoDS_Compound",
    "TopoDS_Edge",
    "TopoDS_Face",
    "TopoDS_Shell",
    "TopoDS_Solid",
    "TopoDS_Vertex",
    "TopoDS_Wire",
)

# The OCCT typedef this plugin's own headers use for a scalar that needs
# AD-portability dual-emission (see emit_param/emit_callable below) - grunk's own
# generator has no concept of this at all, only a `real` kind name this file itself
# chose and is now responsible for.
STANDARD_REAL = "Standard_Real"
# OCCT typedef aliases for plain C++ fundamentals - added to the generator's own
# small built-in CXX_FUNDAMENTAL_PASSTHROUGH_TYPES baseline (bare "double"/"bool"/
# "unsigned int"/"size_t"), which OCCT's own headers never spell that way.
OCCT_TYPEDEF_PASSTHROUGH = {"Standard_Integer", "Standard_Boolean"}

# std::string used to need its own explicit hook here (grunk.codegen's shared
# Param.kind() rejected it outright - see DESIGN.md's "two bugs fixed upstream")
# but is now a genuine, generic built-in in grunk.codegen.generate itself
# (CXX_STD_STRING/Param.kind()'s own unconditional check, ahead of this file's own
# classify_param entirely) - grunk-geoml's own pinned grunk revision was bumped
# 2026-09-28 to include that fix, so the local workaround that used to live here is
# gone; nothing in this file needs to special-case std::string any more.

# opencascade::handle<T> (OCCT's own reference-counted smart-pointer template).
# Unlike grunk-occt's own identical pattern, accepted here regardless of whether T is
# itself a type this plugin's own config registers - see classify_param below for why.
HANDLE_PATTERN = re.compile(r"^opencascade::handle<\s*(\w+)\s*>$")

# NCollection_Array1<T>/NCollection_Array2<T> (e.g. TColgp_Array1OfPnt,
# TColStd_Array1OfReal) are typedefs, invisible as such to a parameter's own
# (non-canonical) spelling - only libclang's *canonical* type resolution reveals the
# actual template, which is what Param.canonical carries and these patterns match
# against - see src/collections.hpp's own "NCollection_Array1/2 support".
ARRAY1_PATTERN = re.compile(r"^NCollection_Array1<\s*(.+?)\s*>$")
ARRAY2_PATTERN = re.compile(r"^NCollection_Array2<\s*(.+?)\s*>$")
# Canonical resolution also resolves Standard_Real/Standard_Integer/Standard_Boolean
# themselves away to their own underlying fundamental type (double/int/bool) - an
# array element's spelling never comes back as "Standard_Real", only "double".
REAL_ELEMENT_SPELLINGS = {"double", "float"}
PASSTHROUGH_ELEMENT_SPELLINGS = {"int", "bool"}

# geoml's own API (unlike OCCT's own NCollection_Array1/2-based one) uses plain
# std::vector<T> directly (nurbs_curve's poles, interpolate_curves' curve list,
# translate's point list, ...) - sol2 already converts a Lua table to/from
# std::vector<T> natively (its own generic container support) whenever T itself is
# push/gettable, with zero wrapping code needed, unlike NCollection_Array1/2 above
# (which genuinely need src/collections.hpp's manual to_array1/to_array2
# conversion).
#
# grunk.codegen's own Param.kind() now recognizes std::vector<T> as a genuine,
# generic built-in too (see DESIGN.md's "two bugs fixed upstream" - the same fix
# that made std::string a built-in, above), and already tries it - with its own
# generate._std_vector_element_supported() - *before* this file's classify_param()
# is even called. So by the time control reaches here, that means for a
# std::vector<T> parameter/return, T is specifically NOT a fundamental/std::string/
# registered-or-plausible-bare-class (core's own generate._std_vector_element_
# supported() already covers all of those) - the one remaining, genuinely OCCT-
# specific case core's own generic support can't know about: T being a Handle(X)
# (e.g. std::vector<Handle(Geom_Curve)>, interpolate_curve_network's own curve
# list) - HANDLE_PATTERN's own `<...>` shape fails core's own bare-type-name
# fallback the same way a bare Handle(X) parameter would without this file's own
# relaxed HANDLE_PATTERN handling below. This is genuinely this plugin's own
# extension of core's own built-in, not a duplicate of it.
STDVECTOR_PATTERN = generate.STD_VECTOR_PATTERN

# The C++ preprocessor macro gating this plugin's own AD-portability dual-emission -
# entirely this file's own concern; grunk's own generator never sees this name.
AD_MACRO = "GRUNK_GEOML_WITH_ADOLC"

# The C++ namespace this plugin's own to_real/to_array1/to_array2/to_real_array1/
# to_real_array2 conversion helpers live in (see src/collections.hpp/
# src/geoml_ad_support.hpp) - a plain string literal this file's own emit_param()
# uses, invisible to grunk's own generator.
NS = "geoml_plugin"


def _array_element_supported(elem: str, registered_types: set[str]) -> bool:
    """Whether elem - an NCollection_Array1/2's own already-canonical template
    argument spelling, e.g. "gp_Pnt" or "double" - is itself a collection element
    type this plugin can convert via src/collections.hpp's to_array1/to_array2.
    Deliberately does NOT extend AD-portability's #ifdef GRUNK_GEOML_WITH_ADOLC
    dual-emission to array elements (see grunk-occt's own codegen README's "Known
    gaps") - a "real" array element is always plain double, in both AD and non-AD
    builds."""
    return (
        elem in REAL_ELEMENT_SPELLINGS
        or elem in PASSTHROUGH_ELEMENT_SPELLINGS
        or elem in registered_types
    )


def _array_element_type(canonical: str, pattern: re.Pattern) -> str:
    """Re-extracts the same element type _array_element_supported already found
    supported, for emission - called only after classify_param has already
    matched, so the match here is guaranteed to succeed."""
    m = pattern.match(generate.strip_type(canonical))
    assert m is not None, f"expected {canonical!r} to match {pattern.pattern!r}"
    return m.group(1).strip()


class Hooks(generate.CodeGenerator):
    OVERLOAD_AMBIGUITY_KINDS = {"real"}
    REVERSIBLE_OPERAND_KINDS = frozenset({"passthrough", "real"})

    # Set/cleared around a second emit_callable() pass for a "real"-kind callable
    # (see emit_callable below) - private to this file, invisible to
    # grunk.codegen.generate itself, which has no "variant"/AD concept at all.
    _ad_variant = False

    def classify_param(self, param, registered_types, registered_enums):
        # Non-const-reference ("out") parameters ARE supported for a registered
        # object (see Param.kind's own docstring) but NOT for Standard_Real - Lua
        # has no mutable-number-by-reference concept. generate.REJECTED, not
        # None: a bare None here would fall through to Param.kind()'s own
        # generic "mutable reference to a genuine, stable-identity class" rule,
        # which genuinely does accept a mutable reference - and Standard_Real's
        # own *canonical* spelling is build-variant-dependent (a fundamental
        # `double` for the stock build, but a real class, Standard_Adouble, for
        # the AD build) - found the hard way regenerating against an AD-enabled
        # geoml: a mutable Standard_Real& out-parameter (geoml::Point::getMinMax)
        # was silently accepted via that fallback (Standard_Adouble, a genuine
        # class, satisfies it) and baked as a literal, non-#ifdef-gated
        # `Standard_Real&` parameter into the *shared* generated/*.cpp - which
        # then failed to even compile for the stock build, where that same
        # parameter resolves to `double&` and sol2 cannot bind a mutable
        # reference to a fundamental-type argument at all. Explicitly rejecting
        # here, regardless of which build generated it, keeps this symmetric.
        if param.base == STANDARD_REAL:
            return generate.REJECTED if param.is_mutable_ref else "real"
        if param.base in OCCT_TYPEDEF_PASSTHROUGH:
            return None if param.is_mutable_ref else "passthrough"
        # std::vector<Handle(X)> - the one std::vector<T> shape core's own built-in
        # support (already tried, and evidently declined, before classify_param()
        # is ever called - see STDVECTOR_PATTERN's own comment above) can't know
        # about on its own: geoml's own headers spell it out directly (no opaque
        # OCCT-style typedef indirection), so param.base already shows the real
        # template argument, for a genuine parameter *and* a synthetic return-type
        # check alike (whose own .canonical is never populated at all - see
        # Param.canonical's own field docstring, and why the NCollection_Array1/2
        # branch below is parameter-only but this one isn't).
        m_vec = STDVECTOR_PATTERN.match(param.base)
        if m_vec and HANDLE_PATTERN.match(m_vec.group(1).strip()):
            return None if param.is_mutable_ref else "passthrough"
        # Handle(X) (i.e. opencascade::handle<X>) - accepted unconditionally, unlike
        # grunk-occt's own identical-looking gate, which requires
        # `m.group(1) in registered_types`. That gate was motivated by a theory (sol2
        # automagic comparison-operator generation being scoped per-translation-unit)
        # which a from-scratch read of the actual vendored sol2 3.5.0 source did not
        # support (automagic_flags never reaches the comparison-operator gate at
        # all - the real gate, insert_default_registrations's supports_op_less/
        # supports_op_equal SFINAE, is registration-independent). A real, minimal
        # two-part spike (build-and-run, not just reasoned about) against
        # grunk-occt's own toolchain confirmed this empirically: a function
        # returning/accepting Handle(Geom_Circle), with Geom_Circle never registered
        # anywhere in that translation unit, compiled and ran correctly (passthrough,
        # equality, tostring); and a second plugin that *does* fully register
        # Geom_Circle correctly dispatched real methods on a value produced by the
        # first, opaque one, in both load orders. See DESIGN.md's "Handle(X)
        # cross-plugin question" for the full writeup - this plugin's whole reason to
        # need this relaxation is that geoml's real API returns/takes Handle(Geom_*)
        # types directly (e.g. Handle(Geom_BSplineCurve) nurbs_curve(...)), and those
        # values need to cross freely between geoml.* and occt.* calls in one recipe
        # without this plugin re-registering OCCT types grunk-occt already owns.
        m = HANDLE_PATTERN.match(param.base)
        if m:
            return "handle"
        # NCollection_Array1<X>/NCollection_Array2<X> - by value or const-reference
        # only (mirrors Standard_Real's own out-param rule above: no Lua-side
        # identity to mutate a bare std::vector<X> in place through, unlike a
        # registered object/Handle). Parameter-only: param.name is empty only for
        # the synthetic Param a Callable's own rejection_reason/accepted_arities
        # build to check a *return* type, so an array-typed return stays rejected
        # (no decided Lua-facing shape yet for a from_array1/2-converted return
        # value).
        #
        # Once m1/m2 actually matches, this IS an NCollection_Array1/2 parameter -
        # a mutable reference is always a definitive rejection (generate.REJECTED,
        # not None): there's no Lua-side identity to mutate a bare array in place
        # through, and this is a universal correctness concern, not a cross-plugin-
        # registration nuance - unlike the unsupported-element case just below,
        # nothing about *this* plugin's own registered_types could ever make a
        # mutable array reference safe. Plain None here used to let Param.kind()'s
        # own generic BARE_TYPE_NAME fallback silently accept the param's *surface*
        # spelling - almost always a bare-identifier typedef (e.g.
        # "TColgp_Array1OfPnt" for "NCollection_Array1<gp_Pnt>") - as a plain
        # mutable "object" instead (a real, confirmed bug - see
        # generate.REJECTED's own module-level docstring; grunk-occt's own
        # hooks.py has the identical fix).
        #
        # An UNSUPPORTED ELEMENT TYPE, by contrast, deliberately stays plain None
        # here (not generate.REJECTED) - unlike grunk-occt's own otherwise-identical
        # hooks.py: geoml's own codegen run never registers OCCT types it only
        # references (gp_Pnt, say - owned entirely by grunk-occt's own separate
        # run), so "element not in *this* plugin's own registered_types" does NOT
        # mean "this array is unsupported," only "not one *this* run's own opinion
        # can settle" - falling through to the generic "object" fallback lets such
        # a value cross the plugin boundary opaquely, exactly the same deliberate
        # relaxation already applied to Handle(X) above (see HANDLE_PATTERN's own
        # comment and DESIGN.md's "Handle(X) cross-plugin sharing"). Confirmed via
        # tests/python/test_hooks.py::test_classify_param_array1_array2's own
        # comment - this is the one param.name/param.canonical to intentionally
        # diverge from grunk-occt's own copy of this method.
        if param.name and param.canonical:
            canon_base = generate.strip_type(param.canonical)
            m1 = ARRAY1_PATTERN.match(canon_base)
            if m1:
                if param.is_mutable_ref:
                    return generate.REJECTED
                if not _array_element_supported(m1.group(1), registered_types):
                    return None
                return "array1"
            m2 = ARRAY2_PATTERN.match(canon_base)
            if m2:
                if param.is_mutable_ref:
                    return generate.REJECTED
                if not _array_element_supported(m2.group(1), registered_types):
                    return None
                return "array2"
        return None

    def classify_ctor_mode(self, class_name, base_chain):
        # Standard_Transient-derived types are never value-constructed by
        # convention (OCCT's own idiom is always heap-allocate + refcount via
        # Handle(T)) - grunk's own generator has no notion of this at all,
        # this override is entirely this file's own responsibility (see
        # TRANSIENT_ROOT above).
        if TRANSIENT_ROOT in base_chain:
            return "handle"
        # See SHAPE_TOPODS_WIDEN_CLASS's own module-level comment above - this
        # covers BOTH of Shape's real constructors (the zero-arg default and the
        # TopoDS_Shape one): emit_constructor below tells them apart by
        # param_decls instead, since this hook only gets the class as a whole.
        if class_name == SHAPE_TOPODS_WIDEN_CLASS:
            return SHAPE_TOPODS_WIDEN_CTOR_MODE
        return None

    def emit_bases(self, class_name, chain):
        # .add_bases<...>() only needs the chain up to (and including)
        # Standard_Transient itself - OCCT's own internal allocator/RTTI mixins
        # further up aren't meaningful Lua-side base classes and aren't
        # registered types themselves, so truncate right there rather than
        # walking base_chain()'s raw result all the way to its natural end.
        if TRANSIENT_ROOT in chain:
            return chain[: chain.index(TRANSIENT_ROOT) + 1]
        return chain

    def emit_constructor(self, class_name, ctor_mode, param_decls, args_str):
        if ctor_mode == SHAPE_TOPODS_WIDEN_CTOR_MODE:
            # The zero-arg default ctor - left alone, exactly as the built-in
            # "value" mode would have emitted it.
            if not param_decls:
                return f"[]() {{ return {class_name}(); }}"
            # The real TopoDS_Shape-taking ctor - widened per
            # SHAPE_TOPODS_WIDEN_CLASS's own comment above. One candidate per
            # concrete subtype plus the base TopoDS_Shape itself, all calling
            # the same real constructor.
            candidates = [f"[](const TopoDS_Shape & s) {{ return {class_name}(s); }}"]
            candidates += [
                f"[](const {subtype} & s) {{ return {class_name}(s); }}"
                for subtype in TOPODS_SHAPE_SUBTYPES
            ]
            return ",\n            ".join(candidates)
        # Only ever called for ctor_mode == "handle" otherwise (see
        # classify_ctor_mode above) - heap-allocate and let
        # geoml_sol_traits.hpp's own
        # sol::unique_usertype_traits<opencascade::handle<T>> bridge the result
        # to Lua as if it were a T instance.
        return (f"[]({', '.join(param_decls)}) -> opencascade::handle<{class_name}> "
                f"{{ return opencascade::handle<{class_name}>(new {class_name}({args_str})); }}")

    def emit_param(self, param, kind):
        if kind == "real":
            if self._ad_variant:
                return f"sol::object {param.name}", f"{NS}::to_real({param.name})"
            return None  # primary variant: plain Standard_Real spelling, builtin fallback is correct
        if kind == "handle":
            # Emitted BY VALUE - mirrors grunk-occt's own identical override,
            # see that file's own (longer) comment for the full "why": sol2's
            # own base-class-aware Handle(Derived)->Handle(X) unique-usertype
            # conversion (sol::detail::inheritance<T>::type_unique_cast, needs
            # SOL_BASE_CLASSES/SOL_DERIVED_CLASSES - generated/
            # sol_base_classes.hpp) only ever applies to a *by-value* parameter
            # (qualified_checker/qualified_getter explicitly exclude reference
            # parameters from that whole code path) - a Handle is a cheap
            # smart pointer to copy (O(1) refcount increment), so this is
            # always safe. This is this plugin's own equivalent of the fix
            # that resolved geoml.interpolate_curves's real segfault (see
            # DESIGN.md) - curve_vec_extras.hpp's own exact-type-only
            # workaround is no longer needed once every Handle(X) parameter
            # generated this way accepts any registered subtype directly.
            m = HANDLE_PATTERN.match(param.base)
            handle_type = f"opencascade::handle<{m.group(1)}>"
            return f"{handle_type} {param.name}", param.name
        if kind == "array1":
            elem = _array_element_type(param.canonical, ARRAY1_PATTERN)
            fn = "to_real_array1" if elem in REAL_ELEMENT_SPELLINGS else "to_array1"
            return f"std::vector<{elem}> const& {param.name}", f"{NS}::{fn}({param.name})"
        if kind == "array2":
            elem = _array_element_type(param.canonical, ARRAY2_PATTERN)
            fn = "to_real_array2" if elem in REAL_ELEMENT_SPELLINGS else "to_array2"
            return (
                f"std::vector<{elem}> const& {param.name}_flat, Standard_Integer {param.name}_rows, "
                f"Standard_Integer {param.name}_cols",
                f"{NS}::{fn}({param.name}_flat, {param.name}_rows, {param.name}_cols)",
            )
        return None

    def emit_return_type(self, return_spelling):
        # A Handle(X)-returning method/constructor that returns it by reference
        # (e.g. Value() const -> const Handle(SomeCurve)&) is emitted as a
        # by-value opencascade::handle<X> return instead of the raw reference - see
        # grunk-occt's own identical override for the full "why" (pushing a bare
        # Handle<X> by reference hits a sol2 code path that tries to instantiate
        # default comparison operators for X, which most OCCT geometry classes don't
        # declare - a Handle is a cheap-to-copy smart pointer, so returning by value
        # instead is always safe).
        if "&" in return_spelling and HANDLE_PATTERN.match(generate.strip_type(return_spelling)):
            return generate.strip_type(return_spelling)
        return None

    def emit_callable(self, c, kinds, ctor_mode):
        # Standard_Real AD-portability dual-emission: the ONLY place this
        # plugin's own hooks.py needs more than one textual variant of the same
        # callable - see grunk.codegen.generate.CodeGenerator.emit_callable's own
        # docstring for why this is entirely this file's own responsibility, not
        # a mechanism grunk's own generator provides.
        if c.kind == "operator" or "real" not in kinds:
            return super().emit_callable(c, kinds, ctor_mode)
        plain = super().emit_callable(c, kinds, ctor_mode)
        self._ad_variant = True
        try:
            alternate = super().emit_callable(c, kinds, ctor_mode)
        finally:
            self._ad_variant = False
        return f"\n#if defined({AD_MACRO})\n            {alternate}\n#else\n            {plain}\n#endif\n        "
