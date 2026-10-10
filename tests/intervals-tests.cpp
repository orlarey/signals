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

/* The intervals of the horizon typing, tested on the shapes that once made it
 * fail : the remainder of a rounding, the convex mix, select2 under its
 * condition, the slowly contracting recursion, the literal table, the rotation
 * oscillator, the forms of -fir.
 *
 * Three kinds of checks :
 *  - inclusion : an evaluator runs every witness, in float and in double, with
 *    and without fused multiply-adds, its controls at their bounds and inside,
 *    and every value of every node must lie in the node's interval ; a NaN or
 *    an undefined conversion may only appear where the interval says it may ;
 *  - precision : the bounds a rule promises (a smoother of a control in
 *    [-12, 12] stays near [-12, 12], not [-2^30, 2^30]) ;
 *  - validity : what must be valid is, what may be invalid stays so.
 */

#include <chrono>
#include <climits>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "binop.hh"
#include "ppsig.hh"
#include "sigs-config.hh"
#include "sigs-state.hh"
#include "sigtransform.hh"
#include "sigtreealgebra.hh"
#include "signals.hh"
#include "sigtype.hh"
#include "sigtyperules.hh"
#include "tlib.hh"
#include "xtended.hh"

static int gFailed = 0;

static void check(bool ok, const std::string& what)
{
    std::cout << (ok ? "OK   : " : "FAIL : ") << what << std::endl;
    if (!ok) gFailed++;
}

static void section(const std::string& title)
{
    std::cout << "--- " << title << " ---" << std::endl;
}

//----------------------------------------------------------------------------------------
// The primitives : in the standalone library the registry of extended primitives is
// empty (the concrete ones carry code generation and live in the compiler). The tests
// register signal-level stand-ins under the same names : a node prim(args), typed
// through the registry like the compiler's.
//----------------------------------------------------------------------------------------

class TestPrim : public xtended {
    unsigned int fArity;

   public:
    TestPrim(const char* name, unsigned int arity) : xtended(name), fArity(arity) {}
    unsigned int arity() override { return fArity; }
    bool         needCache() override { return true; }
    Tree         computeSigOutput(const std::vector<Tree>& args) override
    {
        return fArity == 1 ? tree(symbol(), args[0]) : tree(symbol(), args[0], args[1]);
    }
};

static void registerPrimitives()
{
    sigs::State& g = sigs::g;
    g.gFloorPrim   = new TestPrim("floor", 1);
    g.gRintPrim    = new TestPrim("rint", 1);
    g.gCeilPrim    = new TestPrim("ceil", 1);
    g.gAbsPrim     = new TestPrim("abs", 1);
    g.gSinPrim     = new TestPrim("sin", 1);
    g.gCosPrim     = new TestPrim("cos", 1);
    g.gExpPrim     = new TestPrim("exp", 1);
    g.gSqrtPrim    = new TestPrim("sqrt", 1);
    g.gLogPrim     = new TestPrim("log", 1);
    g.gPowPrim     = new TestPrim("pow", 2);
    g.gFmodPrim    = new TestPrim("fmod", 2);
    g.gMinPrim     = new TestPrim("min", 2);
    g.gMaxPrim     = new TestPrim("max", 2);
}

//----------------------------------------------------------------------------------------
// Builders : the shapes of the normal form, written out by hand
//----------------------------------------------------------------------------------------

static Tree slider(const std::string& name, double init, double lo, double hi)
{
    return sigHSlider(list1(tree(("\"" + name + "\"").c_str())), sigReal(init), sigReal(lo),
                      sigReal(hi), sigReal(0.001));
}

/// x delayed by n samples, as the normal form writes a recursive read : Delay(x, n)
static Tree late(Tree x, int n = 1)
{
    return sigDelay(x, sigInt(n));
}

/// a one-variable recursion : body(y') where y' is the previous output
static Tree rec1(const std::string& name, const std::function<Tree(Tree)>& body)
{
    Tree id = tree(unique(name.c_str()));
    Tree y  = sigProj(0, ref(id));
    return sigProj(0, rec(id, list1(body(late(y)))));
}

static Tree one()
{
    return sigReal(1.0);
}

/// the impulse 1 - 1'
static Tree impulse()
{
    return sigSub(sigInt(1), late(sigInt(1)));
}

//----------------------------------------------------------------------------------------
// The evaluator : the program the intervals describe, sample by sample. Real
// operations round in T (float or double) ; integer operations wrap in int32 ;
// with fma, a product added to a term is fused, as a C++ compiler may do.
//----------------------------------------------------------------------------------------

struct Run {
    std::map<Tree, double> controls;  ///< slider values (default : init)
    bool                   fma = false;
    int                    samples = 2048;
};

template <typename T>
class Evaluator {
    const Run&                                             fRun;
    std::unordered_map<Tree, std::unordered_map<int, double>> fMemo;
    std::unordered_map<Tree, bool>                         fUndefined;  ///< node -> an undefined operation happened

    static bool isInt(Tree t)
    {
        Type ty = getCertifiedSigType(t);
        return ty && ty->nature() == kInt;
    }
    static int32_t wrap(int64_t v) { return static_cast<int32_t>(static_cast<uint32_t>(v)); }
    bool           fAnyUndefined = false;  ///< an undefined operation happened in this run
    double         toInt(Tree t, double v)
    {
        if (std::isnan(v) || v <= -2147483649.0 || v >= 2147483648.0) {
            fUndefined[t] = true;
            fAnyUndefined = true;
            return double(INT_MIN);  // what cvttsd2si gives
        }
        return double(static_cast<int32_t>(v));
    }
    T r(double v) { return static_cast<T>(v); }

    /// a product a * b of reals, for the fused forms
    bool realProduct(Tree t, Tree& a, Tree& b)
    {
        int op;
        return isSigBinOp(t, &op, a, b) && op == kMul && !isInt(t);
    }

    double binop(Tree t, int op, Tree x, Tree y, int n)
    {
        Tree a, b;
        if (fRun.fma && !isInt(t) && (op == kAdd || op == kSub)) {
            if (realProduct(x, a, b)) {
                T c = r(at(y, n));
                return std::fma(r(at(a, n)), r(at(b, n)), op == kAdd ? c : -c);
            }
            if (realProduct(y, a, b)) {
                T c = r(at(x, n));
                return std::fma(op == kAdd ? r(at(a, n)) : -r(at(a, n)), r(at(b, n)), c);
            }
        }
        double u = at(x, n), v = at(y, n);
        if (op >= kGT && op <= kNE) {
            switch (op) {
                case kGT: return u > v;
                case kLT: return u < v;
                case kGE: return u >= v;
                case kLE: return u <= v;
                case kEQ: return u == v;
                default: return u != v;
            }
        }
        if (isInt(t)) {
            int64_t p = int64_t(u), q = int64_t(v);
            switch (op) {
                case kAdd: return wrap(p + q);
                case kSub: return wrap(p - q);
                case kMul: return wrap(p * q);
                case kRem:
                    if (q == 0) {
                        fUndefined[t] = fAnyUndefined = true;
                        return 0;
                    }
                    return wrap(p % q);
                case kAND: return wrap(p & q);
                case kOR: return wrap(p | q);
                case kXOR: return wrap(p ^ q);
                case kLsh:
                case kARsh:
                case kLRsh:
                    if (q < 0 || q > 31) {
                        fUndefined[t] = fAnyUndefined = true;
                        return 0;
                    }
                    if (op == kLsh) return wrap(int64_t(uint32_t(int32_t(p)) << q));
                    if (op == kARsh) return double(int32_t(p) >> q);
                    return double(int32_t(uint32_t(int32_t(p)) >> q));
                default: break;
            }
        }
        T p = r(u), q = r(v);
        switch (op) {
            case kAdd: return p + q;
            case kSub: return p - q;
            case kMul: return p * q;
            case kDiv: return p / q;
            case kRem: return std::fmod(p, q);
            default: throw std::runtime_error("evaluator : binop " + std::to_string(op));
        }
    }

    double prim(Tree t, int n)
    {
        void* p = getUserData(t);
        auto  arg = [&](int i) { return r(at(t->branch(i), n)); };
        auto  is  = [&](xtended* x) { return p == static_cast<void*>(x); };
        const sigs::State& g = sigs::g;
        if (is(g.gFloorPrim)) return std::floor(arg(0));
        if (is(g.gRintPrim)) return std::rint(arg(0));
        if (is(g.gCeilPrim)) return std::ceil(arg(0));
        if (is(g.gSinPrim)) return std::sin(arg(0));
        if (is(g.gCosPrim)) return std::cos(arg(0));
        if (is(g.gExpPrim)) return std::exp(arg(0));
        if (is(g.gSqrtPrim)) return std::sqrt(arg(0));
        if (is(g.gLogPrim)) return std::log(arg(0));
        if (is(g.gPowPrim)) return std::pow(arg(0), arg(1));
        if (is(g.gFmodPrim)) return std::fmod(arg(0), arg(1));
        if (is(g.gAbsPrim)) return isInt(t) ? std::abs(at(t->branch(0), n)) : std::abs(arg(0));
        if (is(g.gMinPrim)) {
            double u = at(t->branch(0), n), v = at(t->branch(1), n);
            return isInt(t) ? std::min(u, v) : (r(u) < r(v) ? r(u) : r(v));
        }
        if (is(g.gMaxPrim)) {
            double u = at(t->branch(0), n), v = at(t->branch(1), n);
            return isInt(t) ? std::max(u, v) : (r(u) < r(v) ? r(v) : r(u));
        }
        throw std::runtime_error("evaluator : primitive");
    }

    double compute(Tree t, int n)
    {
        int    i, op;
        double v;
        Tree   x, y, z, label, init, lo, hi, step, g, var, body;
        tvec   subs;
        if (isSigInt(t, &i)) return i;
        if (isSigReal(t, &v)) return r(v);
        if (isSigInput(t, &i)) return r(0.9 * std::sin(0.0137 * n * (i + 1)) + (n == 0 ? 0.1 : 0));
        if (isSigHSlider(t, label, init, lo, hi, step)) {
            auto c = fRun.controls.find(t);
            if (c != fRun.controls.end()) return r(c->second);
            isSigReal(init, &v);
            return r(v);
        }
        if (isSigBinOp(t, &op, x, y)) return binop(t, op, x, y, n);
        if (isSigDelay(t, x, y)) {
            const double d = at(y, n);
            return n - int(d) < 0 ? 0 : at(x, n - int(d));
        }
        if (isSigDelay1(t, x)) return n < 1 ? 0 : at(x, n - 1);
        if (isProj(t, &i, g)) {
            if (!isRec(g, var, body)) throw std::runtime_error("evaluator : free projection");
            return at(nth(body, i), n);
        }
        if (isSigSelect2(t, x, y, z)) {
            const double s = at(x, n), a = at(y, n), b = at(z, n);
            return s != 0 ? b : a;
        }
        if (isSigFrac(t, x) || isSigCfrac(t, x)) {
            const bool frac = isSigFrac(t, x);
            auto       round = [&](T v) { return frac ? std::floor(v) : std::rint(v); };
            Tree       a, b;
            if (fRun.fma && realProduct(x, a, b)) {  // the product contracted with the subtraction
                const T pa = r(at(a, n)), pb = r(at(b, n));
                return std::fma(pa, pb, -round(pa * pb));
            }
            const T v = r(at(x, n));
            return v - round(v);
        }
        if (isSigTpt(t, x)) {
            const T v = r(at(x, n));
            return v / (v + T(1));
        }
        if (isSigMix(t, x, y, z)) {
            const T c = r(at(x, n)), a = r(at(y, n)), b = r(at(z, n)), w = T(1) - c;
            return fRun.fma ? std::fma(c, b, w * a) : w * a + c * b;
        }
        if (isSigIntCast(t, x)) return toInt(t, at(x, n));
        if (isSigFloatCast(t, x)) return r(at(x, n));
        if (isSigSum(t, subs)) {
            T s = 0;
            for (Tree u : subs) s = s + r(at(u, n));
            return s;
        }
        if (isSigFIR(t, subs)) {  // FIR[x, c0, c1, ...] = sum c_k x(n-k)
            T s = 0;
            for (size_t k = 1; k < subs.size(); k++) {
                s = s + r(at(subs[k], n)) * r(n - int(k - 1) < 0 ? 0 : at(subs[0], n - int(k - 1)));
            }
            return s;
        }
        if (isSigIIR(t, subs)) {  // IIR[Vi, x, c0, c1, ...] : y = x + sum_k c_(k+1) y(n-k), k >= 1
            T s = r(at(subs[1], n));
            for (size_t k = 3; k < subs.size(); k++) {
                const int m = n - int(k - 2);
                s = s + r(at(subs[k], n)) * r(m < 0 ? 0 : at(t, m));
            }
            return s;
        }
        if (isSigRDTbl(t, x, y)) {
            Tree size, gen, wi, ws, content;
            if (!isSigWRTbl(x, size, gen, wi, ws) || !isSigInt(size, &i) || !isSigGen(gen, content)) {
                throw std::runtime_error("evaluator : table");
            }
            int k = std::max(0, std::min(i - 1, int(at(y, n))));  // the guard of promotion
            if (isSigWaveform(content)) return at(content->branch(k % content->arity()), 0);
            return at(content, k);  // the generator at time k
        }
        if (getUserData(t) != nullptr) return prim(t, n);
        std::ostringstream os;
        os << ppsig(t, 40);
        throw std::runtime_error("evaluator : unsupported " + os.str());
    }

   public:
    explicit Evaluator(const Run& run) : fRun(run) {}

    double at(Tree t, int n)
    {
        auto& m  = fMemo[t];
        auto  it = m.find(n);
        if (it != m.end()) return it->second;
        const double v = compute(t, n);
        m[n]           = v;
        return v;
    }

    /// every value of every evaluated node within its interval ; returns the violations
    int inclusion(std::string& first)
    {
        int bad = 0;
        for (const auto& [t, values] : fMemo) {
            Type ty = getCertifiedSigType(t);
            if (!ty) continue;
            const itv::interval I       = ty->getInterval();
            const bool          invalid = I.mayBeInvalid();
            for (const auto& [n, v] : values) {
                // an execution that went through an undefined operation promises no
                // value : past it, only the alert of the interval is checked
                bool ok = std::isnan(v) ? invalid
                                        : (I.lo() <= v && v <= I.hi()) || (invalid && fAnyUndefined);
                if (fUndefined.count(t) && !invalid) ok = false;
                if (!ok) {
                    if (bad == 0) {
                        std::ostringstream os;
                        os << "value " << v << " at sample " << n << " outside " << I << " : "
                           << ppsig(t, 60);
                        first = os.str();
                    }
                    bad++;
                }
            }
        }
        return bad;
    }
};

//----------------------------------------------------------------------------------------
// The harness : type the outputs, then evaluate them in several runs
//----------------------------------------------------------------------------------------

static std::vector<Tree> slidersOf(Tree t, std::map<Tree, bool>& seen)
{
    std::vector<Tree> out;
    std::function<void(Tree)> walk = [&](Tree u) {
        if (seen.count(u)) return;
        seen[u] = true;
        Tree label, init, lo, hi, step, var, body;
        if (isSigHSlider(u, label, init, lo, hi, step)) out.push_back(u);
        if (isRec(u, var, body)) {
            if (body) walk(body);
            return;
        }
        for (int i = 0; i < u->arity(); i++) walk(u->branch(i));
    };
    walk(t);
    return out;
}

/// type the outputs in the given precision ; then, in the same precision, evaluate them
/// with the controls at their minimum, maximum, default and at random, with and
/// without fused multiply-adds ; every value of every node must lie in its interval
static void inclusion(const std::string& name, Tree outs, int precision, int samples = 2048)
{
    itv::programPrecision() = precision;
    typeAnnotation(outs, false);
    std::map<Tree, bool> seen;
    std::vector<Tree>    controls = slidersOf(outs, seen);
    std::vector<Run>     runs;
    std::mt19937         rng(1234);
    for (int k = 0; k < 6; k++) {
        Run run;
        run.samples = samples;
        for (Tree s : controls) {
            Tree   label, init, lo, hi, step;
            double vi, vl, vh;
            isSigHSlider(s, label, init, lo, hi, step);
            isSigReal(init, &vi);
            isSigReal(lo, &vl);
            isSigReal(hi, &vh);
            const double rnd = std::uniform_real_distribution<double>(vl, vh)(rng);
            run.controls[s]  = k == 0 ? vl : k == 1 ? vh : k == 2 ? vi : rnd;
        }
        runs.push_back(run);
    }
    int         bad = 0;
    std::string first;
    for (bool fma : {false, true}) {
        for (Run run : runs) {
            run.fma = fma;
            std::string f;
            int         b = 0;
            if (precision == 1) {
                Evaluator<float> ev(run);
                for (int n = 0; n < run.samples; n++) {
                    for (Tree o = outs; !isNil(o); o = tl(o)) ev.at(hd(o), n);
                }
                b = ev.inclusion(f);
            } else {
                Evaluator<double> ev(run);
                for (int n = 0; n < run.samples; n++) {
                    for (Tree o = outs; !isNil(o); o = tl(o)) ev.at(hd(o), n);
                }
                b = ev.inclusion(f);
            }
            if (b && first.empty()) first = f;
            bad += b;
        }
    }
    check(bad == 0, "inclusion (" + std::string(precision == 1 ? "float" : "double") + ") : " + name +
                        (bad ? " -- " + std::to_string(bad) + " values outside, first " + first : ""));
}

static itv::interval itvOf(Tree s)
{
    return getCertifiedSigType(s)->getInterval();
}

static Tree outs(std::initializer_list<Tree> l)
{
    Tree r = nil();
    for (Tree t : l) r = cons(t, r);
    return r;
}

//----------------------------------------------------------------------------------------
// The witnesses
//----------------------------------------------------------------------------------------

static void remainders()
{
    section("the remainder of a rounding");
    for (int precision : {1, 2}) {
        const std::string p = precision == 1 ? " (float)" : " (double)";
        // a phase : y = X - floor(X), X = y' + inc
        Tree inc   = slider("inc" + p, 0.01, 0.0, 0.5);
        Tree phase = rec1("phase", [&](Tree y) {
            Tree x = sigAdd(y, inc);
            return sigSub(x, sigFloor(x));
        });
        // the same phase as the normal form spreads it : (inc - floor(inc + y')) + y'
        Tree spread = rec1("spread", [&](Tree y) {
            return sigAdd(sigSub(inc, sigFloor(sigAdd(inc, y))), y);
        });
        // the remainders of rint and ceil
        Tree d   = sigMul(slider("f" + p, 1, 0, 10), sigReal(1.0 / 50));
        Tree rem = sigSub(d, sigRint(d));
        Tree cei = sigSub(d, sigCeil(d));
        // the plain form on a large product : X - floor(X), X = 2^30 (d - rint(d)), and its
        // 24-bit fraction, the fixed-point phasor of the libraries
        Tree big  = sigMul(sigReal(1073741824.0), rem);
        Tree frac = sigMul(sigReal(16777216.0), sigSub(big, sigFloor(big)));
        // the edge the margin covers : x just below 1 and inc a quarter of its ulp, a
        // tie in both roundings : inc + x rounds (to even) up to 1, inc - 1 down to -1,
        // and the spread sum is -x's ulp, just below 0
        const double xe   = precision == 1 ? double(std::nextafter(1.0f, 0.0f)) : std::nextafter(1.0, 0.0);
        const double ie   = precision == 1 ? std::ldexp(1.0, -25) : std::ldexp(1.0, -54);
        Tree         ex   = sigReal(xe);
        Tree         ei   = sigReal(ie);
        Tree         edge = sigAdd(sigSub(ei, sigFloor(sigAdd(ei, ex))), ex);
        Tree o    = outs({phase, spread, rem, cei, frac, sigIntCast(sigMul(phase, sigReal(1000))),
                          sigIntCast(frac), edge});
        itv::programPrecision() = precision;
        typeAnnotation(o, false);
        check(itvOf(phase).lo() > -1e-3 && itvOf(phase).hi() < 1 + 1e-3 && itvOf(phase).isValid(),
              "precision : x - floor(x) of a phase within [0, 1]" + p);
        check(itvOf(spread).lo() > -1e-3 && itvOf(spread).hi() < 1 + 1e-3 && itvOf(spread).isValid(),
              "precision : the spread form (inc - floor(inc + x')) + x' within [0, 1]" + p);
        check(itvOf(rem).lo() >= -0.5 - 1e-3 && itvOf(rem).hi() <= 0.5 + 1e-3,
              "precision : x - rint(x) within [-1/2, 1/2]" + p);
        check(itvOf(cei).lo() >= -1 - 1e-3 && itvOf(cei).hi() <= 1e-3,
              "precision : x - ceil(x) within [-1, 0]" + p);
        check(itvOf(sigIntCast(frac)).isValid(),
              "validity : int(2^24 frac(2^30 (x - rint x))) is valid, the margin is half an ulp" + p);
        inclusion("remainders of roundings", o, precision);
    }
}

static void mixes()
{
    section("the convex mix");
    for (int precision : {1, 2}) {
        const std::string p = precision == 1 ? " (float)" : " (double)";
        Tree s  = slider("s" + p, 0, -12, 12);
        Tree pl = sigReal(0.999);
        // y = (1 - p) s + p y'
        Tree smooth = rec1("smooth", [&](Tree y) { return sigAdd(sigMul(sigSub(one(), pl), s), sigMul(pl, y)); });
        // the products flattened by the normal form : 440 * (pow(2, s/12) * (1 - p)) + p y'
        Tree k       = sigPow(sigReal(2.0), sigMul(s, sigReal(1.0 / 12)));
        Tree flat    = rec1("flat", [&](Tree y) {
            return sigAdd(sigMul(sigReal(440), sigMul(k, sigSub(one(), pl))), sigMul(pl, y));
        });
        // the reciprocal of a smoothed positive value
        Tree f       = slider("freq" + p, 440, 50, 1000);
        Tree sf      = rec1("sf", [&](Tree y) { return sigAdd(sigMul(sigSub(one(), pl), f), sigMul(pl, y)); });
        Tree inverse = sigDiv(sigReal(48000), sf);
        Tree o       = outs({smooth, flat, sf, inverse, sigIntCast(sigAdd(smooth, sigReal(12)))});
        itv::programPrecision() = precision;
        typeAnnotation(o, false);
        check(itvOf(smooth).lo() > -12.1 && itvOf(smooth).hi() < 12.1,
              "precision : a smoother of [-12, 12] stays near [-12, 12]" + p);
        // from the initial 0 : (1 - p) 220 = 0.22 at the first sample, at most 880 after,
        // plus the rounding of pow and of the smoother (0.25 %)
        check(itvOf(flat).lo() > 0.2 && itvOf(flat).hi() < 882.2,
              "precision : the flattened mix 440 pow(2, s/12) stays in [0.22, 880]" + p);
        check(itvOf(sf).lo() > 0, "precision : a smoother of [50, 1000] stays positive" + p);
        check(itvOf(inverse).isBounded() && itvOf(inverse).isValid(),
              "validity : the reciprocal of a positive smoother is bounded" + p);
        inclusion("convex mixes", o, precision);
    }
}

static void selects()
{
    section("select2 under its condition");
    for (int precision : {1, 2}) {
        const std::string p = precision == 1 ? " (float)" : " (double)";
        // select2(abs(t) < eps, t, 1) never divides by a t near zero
        Tree t   = sigMul(sigReal(0.001), slider("attack" + p, 1, 0, 30));
        Tree d   = sigSelect2(sigBinOp(kLT, sigAbs(t), sigReal(1.1920929e-07)), t, sigReal(1.0));
        Tree q   = sigDiv(one(), sigMul(sigReal(48000), d));
        // a pole computed under t > 0, then a smoother
        Tree t60  = slider("t60" + p, 0.5, 0, 100);
        Tree pole = sigSelect2(sigBinOp(kGT, t60, sigReal(0)), sigReal(0),
                               sigExp(sigDiv(sigReal(-1), sigMul(t60, sigReal(48000 / 6.91)))));
        Tree x    = slider("x" + p, 0, 0, 1);
        Tree y    = rec1("pole", [&](Tree yy) {
            return sigAdd(sigMul(sigSub(one(), pole), x), sigMul(pole, yy));
        });
        Tree o = outs({q, pole, y});
        itv::programPrecision() = precision;
        typeAnnotation(o, false);
        check(itvOf(q).isBounded() && itvOf(q).isValid(), "precision : the guarded division is bounded" + p);
        check(itvOf(pole).lo() >= 0 && itvOf(pole).hi() <= 1, "precision : the guarded pole lies in [0, 1]" + p);
        check(itvOf(y).isBounded() && itvOf(y).lo() >= 0, "precision : its smoother is bounded and nonnegative" + p);
        inclusion("select2 guards", o, precision);
    }
}

static void slowPoles()
{
    section("the slowly contracting recursion");
    for (int precision : {1, 2}) {
        const std::string p = precision == 1 ? " (float)" : " (double)";
        Tree x  = slider("x" + p, 0, 0, 1);
        Tree pl = sigReal(0.9999997);
        Tree y  = rec1("slow", [&](Tree yy) { return sigAdd(sigMul(sigSub(one(), pl), x), sigMul(pl, yy)); });
        Tree o  = outs({y});
        itv::programPrecision() = precision;
        typeAnnotation(o, false);
        check(itvOf(y).lo() >= 0 && itvOf(y).hi() <= 16,
              "precision : a pole a few ulps below 1 is bounded (the probe ladder)" + p);
        inclusion("slow pole", o, precision);
    }
}

static void tables()
{
    section("tables");
    for (int precision : {1, 2}) {
        const std::string p = precision == 1 ? " (float)" : " (double)";
        Tree wf  = sigWaveform({sigReal(1.0), sigReal(2.0), sigReal(4.0), sigReal(8.0)});
        auto t   = [&](int i) { return sigRDTbl(sigWRTbl(sigInt(4), sigGen(wf)), sigInt(i)); };
        Tree q   = sigDiv(sigReal(100), sigSub(t(3), t(1)));
        Tree o   = outs({q, t(2)});
        itv::programPrecision() = precision;
        typeAnnotation(o, false);
        check(itvOf(t(2)).lo() == 4 && itvOf(t(2)).hi() == 4, "precision : a constant read of a waveform is its element" + p);
        check(itvOf(q).isBounded() && itvOf(q).isValid(), "validity : 100 / (t[3] - t[1]) is bounded" + p);
        inclusion("waveform reads", o, precision);
    }
}

static void rotations()
{
    section("the rotation oscillator");
    for (int precision : {1, 2}) {
        const std::string p = precision == 1 ? " (float)" : " (double)";
        Tree f     = slider("lfo" + p, 1.6, 0.1, 10);
        Tree theta = sigMul(sigReal(2 * M_PI / 48000), f);
        Tree c = sigCos(theta), s = sigSin(theta);
        Tree id = tree(unique("rot"));
        Tree x1 = late(sigProj(0, ref(id))), y1 = late(sigProj(1, ref(id)));
        // x = c x' + s y' ; y = c y' - s x' + (1 - 1')
        Tree bx  = sigAdd(sigMul(y1, s), sigMul(x1, c));
        Tree by  = sigAdd(sigAdd(sigMul(y1, c), sigMul(x1, sigSub(sigInt(0), s))), impulse());
        Tree grp = rec(id, list2(bx, by));
        Tree x = sigProj(0, grp), y = sigProj(1, grp);
        Tree o = outs({x, y, sigIntCast(sigMin(sigInt(511), sigMax(sigInt(0), sigAdd(sigReal(48.5), sigMul(sigReal(25), x)))))});
        sigs::g.gCertifiedSamples.clear();
        itv::programPrecision() = precision;
        typeAnnotation(o, false);
        const double r = std::max(std::abs(itvOf(x).lo()), std::abs(itvOf(x).hi()));
        if (precision == 1) {
            check(r <= 2.001, "precision : the radius is certified below 2" + p);
            bool   dated   = sigs::g.gCertifiedSamples.size() == 1;
            double samples = dated ? sigs::g.gCertifiedSamples.begin()->second : 0;
            check(dated && samples > 1e6 && samples < 2e6,
                  "certificate : about 30 seconds at 48 kHz in single precision" + p);
        } else {
            check(r <= 1.001, "precision : the radius stays near 1 over the horizon" + p);
            check(sigs::g.gCertifiedSamples.empty(), "certificate : none in double precision" + p);
        }
        inclusion("rotation", o, precision, 4096);
    }
}

static void firForms()
{
    section("the forms of -fir");
    for (int precision : {1, 2}) {
        const std::string p = precision == 1 ? " (float)" : " (double)";
        // a dummy recursive projection names the IIR's variable
        Tree id = tree(unique("firvar"));
        Tree vi = sigProj(0, ref(id));
        rec(id, list1(sigInt(0)));
        // the one-pole smoother IIR[Vi, (1 - a) A, 0, a], a written -1 and -1.0 on both sides
        Tree sr   = slider("sr" + p, 48000, 50, 192000);
        Tree a    = sigExp(sigMul(sigInt(-1), sigDiv(sigReal(1000), sr)));
        Tree a2   = sigExp(sigMul(sigReal(-1.0), sigDiv(sigReal(1000), sr)));
        Tree A    = slider("freq" + p, 200, 200, 800);
        Tree iir  = sigIIR({vi, sigMul(sigSub(one(), a2), A), sigInt(0), a});
        Tree inv  = sigDiv(sr, iir);
        // the integer accumulator IIR[Vi, 1, 0, 1] and a table generated from it
        Tree cnt  = sigIIR({vi, sigInt(1), sigInt(0), sigInt(1)});
        Tree gen  = sigSin(sigMul(sigReal(2 * M_PI / 65536), sigFloatCast(sigSub(cnt, sigInt(1)))));
        Tree tbl  = sigRDTbl(sigWRTbl(sigInt(65536), sigGen(gen)), sigInt(100));
        // the impulse FIR[1, 1, -1] and an n-ary sum with a negation
        Tree imp  = sigFIR({sigInt(1), sigInt(1), sigInt(-1)});
        Tree inc  = slider("inc" + p, 0.01, 0, 0.5);
        Tree ph   = rec1("firphase", [&](Tree y) {
            Tree x = sigSum({y, inc});
            return sigSum({y, inc, sigMul(sigInt(-1), sigFloor(x))});
        });
        Tree o = outs({iir, inv, tbl, imp, ph});
        itv::programPrecision() = precision;
        typeAnnotation(o, false);
        check(itvOf(iir).lo() > 0 && itvOf(iir).hi() < 801, "precision : IIR smoother within ((1-a) 200, 800]" + p);
        check(itvOf(inv).isBounded() && itvOf(inv).isValid(), "validity : sr / IIR smoother is bounded" + p);
        check(itvOf(tbl).isValid() && itvOf(tbl).lo() >= -1.01 && itvOf(tbl).hi() <= 1.01,
              "precision : a table generated from the integer accumulator is a sine" + p);
        check(itvOf(ph).lo() > -1e-3 && itvOf(ph).hi() < 1 + 1e-3, "precision : the Sum[] phase within [0, 1]" + p);
        inclusion("-fir forms", o, precision);
    }
}

static void nodes()
{
    section("the compound nodes frac, cfrac, mix, tpt");
    for (int precision : {1, 2}) {
        const std::string p = precision == 1 ? " (float)" : " (double)";
        // a phasor written with frac : the operand is a sum, no margin
        Tree inc   = slider("ninc" + p, 0.01, 0.0, 0.5);
        Tree phase = rec1("nphase", [&](Tree y) { return sigFrac(sigAdd(y, inc)); });
        // the fixed-point phasor : frac of 2^30 cfrac(d), a product by a power of 2, exact
        Tree d     = sigMul(slider("nf" + p, 1, 0, 10), sigReal(1.0 / 50));
        Tree fixed = sigFrac(sigMul(sigReal(1073741824.0), sigCfrac(d)));
        Tree bits  = sigIntCast(sigMul(sigReal(16777216.0), fixed));
        // frac of an arbitrary product : the margin of a contraction stays
        Tree prod  = sigFrac(sigMul(slider("na" + p, 3, 1, 1000), slider("nb" + p, 2, 1, 1000)));
        // a smoother written with mix, and a mix beyond its premise
        Tree s      = slider("ns" + p, 0, -12, 12);
        Tree smooth = rec1("nsmooth", [&](Tree y) { return sigMix(sigReal(0.999), s, y); });
        Tree wide   = sigMix(slider("nc" + p, 0.5, -1, 2), slider("nu" + p, 0, 0, 1), slider("nv" + p, 1, 0, 1));
        // the gain of a trapezoidal integrator, g / (1 + g), and its pole crossed
        Tree gain   = sigTpt(slider("ng" + p, 1, 0, 1000));
        Tree pole   = sigTpt(slider("nh" + p, 1, -2, 2));
        Tree o      = outs({phase, fixed, bits, prod, smooth, wide, gain, pole});
        itv::programPrecision() = precision;
        typeAnnotation(o, false);
        const double below = precision == 1 ? double(std::nextafter(1.0f, 0.0f)) : std::nextafter(1.0, 0.0);
        check(itvOf(phase).lo() == 0 && itvOf(phase).hi() == below && itvOf(phase).isValid(),
              "precision : frac(y' + inc) is exactly [0, 1[" + p);
        // cfrac(d) keeps the margin of d = f / 50 (a contractible product) : the operand may be
        // slightly negative, so frac may reach 1 ; the power of 2 itself adds no margin
        check(itvOf(fixed).lo() == 0 && itvOf(fixed).hi() == 1,
              "precision : frac of 2^30 cfrac(d) is exactly [0, 1] (a power of 2 does not round)" + p);
        check(itvOf(bits).isValid() && itvOf(bits).hi() <= 16777216,
              "validity : int(2^24 frac(2^30 cfrac(d))) is valid and below 2^24" + p);
        check(itvOf(prod).lo() < 0 && itvOf(prod).hi() > 1,
              "precision : frac of an arbitrary product keeps the margin of a contraction" + p);
        check(itvOf(smooth).lo() > -12.1 && itvOf(smooth).hi() < 12.1, "precision : mix(0.999, s, y') stays in [-12, 12]" + p);
        check(itvOf(wide).lo() < -0.5 && itvOf(wide).hi() > 1.5, "precision : beyond c in [0, 1], mix is its definition" + p);
        check(itvOf(gain).lo() <= 0 && itvOf(gain).lo() > -1e-6 && itvOf(gain).hi() < 1 && itvOf(gain).isValid(),
              "precision : tpt(g) for g in [0, 1000] lies in [0, 1[" + p);
        check(!itvOf(pole).isBounded() || !itvOf(pole).isValid(),
              "precision : tpt(g) for g reaching -1 is its definition, unbounded" + p);
        inclusion("compound nodes", o, precision);
        // a transformation rebuilds the nodes as such : the default definition of the
        // algebra (x - floor(x), ...) must not lower them on the way
        Tree             flat = outs({fixed, prod, wide, gain});
        TransformAlgebra identity;
        check(signalTransform(flat, identity) == flat, "a transformation keeps frac, cfrac, mix and tpt" + p);
        TreeAlgebra B;
        // the exact attributes of a compound node are those of its definition written out
        {
            Tree ic = sigIntCast(slider("ni" + p, 1, 0, 9));  // an integer operand too
            Tree u  = slider("nu" + p, 0, 0, 1);
            std::vector<std::pair<Tree, Tree>> pairs;
            for (Tree x : {u, ic}) {
                pairs.push_back({sigFrac(x), B.Sub(x, B.Floor(x))});
                pairs.push_back({sigCfrac(x), B.Sub(x, B.Rint(x))});
                pairs.push_back({sigTpt(x), B.Div(x, B.Add(x, B.FloatNum(1.0)))});
                pairs.push_back({sigMix(x, u, ic), B.Add(B.Mul(B.Sub(B.FloatNum(1.0), x), u), B.Mul(x, ic))});
            }
            std::vector<Tree> all;
            for (const auto& q : pairs) all.push_back(q.first), all.push_back(q.second);
            Tree list = nil();
            for (Tree t : all) list = cons(t, list);
            typeAnnotation(list, false);
            bool same = true;
            for (const auto& q : pairs) {
                Type t1 = getCertifiedSigType(q.first), t2 = getCertifiedSigType(q.second);
                same = same && t1->nature() == t2->nature() && t1->variability() == t2->variability() &&
                       t1->computability() == t2->computability() &&
                       t1->vectorability() == t2->vectorability() && t1->boolean() == t2->boolean();
            }
            check(same, "the exact attributes of the compound nodes are those of their definitions" + p);
        }
        // the correspondence with FaustAlgebra : a node's children are exactly the
        // arguments of its operation, in order
        {
            Tree x = slider("nx" + p, 0, 0, 1), c = slider("nc2" + p, 0, 0, 1), a = slider("na" + p, 0, 0, 1);
            Tree f = sigFrac(x), r = sigCfrac(x), t = sigTpt(x), m = sigMix(c, a, x);
            check(f->arity() == 1 && f->branch(0) == x && r->arity() == 1 && r->branch(0) == x &&
                      t->arity() == 1 && t->branch(0) == x && m->arity() == 3 && m->branch(0) == c &&
                      m->branch(1) == a && m->branch(2) == x && B.Frac(x) == f && B.Cfrac(x) == r &&
                      B.Tpt(x) == t && B.Mix(c, a, x) == m,
                  "the children of a compound node are the arguments of its operation" + p);
        }
    }
}

static void kernels()
{
    section("the kernels sum, fir, iir");
    for (int precision : {1, 2}) {
        const std::string p = precision == 1 ? " (float)" : " (double)";
        TreeAlgebra       B;
        Tree              xs = slider("kx" + p, 0, -1, 1);
        Tree              xp = slider("kp" + p, 0.5, 0, 1);
        // the pink noise filter : stable, constant coefficients, a gain near 8300
        Tree pink = B.Iir(xs, {sigReal(2.494956002), sigReal(-2.017265875), sigReal(0.522189400)});
        // a smoother of a nonnegative input, and an integrator (no gain)
        Tree smooth = B.Iir(xp, {sigReal(0.99)});
        Tree integ  = B.Iir(xs, {sigReal(1.0)});
        Tree fir    = B.Fir(xp, {sigReal(1.0), sigReal(0.0), sigReal(-2.0)});
        Tree sum    = B.Sum({xp, sigReal(2.0), xs});
        Tree o      = outs({pink, smooth, fir, sum});
        itv::programPrecision() = precision;
        typeAnnotation(outs({pink, smooth, integ, fir, sum}), false);
        check(itvOf(pink).isValid() && itvOf(pink).lo() > -8500 && itvOf(pink).hi() < 8500,
              "precision : the pink noise filter of [-1, 1] is within its certified gain" + p);
        check(itvOf(smooth).isValid() && itvOf(smooth).lo() > -1e-6 && itvOf(smooth).hi() < 100.01,
              "precision : a smoother of [0, 1] by 0.99 is [0, 100]" + p);
        check(!itvOf(integ).isValid() && !itvOf(integ).isBounded(), "validity : a float integrator may be anything" + p);
        check(itvOf(fir).lo() <= -2 && itvOf(fir).lo() > -2.001 && itvOf(fir).hi() >= 1 && itvOf(fir).hi() < 1.001,
              "precision : fir of [0, 1] by (1, 0, -2) is [-2, 1]" + p);
        check(itvOf(sum).lo() <= 1 && itvOf(sum).lo() > 0.999 && itvOf(sum).hi() >= 4 && itvOf(sum).hi() < 4.001,
              "precision : sum of [0,1], 2, [-1,1] is [1, 4]" + p);
        inclusion("kernels", o, precision);
        // the nodes are the operations : a transformation keeps them, the tree algebra
        // builds them with the layout of the reveal (IIR[nil, x, 0, C1, ...])
        TransformAlgebra identity;
        check(signalTransform(o, identity) == o, "a transformation keeps sum, fir and iir" + p);
        check(fir == sigFIR({xp, sigReal(1.0), sigReal(0.0), sigReal(-2.0)}) &&
                  smooth == sigIIR({nil(), xp, sigInt(0), sigReal(0.99)}) &&
                  sum == sigSum({xp, sigReal(2.0), xs}),
              "the kernel nodes are the operations of FaustAlgebra" + p);
    }
}

static void invalidities()
{
    section("what must stay invalid");
    for (int precision : {1, 2}) {
        const std::string p = precision == 1 ? " (float)" : " (double)";
        // a bend down to 1.19e-7 : SR / (440 bend) passes 2^31 at 192 kHz (3.7e9), int()
        // of it is undefined
        Tree b    = slider("bend" + p, 1, 1.19e-7, 10);
        Tree over = sigIntCast(sigDiv(sigReal(192000), sigMul(sigReal(440), b)));
        // a division by a value that may be zero, unguarded : bounded but infinite
        Tree z    = slider("z" + p, 1, 0, 1);
        Tree d0   = sigSub(sigDiv(one(), z), sigDiv(one(), z));
        Tree o    = outs({over, d0});
        itv::programPrecision() = precision;
        typeAnnotation(o, false);
        check(!itvOf(over).isValid(), "validity : int() of a value past 2^31 stays invalid" + p);
        check(!itvOf(d0).isValid(), "validity : 1/z - 1/z with z in [0, 1] may be inf - inf" + p);
        inclusion("invalid cases", o, precision);
    }
}

static void costs()
{
    section("cost");
    // a long chain of sums with no rounding term and no mix : the filters keep it linear
    Tree s = sigInput(0);
    for (int i = 0; i < 1000; i++) s = sigAdd(sigMul(sigReal(0.5 + i * 1e-4), late(s, 1 + i % 7)), sigInput(0));
    auto t0 = std::chrono::steady_clock::now();
    itv::programPrecision() = 2;
    typeAnnotation(outs({s}), false);
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    check(secs < 5, "cost : a chain of 1000 sums is typed in " + std::to_string(secs) + " s");
}

int main()
{
    tlib::init();
    sigs::init();
    registerPrimitives();
    remainders();
    mixes();
    selects();
    slowPoles();
    tables();
    rotations();
    firForms();
    nodes();
    kernels();
    invalidities();
    costs();
    std::cout << (gFailed ? "FAILED" : "PASSED") << " (" << gFailed << " failure(s))" << std::endl;
    return gFailed ? 1 : 0;
}
