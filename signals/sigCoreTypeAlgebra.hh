/************************************************************************
 ************************************************************************
    FAUST signal library
    Copyright (C) 2003-2026 GRAME
    ---------------------------------------------------------------------
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License.
 ************************************************************************
 ************************************************************************/

#pragma once

#include <vector>

#include "FaustAlgebra.hh"
#include "sigs-export.hh"

// CoreType uses independent ordered enums so its exact finite lattice does
// not depend on the legacy AudioType representation or interval library.
enum class CoreNature { Integer, Real, Any };
enum class CoreVariability { Constant, Block, Sample };
enum class CoreComputability { Compile, Init, Execute };
enum class CoreVectorability { Vector, Scalar, TrueScalar };
enum class CoreBoolean { Numeric, Boolean };
enum class CoreTypeShape { Bottom, Scalar, Table, Tuple };

/**
 * Exact structural signal type used independently of range analysis.
 * Scalar fields form finite ordered domains; elements reserve structural
 * content for tables and tuples without mixing numeric intervals into the
 * carrier. Bottom represents an attribute not computed yet.
 */
struct CoreType {
    CoreTypeShape         shape         = CoreTypeShape::Bottom;
    CoreNature            nature        = CoreNature::Integer;
    CoreVariability       variability   = CoreVariability::Constant;
    CoreComputability     computability = CoreComputability::Compile;
    CoreVectorability     vectorability = CoreVectorability::Vector;
    CoreBoolean           boolean       = CoreBoolean::Numeric;
    std::vector<CoreType> elements;

    bool operator==(const CoreType& other) const
    {
        return shape == other.shape && nature == other.nature &&
               variability == other.variability && computability == other.computability &&
               vectorability == other.vectorability && boolean == other.boolean &&
               elements == other.elements;
    }

    bool operator!=(const CoreType& other) const { return !(*this == other); }
    bool isScalar() const { return shape == CoreTypeShape::Scalar; }
};

//--------------------------------------------------------------------------
// Public API: exact structural signal typing
//--------------------------------------------------------------------------

/**
 * Return the experimental Faust algebra whose carrier is CoreType. The
 * initial implementation covers scalar constants, inputs and outputs, basic
 * user-interface elements, casts, numeric binary operations, selection, and
 * simple delays with the structural rules of the legacy type system.
 *
 * The algebra never computes intervals or resolution. Traversal, recursive
 * dependencies, fixed-point iteration, validation, and legacy Type
 * materialization remain separate phases. Operations not yet migrated report
 * an explicit error instead of manufacturing an imprecise structural type.
 *
 * The returned stateless singleton remains owned by the signal library and is
 * valid until process termination.
 */
SIGS_API FaustAlgebra<CoreType>& sigCoreTypeAlgebra();
