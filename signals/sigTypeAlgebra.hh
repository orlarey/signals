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

#include "FaustAlgebra.hh"
#include "sigs-export.hh"
#include "sigtype.hh"

//--------------------------------------------------------------------------
// Public API
//--------------------------------------------------------------------------

/**
 * Return the experimental Faust algebra whose carrier is the existing signal
 * Type. The first implementation covers constants, inputs, outputs, numeric
 * binary operations, casts, and basic user-interface elements with the same
 * local rules as the legacy type inference system.
 *
 * The algebra computes only local typing rules: traversal, recursive
 * dependencies, fixed-point iteration, causality checks, and Tree annotation
 * belong to the future attribute engine. Operations not yet migrated report
 * an explicit error instead of returning an unsound placeholder type.
 *
 * The returned stateless singleton remains owned by the signal library and is
 * valid until process termination.
 */
SIGS_API FaustAlgebra<Type>& sigTypeAlgebra();

