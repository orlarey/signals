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

#include "sigs-export.hh"
#include "tree.hh"

/**
 * Replace every n-component symbolic recursive signal group with n fresh
 * one-component groups. A projection proj(i, R) becomes proj(0, R_i).
 *
 * The transformation is pure: RECDEF properties of the source groups are
 * never modified. Sharing outside rewritten recursive references is
 * preserved by a per-call memo.
 */
SIGS_API Tree sigScalarize(Tree signal);

/**
 * Check the scalarized recursive normal form: every recursive projection has
 * index zero and targets a symbolic group whose RECDEF is a singleton list.
 */
SIGS_API bool isSigScalarized(Tree signal);

