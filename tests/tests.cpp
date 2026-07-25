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

/* Basic standalone tests of the signal library: construction and hash
 * consing, pretty-printing, type inference (nature, variability, interval).
 */

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "binop.hh"
#include "sigattributes.hh"
#include "sighorizon.hh"
#include "sigintervals.hh"
#include "ppsig.hh"
#include "sigs-config.hh"
#include "sigtype.hh"
#include "sigtyperules.hh"
#include "signals.hh"
#include "tlib.hh"

static int gFailed = 0;

static void check(bool ok, const std::string& what)
{
    if (ok) {
        std::cout << "OK   : " << what << std::endl;
    } else {
        std::cout << "FAIL : " << what << std::endl;
        gFailed++;
    }
}

static void checkNatureFixpoint()
{
    std::cout << "--- nature by fixpoint (shadow of the current type system) ---" << std::endl;

    // (1) straight-line signals : every rule of the dense switch that has no fixpoint
    Tree i12  = sigAdd(sigInt(1), sigInt(2));                     // kInt
    Tree mix  = sigMul(sigInput(0), sigReal(0.5));                // kReal
    Tree cst  = sigIntCast(sigInput(0));                          // kInt despite a kReal child
    Tree cmp  = sigBinOp(kLT, sigReal(1.0), sigReal(2.0));        // kInt : a comparison is boolean
    Tree quo  = sigBinOp(kDiv, sigInt(7), sigInt(2));             // kReal : division floats
    Tree selr = sigSelect2(sigInt(0), sigInt(1), sigReal(2.0));   // kReal

    // The three rules of the form "this argument does not contribute its nature" are only
    // exercised when that argument's nature DIFFERS from the result's -- hence the kReal
    // second argument on signals whose result must stay kInt.
    Tree dly = sigDelay(sigInt(7), sigReal(3.0));                    // kInt : amount excluded
    Tree sel = sigSelect2(sigReal(0.0), sigInt(1), sigInt(2));       // kInt : selector excluded
    Tree att = sigAttach(sigInt(1), sigReal(2.0));                   // kInt : effect excluded

    // (2) a self-recursion that stays kInt : x = 1 + x@1.
    // In symbolic form the rec node and its self-reference are ONE hash-consed node, so
    // the reference can be built before the body it will close over.
    Tree idA   = tree(unique("A"));
    Tree refA  = ref(idA);
    Tree bodyA = sigAdd(sigInt(1), sigDelay1(sigProj(0, refA)));
    Tree recA  = sigProj(0, rec(idA, list1(bodyA)));

    // (3) a self-recursion that RISES to kReal : y = 0.5 + y@1 needs a second round,
    // since the variable starts at the bottom of the lattice (kInt).
    Tree idB   = tree(unique("B"));
    Tree refB  = ref(idB);
    Tree bodyB = sigAdd(sigReal(0.5), sigDelay1(sigProj(0, refB)));
    Tree recB  = sigProj(0, rec(idB, list1(bodyB)));

    // (4) a two-branch group whose branches settle on DIFFERENT natures : the point of
    // keeping one V per branch (a Row) instead of a single value per component.
    Tree idC  = tree(unique("C"));
    Tree refC = ref(idC);
    Tree c0   = sigAdd(sigInt(1), sigDelay1(sigProj(0, refC)));  // kInt
    Tree c1 = sigAdd(sigMul(sigReal(0.5), sigProj(0, refC)),     // kReal
                     sigDelay1(sigProj(1, refC)));
    Tree grpC  = rec(idC, list2(c0, c1));
    Tree recC0 = sigProj(0, grpC);
    Tree recC1 = sigProj(1, grpC);

    // (5) the canonical probe target: a mod-counter m = (1 + m@1) % 2000. The current
    // system reports [0, +inf) ; the certified descending probe must give [0, 1999].
    Tree idM   = tree(unique("M"));
    Tree refM  = ref(idM);
    Tree bodyM = sigBinOp(kRem, sigAdd(sigInt(1), sigDelay1(sigProj(0, refM))), sigInt(2000));
    Tree recM  = sigProj(0, rec(idM, list1(bodyM)));

    // (6) a parameter-driven accumulator: z = z@1 * fb + 1 with fb a slider defaulting
    // to 0.5 (contraction: certified, undated) but reaching 1.0 (true accumulator: dated
    // at worst case). Discriminates the nominal reading from the worst-case one.
    // NB: a slider label is a LIST cons(name, path) -- ppsig's printlabel dereferences
    // hd(label), so a bare symbol label crashes any printing of the widget.
    Tree fb = sigVSlider(list1(tree("\"fb\"")), sigReal(0.5), sigReal(0), sigReal(1),
                         sigReal(0.01));
    Tree idD   = tree(unique("D"));
    Tree refD  = ref(idD);
    Tree bodyD = sigAdd(sigMul(sigDelay1(sigProj(0, refD)), fb), sigInt(1));
    Tree recD  = sigProj(0, rec(idD, list1(bodyD)));

    Tree outs = nil();
    for (Tree s :
         {i12, mix, cst, dly, cmp, quo, sel, selr, att, recA, recB, recC0, recC1, recM, recD}) {
        outs = cons(s, outs);
    }

    typeAnnotation(outs, false);

    // shadowCheckNature recomputes the nature of EVERY annotated subterm reachable from
    // outs and compares it to the one inferSigType stored. Since nature is exact, the
    // only acceptable result is zero divergence.
    check(shadowCheckExactAttributes(outs, true) == 0,
          "the five exact attributes by fixpoint agree with the type system");

    // The interval shadow CLASSIFIES rather than equating (no exact oracle). On this
    // small corpus we still demand: nothing suspicious (no empty against a bounded
    // reference, no incomparable overlap), and nothing strictly coarser.
    IntervalShadowStats st = shadowCheckInterval(outs, true);
    check(st.total() > 0, "interval shadow compared some signals");
    check(st.toEmpty == 0, "interval: never empty where the type system had bounds");
    check(st.incomparable == 0, "interval: no incomparable overlap");
    check(st.wider == 0, "interval: never coarser than the type system");

    // The horizon analysis must date exactly the three unclamped accumulators of this
    // corpus -- recA (int counter, wraps at 2^31) and the two int-counter branches --
    // plus the float accumulator recB, absorbed at ~2^24 samples in single precision.
    // The certified mod-counter must NOT be dated. T* is recB's absorption.
    HorizonReport hr = horizonAnalysis(outs, false);
    check(hr.events.size() == 4, "horizon: four dated accumulators at worst case");
    check(hr.defaultEventCount == 3,
          "horizon: the parameter-driven accumulator is undated at default values");
    check(hr.horizonSamples > 1.6e7 && hr.horizonSamples < 1.7e7,
          "horizon: worst-case T* is the float absorption at ~2^24 samples");
    check(hr.horizonDefaultSamples > 1.6e7 && hr.horizonDefaultSamples < 1.7e7,
          "horizon: nominal T* stays the unparameterized float accumulator");
}

int main()
{
    tlib::init();
    sigs::init();

    // --- construction and hash-consing -----------------------------------
    Tree a = sigAdd(sigInput(0), sigReal(0.5));
    Tree b = sigAdd(sigInput(0), sigReal(0.5));
    check(a == b, "hash-consing: same expression, same tree");
    check(a != sigAdd(sigInput(0), sigReal(0.25)), "hash-consing: different expressions differ");

    int  i = -1;
    Tree x, y;
    check(isSigBinOp(a, &i, x, y) && isSigInput(x, &i) && i == 0,
          "pattern matching: sigAdd is a binop on input 0");

    // --- pretty-printing --------------------------------------------------
    std::ostringstream oss;
    oss << ppsig(a);
    check(oss.str().find("0.5") != std::string::npos, "ppsig prints the real constant: " + oss.str());

    // --- type inference ----------------------------------------------------
    Tree slider = sigVSlider(tree("\"level\""), sigReal(0.5), sigReal(0), sigReal(1), sigReal(0.01));
    Tree sig    = sigMul(a, slider);
    typeAnnotation(sig, false);
    Type t = getCertifiedSigType(sig);
    check(t->nature() == kReal, "type: input * slider is kReal");
    check(t->variability() == kSamp, "type: input * slider is kSamp");

    Type ts = getCertifiedSigType(slider);
    check(ts->variability() == kBlock, "type: slider is kBlock");
    check(ts->getInterval().lo() == 0.0 && ts->getInterval().hi() == 1.0,
          "interval: slider in [0, 1]");

    // input + 0.5 is [-0.5, 1.5], times slider [0, 1] -> product in [-0.5, 1.5]
    check(t->getInterval().lo() >= -0.5 && t->getInterval().hi() <= 1.5,
          "interval: product bounded by [-0.5, 1.5]");

    // --- integer signals ----------------------------------------------------
    Tree n = sigAdd(sigInt(1), sigInt(2));
    typeAnnotation(n, false);
    check(getCertifiedSigType(n)->nature() == kInt, "type: 1 + 2 is kInt");

    checkNatureFixpoint();

    std::cout << (gFailed ? "FAILED" : "PASSED") << " (" << gFailed << " failure(s))" << std::endl;
    return gFailed ? 1 : 0;
}
