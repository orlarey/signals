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
#include "sigtreealgebra.hh"
#include "sigtypesolver.hh"
#include "ppsig.hh"
#include "sigs-config.hh"
#include "sigtype.hh"
#include "sigpattern.hh"
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

static void checkPatternAlgebra()
{
    std::cout << "--- the pattern algebra (free algebra over the extended signature) ---"
              << std::endl;

    TreeAlgebra A;
    Tree        in0 = A.Input(tree(0));
    Tree        n = nullptr, m = nullptr, x = nullptr, y = nullptr;

    // simplify rule n*(m*x) -> (n*m)*x, destructured on 2*(3*in0)
    Tree t1 = A.Mul(tree(2), A.Mul(tree(3), in0));
    check(pat::Mul(pat::num(n), pat::Mul(pat::num(m), pat::var(x))).match(t1),
          "pattern: n*(m*x) matches 2*(3*in0)");
    check(n == tree(2) && m == tree(3) && x == in0,
          "pattern: bindings are the matched subtrees");
    check(!pat::Mul(pat::num(n), pat::Mul(pat::num(m), pat::var(x)))
               .match(A.Mul(tree(2), A.Mul(in0, tree(3)))),
          "pattern: a guard out of place fails the whole match");

    // simplify rule -n*(x-y) -> n*(y-x), destructured on -4*(in0-1)
    Tree t2 = A.Mul(tree(-4), A.Sub(in0, tree(1)));
    check(pat::Mul(pat::negNum(n), pat::Sub(pat::var(x), pat::var(y))).match(t2) &&
              n == tree(-4) && x == in0 && y == tree(1),
          "pattern: -n*(x-y) destructures through two levels");

    // simplify rule select2(c, a, a) -> a: LINEAR pattern, cross-branch equality is
    // a pointer comparison in the rule body (hash-consing makes it exact)
    Tree t3 = A.Select2(in0, A.Mul(tree(2), in0), A.Mul(tree(2), in0));
    Tree c = nullptr, a = nullptr, b = nullptr;
    check(pat::Select2(pat::var(c), pat::var(a), pat::var(b)).match(t3) && a == b,
          "pattern: linearity + hash-consing decide the select2(c, a, a) rule");

    // simplify rule (s@d1)@d2 -> s@(d1+d2): nested delays
    Tree t4 = A.Delay(A.Delay(in0, tree(3)), tree(2));
    Tree s = nullptr, d1 = nullptr, d2 = nullptr;
    check(pat::Delay(pat::Delay(pat::var(s), pat::num(d1)), pat::num(d2)).match(t4) &&
              s == in0 && d1 == tree(3) && d2 == tree(2),
          "pattern: nested delays destructure");

    // the generators: pinned constant, ordered alternative
    check(pat::constant(t4).match(t4) && !pat::constant(t4).match(t1),
          "pattern: constant matches only its own tree");
    Tree z = nullptr;
    check((pat::IntCast(pat::var(z)) | pat::FloatCast(pat::var(z))).match(A.FloatCast(in0)) &&
              z == in0,
          "pattern: ordered alternative binds through the succeeding branch");

    // extended primitives destructure by name, mirroring TreeAlgebra::xt
    Tree e = nullptr;
    check(pat::Pow(pat::var(x), pat::num(e)).match(A.Pow(in0, tree(2))) && e == tree(2),
          "pattern: xtended primitives destructure by name");

    // the depth-1 fragment is exactly the isSigXXX idiom
    int  op = -1;
    Tree u = nullptr, v = nullptr;
    check(isSigBinOp(t1, &op, u, v) == pat::Mul(pat::var(x), pat::var(y)).match(t1) &&
              x == u && y == v,
          "pattern: depth-1 fragment agrees with the isSigXXX destructor");
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

    // The facade IS the type system: assert its verdicts directly on the designed
    // corpus (the natures were built pairwise, the mod-counter is probe-certified).
    auto ty = [](Tree s) { return isSimpleType(getCertifiedSigType(s)); };
    check(ty(i12)->nature() == kInt, "nature: 1+2 stays int");
    check(ty(mix)->nature() == kReal, "nature: input*0.5 is real");
    check(ty(cst)->nature() == kInt, "nature: intCast forces int");
    check(ty(cmp)->nature() == kInt && ty(cmp)->boolean() == kBool,
          "nature: a comparison is a boolean int");
    check(ty(quo)->nature() == kReal, "nature: division floats");
    check(ty(dly)->nature() == kInt, "nature: the delay amount is excluded");
    check(ty(sel)->nature() == kInt, "nature: the selector is excluded");
    check(ty(att)->nature() == kInt, "nature: the attached signal is excluded");
    check(ty(recA)->nature() == kInt, "nature: x = 1 + x@1 stays int");
    check(ty(recB)->nature() == kReal, "nature: y = 0.5 + y@1 rises to real");
    check(ty(recC0)->nature() == kInt && ty(recC1)->nature() == kReal,
          "nature: the two branches of a group settle independently");
    check(ty(i12)->variability() == kKonst, "variability: constants are konst");
    check(ty(mix)->variability() == kSamp, "variability: inputs vary by samples");
    check(ty(recM)->variability() == kSamp, "variability: recursions vary by samples");

    // The intervals: straight-line bounds, and the probe-certified mod-counter, for
    // which plain widening would only give [0, +inf).
    check(ty(dly)->getInterval().lo() == 0 && ty(dly)->getInterval().hi() == 7,
          "interval: delay output covers its initial zeros and its source");
    check(ty(recM)->getInterval().lo() == 0 && ty(recM)->getInterval().hi() == 1999,
          "interval: the mod-counter is certified [0, 1999] (integer modulo)");

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

    // The facade: recType(X, i) is type(proj(i, X)); and the boundary is STRICT --
    // asking the type of structure (a list, or a bare recursive group) is an error.
    TypeSolver& solver = getTypeSolver(outs);
    {
        Tree X = nullptr, w, body_;
        // retrieve recM's group through its projection
        int  pi;
        TLIB_ASSERT(isProj(recM, pi, X));
        check(solver.recType(X, 0) == solver.type(recM), "facade: recType is type of proj");
        bool caught = false;
        try {
            solver.type(outs);  // the top-level LIST is structure, not a signal
        } catch (std::exception& e) {
            caught = true;
        }
        check(caught, "facade: typing a list is an error");
        caught = false;
        try {
            solver.type(X);  // a bare recursive group is solved, not typed
        } catch (std::exception& e) {
            caught = true;
        }
        check(caught, "facade: typing a bare recursive group is an error");
    }

    // The INITIAL algebra: rebuilding through TreeAlgebra is the identity up to
    // alpha-renaming. Rec-free terms come back pointer-EQUAL (hash-consing);
    // recursive terms come back alpha-equivalent with FRESH variables -- and the
    // rebuild never redefines a group (a redefinition is now fatal in tlib, so
    // merely completing this rebuild proves immutability-cleanliness).
    {
        TreeAlgebra A;
        Tree        outs2  = signalRebuild(outs, A);
        check(alphaEquiv(outs2, outs), "identity: rebuild is alpha-equivalent");
        check(areEquiv(outs2, outs) == alphaEquiv(outs2, outs),
              "identity: direct and de-Bruijn alpha-equivalence agree");
        check(signalRebuild(mix, A) == mix, "identity: rec-free rebuild is pointer-equal");
        check(signalRebuild(dly, A) == dly, "identity: delay chain is pointer-equal");
        check(outs2 != outs,
              "identity: recursive groups get fresh variables (alpha, not equality)");
    }
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

    checkPatternAlgebra();
    checkNatureFixpoint();

    std::cout << (gFailed ? "FAILED" : "PASSED") << " (" << gFailed << " failure(s))" << std::endl;
    return gFailed ? 1 : 0;
}
