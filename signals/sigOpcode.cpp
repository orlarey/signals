/************************************************************************
 ************************************************************************
    FAUST signal library
    Copyright (C) 2003-2026 GRAME, Centre National de Creation Musicale
    SPDX-License-Identifier: LGPL-2.1-or-later
 ************************************************************************
 ************************************************************************/

#include "sigOpcode.hh"

namespace sigs {

//--------------------------------------------------------------------------
// Public API: signal constructor signature
//--------------------------------------------------------------------------

/**
 * Return the interned signature shared by every registered signal constructor.
 *
 * The handle is looked up on every call rather than cached because a TLIB
 * cleanup/init cycle invalidates its identity and allocation state.
 */
Signature signalSignature()
{
    return signature("Faust.Signal");
}

}  // namespace sigs
