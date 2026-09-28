// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// std::vector<T> <-> NCollection_Array1/2 conversion helpers - byte-identical to
// grunk-occt's own src/collections.hpp (see its own comment for the full rationale).
// Deliberately duplicated, not shared across repos - see DESIGN.md's "hooks.py
// duplication is intentional".

#pragma once

#include <NCollection_Array1.hxx>
#include <NCollection_Array2.hxx>
#include <Standard_TypeDef.hxx>

#include <stdexcept>
#include <tuple>
#include <vector>

namespace geoml_plugin {

template <typename T>
inline NCollection_Array1<T> to_array1(std::vector<T> const& v)
{
    NCollection_Array1<T> arr(1, static_cast<Standard_Integer>(v.size()));
    for (std::size_t i = 0; i < v.size(); ++i) {
        arr.SetValue(static_cast<Standard_Integer>(i + 1), v[i]);
    }
    return arr;
}

inline NCollection_Array1<Standard_Real> to_real_array1(std::vector<double> const& v)
{
    NCollection_Array1<Standard_Real> arr(1, static_cast<Standard_Integer>(v.size()));
    for (std::size_t i = 0; i < v.size(); ++i) {
        arr.SetValue(static_cast<Standard_Integer>(i + 1), Standard_Real(v[i]));
    }
    return arr;
}

template <typename T>
inline std::vector<T> from_array1(NCollection_Array1<T> const& arr)
{
    std::vector<T> v;
    v.reserve(static_cast<std::size_t>(arr.Length()));
    for (Standard_Integer i = arr.Lower(); i <= arr.Upper(); ++i) {
        v.push_back(arr.Value(i));
    }
    return v;
}

template <typename T>
inline NCollection_Array2<T> to_array2(std::vector<T> const& flat, Standard_Integer rows, Standard_Integer cols)
{
    if (rows <= 0 || cols <= 0) {
        throw std::invalid_argument("to_array2: rows and cols must both be positive");
    }
    if (flat.size() != static_cast<std::size_t>(rows) * static_cast<std::size_t>(cols)) {
        throw std::invalid_argument("to_array2: flat.size() must equal rows*cols");
    }
    NCollection_Array2<T> arr(1, rows, 1, cols);
    for (Standard_Integer r = 0; r < rows; ++r) {
        for (Standard_Integer c = 0; c < cols; ++c) {
            arr.SetValue(r + 1, c + 1, flat[static_cast<std::size_t>(r) * static_cast<std::size_t>(cols) + static_cast<std::size_t>(c)]);
        }
    }
    return arr;
}

inline NCollection_Array2<Standard_Real> to_real_array2(std::vector<double> const& flat, Standard_Integer rows, Standard_Integer cols)
{
    if (rows <= 0 || cols <= 0) {
        throw std::invalid_argument("to_real_array2: rows and cols must both be positive");
    }
    if (flat.size() != static_cast<std::size_t>(rows) * static_cast<std::size_t>(cols)) {
        throw std::invalid_argument("to_real_array2: flat.size() must equal rows*cols");
    }
    NCollection_Array2<Standard_Real> arr(1, rows, 1, cols);
    for (Standard_Integer r = 0; r < rows; ++r) {
        for (Standard_Integer c = 0; c < cols; ++c) {
            arr.SetValue(r + 1, c + 1, Standard_Real(flat[static_cast<std::size_t>(r) * static_cast<std::size_t>(cols) + static_cast<std::size_t>(c)]));
        }
    }
    return arr;
}

template <typename T>
inline std::tuple<std::vector<T>, Standard_Integer, Standard_Integer> from_array2(NCollection_Array2<T> const& arr)
{
    Standard_Integer const rows = arr.NbRows();
    Standard_Integer const cols = arr.NbColumns();
    std::vector<T> flat;
    flat.reserve(static_cast<std::size_t>(rows) * static_cast<std::size_t>(cols));
    for (Standard_Integer r = arr.LowerRow(); r <= arr.UpperRow(); ++r) {
        for (Standard_Integer c = arr.LowerCol(); c <= arr.UpperCol(); ++c) {
            flat.push_back(arr.Value(r, c));
        }
    }
    return {std::move(flat), rows, cols};
}

} // namespace geoml_plugin
