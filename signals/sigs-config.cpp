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

#include "sigs-config.hh"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "interval.hh"
#include "signals.hh"
#include "sigs-state.hh"
#include "sigtype.hh"

// The interval algebra used by the signal type system (declared in
// signals/interval.hh)
itv::interval_algebra gAlgebra;

namespace sigs {

/**
 * Standalone initialization of the library state: symbols, property keys,
 * type singletons, session state and option defaults. NOT called by the
 * Faust compiler, which performs the same writes itself in global.cpp (in
 * its own, order-sensitive sequence); intended for standalone hosts and
 * tests. Requires tlib::init() first. Can be called again between two
 * sessions (after tlib::init()).
 */
void init()
{
    // Symbols (same names as in global.cpp)
    g.FFUN               = symbol("ForeignFunction");
    g.SIGINPUT           = symbol("SigInput");
    g.SIGOUTPUT          = symbol("SigOutput");
    g.SIGDELAY1          = symbol("SigDelay1");
    g.SIGDELAY           = symbol("SigDelay");
    g.SIGPREFIX          = symbol("SigPrefix");
    g.SIGRDTBL           = symbol("SigRDTbl");
    g.SIGWRTBL           = symbol("SigWRTbl");
    g.SIGGEN             = symbol("SigGen");
    g.SIGDOCONSTANTTBL   = symbol("SigDocConstantTbl");
    g.SIGDOCWRITETBL     = symbol("SigDocWriteTbl");
    g.SIGDOCACCESSTBL    = symbol("SigDocAccessTbl");
    g.SIGSELECT2         = symbol("SigSelect2");
    g.SIGASSERTBOUNDS    = symbol("sigAssertBounds");
    g.SIGHIGHEST         = symbol("sigHighest");
    g.SIGLOWEST          = symbol("sigLowest");
    g.SIGBINOP           = symbol("SigBinOp");
    g.SIGFFUN            = symbol("SigFFun");
    g.SIGFCONST          = symbol("SigFConst");
    g.SIGFVAR            = symbol("SigFVar");
    g.SIGPROJ            = symbol("SigProj");
    g.SIGINTCAST         = symbol("SigIntCast");
    g.SIGBITCAST         = symbol("SigBitCast");
    g.SIGFLOATCAST       = symbol("SigFloatCast");
    g.SIGBUTTON          = symbol("SigButton");
    g.SIGCHECKBOX        = symbol("SigCheckbox");
    g.SIGWAVEFORM        = symbol("SigWaveform");
    g.SIGHSLIDER         = symbol("SigHSlider");
    g.SIGVSLIDER         = symbol("SigVSlider");
    g.SIGNUMENTRY        = symbol("SigNumEntry");
    g.SIGHBARGRAPH       = symbol("SigHBargraph");
    g.SIGVBARGRAPH       = symbol("SigVBargraph");
    g.SIGATTACH          = symbol("SigAttach");
    g.SIGENABLE          = symbol("SigEnable");
    g.SIGCONTROL         = symbol("SigControl");
    g.SIGSOUNDFILE       = symbol("SigSoundfile");
    g.SIGSOUNDFILELENGTH = symbol("SigSoundfileLength");
    g.SIGSOUNDFILERATE   = symbol("SigSoundfileRate");
    g.SIGSOUNDFILEBUFFER = symbol("SigSoundfileBuffer");
    g.SIGREGISTER        = symbol("SigRegister");
    g.SIGTUPLE           = symbol("SigTuple");
    g.SIGTUPLEACCESS     = symbol("SigTupleAccess");
    g.SIGFIR             = symbol("SigFIR");
    g.SIGIIR             = symbol("SigIIR");
    g.SIGSUM             = symbol("SigSum");
    g.SIGTEMPVAR         = symbol("SigTempVar");
    g.SIGPERMVAR         = symbol("SigPermVar");
    g.SIGZEROPAD         = symbol("SigZeroPad");
    g.SIGSEQ             = symbol("SigSeq");
    g.SIGOD              = symbol("SigOD");
    g.SIGUS              = symbol("SigUS");
    g.SIGDS              = symbol("SigDS");
    g.SIGCLOCKED         = symbol("SigClocked");
    g.SIMPLETYPE         = symbol("SimpleType");
    g.TABLETYPE          = symbol("TableType");
    g.TUPLETTYPE         = symbol("TupletType");

    // Property keys
    g.ORDERPROP      = tree(symbol("OrderProp"));
    g.RECURSIVNESS   = tree(symbol("RecursivenessProp"));
    g.NULLTYPEENV    = tree(symbol("NullTypeEnv"));
    g.CLKENVPROPERTY = tree(symbol("CLKENVPROPERTY"));

    // Session state
    g.TABBER = Tabber(1);
    g.gSignalTable.clear();
    g.gSignalTrace.clear();
    g.gSignalCounter    = 0;
    g.gCountInferences  = 0;
    g.gCountMaximal     = 0;
    g.gAllocationCount  = 0;
    g.gSTEP             = 1;
    g.gSymListProp      = new property<Tree>();
    g.gMemoizedTypes    = new property<AudioType*>();

    // Option defaults (same values as global.cpp)
    g.gCausality       = false;
    g.gWideningLimit   = 0;
    g.gNarrowingLimit  = 0;
    g.gMaxFIRSize      = 1024;
    g.gFloatSize       = 1;

    // Extended primitive registry: empty in standalone mode (the concrete
    // primitives carry code generation and live in the compiler)
    g.gAbsPrim        = nullptr;
    g.gAcosPrim       = nullptr;
    g.gAsinPrim       = nullptr;
    g.gAtan2Prim      = nullptr;
    g.gAtanPrim       = nullptr;
    g.gCeilPrim       = nullptr;
    g.gCosPrim        = nullptr;
    g.gExp10Prim      = nullptr;
    g.gExpPrim        = nullptr;
    g.gFloorPrim      = nullptr;
    g.gFmodPrim       = nullptr;
    g.gLog10Prim      = nullptr;
    g.gLogPrim        = nullptr;
    g.gMaxPrim        = nullptr;
    g.gMinPrim        = nullptr;
    g.gPowPrim        = nullptr;
    g.gRemainderPrim  = nullptr;
    g.gRintPrim       = nullptr;
    g.gSinPrim        = nullptr;
    g.gSqrtPrim       = nullptr;
    g.gTanPrim        = nullptr;

    // Type singletons (require gMemoizedTypes above)
    g.TINPUT  = makeSimpleType(kReal, kSamp, kExec, kVect, kNum, interval(-1, 1));
    g.TGUI    = makeSimpleType(kReal, kBlock, kExec, kVect, kNum, interval());
    g.TREC    = makeSimpleType(kInt, kSamp, kInit, kScal, kNum, interval(0, 0));
    g.TRECMAX = makeSimpleType(kInt, kSamp, kInit, kScal, kNum, interval(-HUGE_VAL, HUGE_VAL));
}

// Default clock checker: depth-first scan for a sigClocked node, following
// the same order as the compiler's SignalVisitor (branches left to right).
static bool defaultClockChecker(Tree sig, Tree& clock)
{
    Tree exp;
    if (isSigClocked(sig, clock, exp)) {
        return true;
    }
    for (int i = 0; i < sig->arity(); i++) {
        if (defaultClockChecker(sig->branch(i), clock)) {
            return true;
        }
    }
    return false;
}

static ClockChecker gClockChecker = defaultClockChecker;

ClockChecker setClockChecker(ClockChecker f)
{
    ClockChecker old = gClockChecker;
    gClockChecker    = (f != nullptr) ? f : defaultClockChecker;
    return old;
}

static Tree defaultSimplifier(Tree sig)
{
    return sig;
}

static Simplifier gSimplifier = defaultSimplifier;

Simplifier setSimplifier(Simplifier f)
{
    Simplifier old = gSimplifier;
    gSimplifier    = (f != nullptr) ? f : defaultSimplifier;
    return old;
}

Tree simplify(Tree sig)
{
    return gSimplifier(sig);
}

/**
 * Default real printer: shortest "%g" form that round-trips to the same
 * double, with a trailing ".0" added when the result would read as an int.
 */
static std::string defaultRealPrinter(double n)
{
    char c[64];
    for (int p = 1; p <= 32; p++) {
        snprintf(c, sizeof(c), "%.*g", p, n);
        if (strtod(c, nullptr) == n) {
            break;
        }
    }
    if (strcspn(c, ".e") == strlen(c)) {
        strncat(c, ".0", sizeof(c) - strlen(c) - 1);
    }
    return std::string(c);
}

static RealPrinter gRealPrinter = defaultRealPrinter;

RealPrinter setRealPrinter(RealPrinter p)
{
    RealPrinter old = gRealPrinter;
    gRealPrinter    = (p != nullptr) ? p : defaultRealPrinter;
    return old;
}

std::string printReal(double n)
{
    return gRealPrinter(n);
}

}  // namespace sigs

// Declared in signals.hh; routes through the installed clock checker.
bool hasClock(Tree sig, Tree& clock)
{
    return sigs::gClockChecker(sig, clock);
}
