/************************************************************************
 ************************************************************************
    FAUST signal library
    Copyright (C) 2003-2026 GRAME, Centre National de Creation Musicale
    ---------------------------------------------------------------------
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation; either version 2.1 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 ************************************************************************
 ************************************************************************/

/** \file sigs-config.hh
 * Host configuration hooks of the signal library.
 *
 * The library delegates to the host application the few services whose exact
 * behaviour depends on the host (here the textual formatting of real numbers,
 * which in the Faust compiler follows the -single/-double/-quad and target
 * language options). Every hook has an autonomous default so a standalone
 * user gets a working library with no setup.
 */

#ifndef __SIGS_CONFIG__
#define __SIGS_CONFIG__

#include <string>

#include "sigs-export.hh"
#include "tree.hh"

namespace sigs {

/// Standalone initialization of the library state (symbols, property keys,
/// type singletons, session state, option defaults). The Faust compiler
/// does NOT call it (global.cpp performs the same writes in its own order);
/// standalone hosts call it after tlib::init(), once per session.
SIGS_API void init();

/// Simplifies (normalizes) a signal expression; used by the FIR/IIR analyses.
using Simplifier = Tree (*)(Tree);

/// Install a custom simplifier; nullptr restores the default (identity).
/// Returns the previously installed simplifier. The Faust compiler installs
/// the simplify() function of its normalization module.
SIGS_API Simplifier setSimplifier(Simplifier f);

/// Simplify a signal expression through the installed simplifier.
SIGS_API Tree simplify(Tree sig);

/// Detects whether a signal contains a sigClocked node (and returns its
/// clock); used by the FIR analysis. The Faust compiler installs its
/// SignalClockChecker-based implementation; the default is a plain
/// depth-first scan.
using ClockChecker = bool (*)(Tree sig, Tree& clock);
SIGS_API ClockChecker setClockChecker(ClockChecker f);

/// Formats a real number when pretty-printing signals (ppsig).
using RealPrinter = std::string (*)(double);

/// Install a custom real printer; nullptr restores the default
/// (shortest round-trip "%g" form, with a trailing ".0" when needed).
/// Returns the previously installed printer.
SIGS_API RealPrinter setRealPrinter(RealPrinter p);

/// Format a real number through the installed printer.
SIGS_API std::string printReal(double n);

}  // namespace sigs

#endif
