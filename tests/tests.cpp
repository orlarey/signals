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

#include <cmath>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "bottom-up-attributes.hh"
#include "interval_algebra.hh"
#include "ppsig.hh"
#include "sigs-config.hh"
#include "sigCoreTypeAlgebra.hh"
#include "sigtype.hh"
#include "sigtyperules.hh"
#include "sigScalarize.hh"
#include "signals.hh"
#include "tlib.hh"

static int gFailed = 0;

namespace {

Sym gAttrAdd = nullptr;
Sym gAttrMin = nullptr;
Sym gAttrAverage = nullptr;

struct IntegerTreeEvaluator {
    int operator()(Tree node, const std::vector<int>& inputs) const
    {
        int value = 0;
        if (isInt(node->node(), &value)) {
            return value;
        }

        Tree id;
        Tree body;
        if (isRec(node, id, body)) {
            return inputs.at(0);
        }

        Tree x;
        Tree y;
        if (isTree(node, Node(gAttrAdd), x, y)) {
            return inputs.at(0) + inputs.at(1);
        }
        if (isTree(node, Node(gAttrMin), x, y)) {
            return std::min(inputs.at(0), inputs.at(1));
        }
        throw std::runtime_error("unsupported node in IntegerTreeEvaluator");
    }
};

struct ExactIntegerFixpoint {
    int initial(Tree) const { return 0; }
    bool reached(Tree, int previous, int current) const { return previous == current; }
};

using IntegerAttributes = sigs::attributes::BottomUpAttributes<
    int, IntegerTreeEvaluator, sigs::attributes::SymbolicTreeDependencies,
    ExactIntegerFixpoint>;

struct RealTreeEvaluator {
    double operator()(Tree node, const std::vector<double>& inputs) const
    {
        int value = 0;
        if (isInt(node->node(), &value)) {
            return double(value);
        }

        Tree id;
        Tree body;
        if (isRec(node, id, body)) {
            return inputs.at(0);
        }

        Tree x;
        Tree y;
        if (isTree(node, Node(gAttrAverage), x, y)) {
            return (inputs.at(0) + inputs.at(1)) / 2.0;
        }
        throw std::runtime_error("unsupported node in RealTreeEvaluator");
    }
};

struct ApproximateRealFixpoint {
    double initial(Tree) const { return 0.0; }
    bool reached(Tree, double previous, double current) const
    {
        return std::abs(previous - current) < 1e-6;
    }
};

using RealAttributes = sigs::attributes::BottomUpAttributes<
    double, RealTreeEvaluator, sigs::attributes::SymbolicTreeDependencies,
    ApproximateRealFixpoint>;

struct GroupedIntegerEvaluator {
    int operator()(const sigs::attributes::SignalAttributeNode& node,
                   const std::vector<int>& inputs) const
    {
        if (node.isRecursiveProjection()) {
            return inputs.at(0);
        }

        int value = 0;
        if (isInt(node.tree->node(), &value)) {
            return value;
        }

        int  projection = -1;
        Tree group;
        if (isProj(node.tree, &projection, group)) {
            return inputs.at(0);
        }

        Tree x;
        Tree y;
        if (isTree(node.tree, Node(gAttrAdd), x, y)) {
            return inputs.at(0) + inputs.at(1);
        }
        if (isTree(node.tree, Node(gAttrMin), x, y)) {
            return std::min(inputs.at(0), inputs.at(1));
        }
        throw std::runtime_error("unsupported node in GroupedIntegerEvaluator");
    }
};

struct ExactGroupedIntegerFixpoint {
    int initial(const sigs::attributes::SignalAttributeNode&) const { return 0; }
    bool reached(const sigs::attributes::SignalAttributeNode&, int previous, int current) const
    {
        return previous == current;
    }
};

using GroupedIntegerAttributes = sigs::attributes::BottomUpAttributes<
    int, GroupedIntegerEvaluator, sigs::attributes::SignalProjectionDependencies,
    ExactGroupedIntegerFixpoint>;

struct IntervalSignalEvaluator {
    itv::interval_algebra algebra;

    itv::interval operator()(const sigs::attributes::SignalAttributeNode& node,
                             const std::vector<itv::interval>& inputs)
    {
        if (node.isRecursiveProjection()) {
            return inputs.at(0);
        }

        int integer = 0;
        if (isSigInt(node.tree, &integer)) {
            return algebra.IntNum(integer);
        }
        double real = 0;
        if (isSigReal(node.tree, &real)) {
            return algebra.FloatNum(real);
        }
        int channel = 0;
        if (isSigInput(node.tree, &channel)) {
            return {-1.0, 1.0};
        }

        int  projection = -1;
        Tree group;
        if (isProj(node.tree, &projection, group)) {
            return inputs.at(0);
        }

        Tree label;
        Tree init;
        Tree lo;
        Tree hi;
        Tree step;
        if (isSigVSlider(node.tree, label, init, lo, hi, step)) {
            return algebra.VSlider(itv::interval(0), inputs.at(0), inputs.at(1), inputs.at(2),
                                   inputs.at(3));
        }

        Tree x;
        Tree y;
        int  operation = 0;
        if (isSigBinOp(node.tree, &operation, x, y)) {
            switch (operation) {
                case kAdd:
                    return algebra.Add(inputs.at(0), inputs.at(1));
                case kMul:
                    return algebra.Mul(inputs.at(0), inputs.at(1));
                default:
                    throw std::runtime_error("unsupported binary operation in interval prototype");
            }
        }
        throw std::runtime_error("unsupported signal in IntervalSignalEvaluator");
    }
};

struct IntervalEqualityPolicy {
    itv::interval initial(const sigs::attributes::SignalAttributeNode&) const
    {
        return itv::empty();
    }
    bool reached(const sigs::attributes::SignalAttributeNode&, const itv::interval& previous,
                 const itv::interval& current) const
    {
        return (previous.isEmpty() && current.isEmpty()) ||
               (previous.lo() == current.lo() && previous.hi() == current.hi());
    }
};

using IntervalAttributes = sigs::attributes::BottomUpAttributes<
    itv::interval, IntervalSignalEvaluator, sigs::attributes::SignalProjectionDependencies,
    IntervalEqualityPolicy>;

struct IntervalEnclosure {
    itv::interval lower;
    itv::interval upper;
    std::size_t   iteration = 0;
};

struct IntervalEnclosureEvaluator {
    itv::interval_algebra algebra;

    IntervalEnclosure operator()(const sigs::attributes::SignalAttributeNode& node,
                                 const std::vector<IntervalEnclosure>& inputs)
    {
        if (node.isRecursiveProjection()) {
            IntervalEnclosure result = inputs.at(0);
            ++result.iteration;
            return result;
        }

        int integer = 0;
        if (isSigInt(node.tree, &integer)) {
            itv::interval value = algebra.IntNum(integer);
            return {value, value, 0};
        }
        double real = 0;
        if (isSigReal(node.tree, &real)) {
            itv::interval value = algebra.FloatNum(real);
            return {value, value, 0};
        }
        int channel = 0;
        if (isSigInput(node.tree, &channel)) {
            return {itv::interval(-1.0, 1.0), itv::interval(-1.0, 1.0), 0};
        }

        int  projection = -1;
        Tree group;
        if (isProj(node.tree, &projection, group)) {
            return inputs.at(0);
        }

        Tree x;
        Tree y;
        int  operation = 0;
        if (isSigBinOp(node.tree, &operation, x, y)) {
            const std::size_t iteration =
                std::max(inputs.at(0).iteration, inputs.at(1).iteration);
            switch (operation) {
                case kAdd:
                    return {algebra.Add(inputs.at(0).lower, inputs.at(1).lower),
                            algebra.Add(inputs.at(0).upper, inputs.at(1).upper), iteration};
                case kMul:
                    return {algebra.Mul(inputs.at(0).lower, inputs.at(1).lower),
                            algebra.Mul(inputs.at(0).upper, inputs.at(1).upper), iteration};
                default:
                    throw std::runtime_error(
                        "unsupported binary operation in interval enclosure prototype");
            }
        }
        throw std::runtime_error("unsupported signal in IntervalEnclosureEvaluator");
    }
};

struct IntervalEnclosureTermination {
    static constexpr std::size_t kLimit = 8;

    IntervalEnclosure initial(const sigs::attributes::SignalAttributeNode&) const
    {
        return {itv::interval(0.0), itv::interval(-4.0, 4.0), 0};
    }

    bool reached(const sigs::attributes::SignalAttributeNode&, const IntervalEnclosure&,
                 const IntervalEnclosure& current) const
    {
        const bool exact = current.lower.lo() == current.upper.lo() &&
                           current.lower.hi() == current.upper.hi();
        return exact || current.iteration >= kLimit;
    }
};

using EnclosedIntervalAttributes = sigs::attributes::BottomUpAttributes<
    IntervalEnclosure, IntervalEnclosureEvaluator,
    sigs::attributes::SignalProjectionDependencies, IntervalEnclosureTermination>;

// Prototype of the Tree-valued Faust dispatcher used by the reconstruction
// law. treeRewrite supplies the structural recursion, fresh recursive binders
// and memoization; this rule reconstructs the Faust primitives used by the
// test after their children have been rebuilt.
struct TreeFaustRebuildRule {
    std::size_t applications = 0;

    Tree operator()(Tree signal)
    {
        ++applications;

        int integer = 0;
        if (isSigInt(signal, &integer)) {
            return sigInt(integer);
        }
        double real = 0;
        if (isSigReal(signal, &real)) {
            return sigReal(real);
        }
        int channel = 0;
        if (isSigInput(signal, &channel)) {
            return sigInput(channel);
        }

        int  projection = -1;
        Tree group;
        if (isProj(signal, &projection, group)) {
            return sigProj(projection, group);
        }

        Tree x;
        Tree y;
        int  operation = 0;
        if (isSigBinOp(signal, &operation, x, y)) {
            switch (operation) {
                case kAdd:
                    return sigAdd(x, y);
                case kMul:
                    return sigMul(x, y);
                default:
                    throw std::runtime_error("unsupported binary operation in Tree reconstruction");
            }
        }

        Tree label;
        Tree init;
        Tree lo;
        Tree hi;
        Tree step;
        if (isSigVSlider(signal, label, init, lo, hi, step)) {
            return sigVSlider(label, init, lo, hi, step);
        }

        // Lists, labels and other structural TLIB nodes have already been
        // rebuilt congruentially by treeRewrite and are left untouched.
        return signal;
    }
};

// Compare only finite structural qualities so interval and resolution data
// cannot accidentally participate in CoreType validation.
bool matchesLegacyCore(const CoreType& core, const Type& legacy)
{
    if (!core.isScalar() || !legacy) {
        return false;
    }

    const CoreNature nature =
        (legacy->nature() == kInt) ? CoreNature::Integer : CoreNature::Real;
    const CoreVariability variability =
        (legacy->variability() == kKonst)
            ? CoreVariability::Constant
            : ((legacy->variability() == kBlock) ? CoreVariability::Block
                                                  : CoreVariability::Sample);
    const CoreComputability computability =
        (legacy->computability() == kComp)
            ? CoreComputability::Compile
            : ((legacy->computability() == kInit) ? CoreComputability::Init
                                                   : CoreComputability::Execute);
    const CoreVectorability vectorability =
        (legacy->vectorability() == kVect)
            ? CoreVectorability::Vector
            : ((legacy->vectorability() == kScal) ? CoreVectorability::Scalar
                                                   : CoreVectorability::TrueScalar);
    const CoreBoolean boolean =
        (legacy->boolean() == kNum) ? CoreBoolean::Numeric : CoreBoolean::Boolean;

    return core.nature == nature && core.variability == variability &&
           core.computability == computability && core.vectorability == vectorability &&
           core.boolean == boolean;
}

}  // namespace

static void check(bool ok, const std::string& what)
{
    if (ok) {
        std::cout << "OK   : " << what << std::endl;
    } else {
        std::cout << "FAIL : " << what << std::endl;
        gFailed++;
    }
}

int main()
{
    tlib::init();
    sigs::init();
    gAttrAdd = symbol("ATTR_ADD");
    gAttrMin = symbol("ATTR_MIN");
    gAttrAverage = symbol("ATTR_AVERAGE");

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

    // Rebuild the structural part only, keeping interval inference outside the
    // carrier so the legacy typer remains an independent comparison oracle.
    FaustAlgebra<CoreType>& coreAlgebra = sigCoreTypeAlgebra();
    CoreType coreInput = coreAlgebra.Input(coreAlgebra.IntNum(0));
    CoreType coreHalf  = coreAlgebra.FloatNum(0.5);
    CoreType coreSum   = coreAlgebra.Add(coreInput, coreHalf);
    CoreType coreSlider = coreAlgebra.VSlider(
        coreAlgebra.Label("level"), coreHalf, coreAlgebra.IntNum(0), coreAlgebra.IntNum(1),
        coreAlgebra.FloatNum(0.01));
    CoreType coreProduct = coreAlgebra.Mul(coreSum, coreSlider);

    check(matchesLegacyCore(coreSlider, ts),
          "core type algebra: slider matches legacy structural inference");
    check(matchesLegacyCore(coreProduct, t),
          "core type algebra: input expression matches legacy structural inference");

    CoreType coreComparison =
        coreAlgebra.Lt(coreAlgebra.IntNum(1), coreAlgebra.FloatNum(2.0));
    Tree comparison = sigLT(sigInt(1), sigReal(2.0));
    typeAnnotation(comparison, false);
    check(matchesLegacyCore(coreComparison, getCertifiedSigType(comparison)),
          "core type algebra: comparison matches legacy structural inference");

    // Bargraphs preserve the displayed signal qualities and impose at least
    // block variability; the fourth algebra operand makes that rule possible.
    CoreType coreBargraph =
        coreAlgebra.HBargraph(coreAlgebra.Label("meter"), coreAlgebra.IntNum(-1),
                              coreAlgebra.IntNum(1), coreAlgebra.FloatNum(0.25));
    Tree bargraph = sigHBargraph(tree("\"meter\""), sigInt(-1), sigInt(1), sigReal(0.25));
    typeAnnotation(bargraph, false);
    check(matchesLegacyCore(coreBargraph, getCertifiedSigType(bargraph)),
          "core type algebra: bargraph matches legacy structural inference");

    CoreType coreControlled = coreAlgebra.Control(coreInput, coreAlgebra.Button({}));
    CoreType coreBounded =
        coreAlgebra.AssertBounds(coreAlgebra.IntNum(-1), coreAlgebra.IntNum(1), coreControlled);
    check(coreBounded == coreInput,
          "core type algebra: control and asserted bounds preserve structural type");

    CoreType coreExp10 = coreAlgebra.Exp10(coreAlgebra.IntNum(2));
    CoreType coreRemainder =
        coreAlgebra.Remainder(coreAlgebra.IntNum(5), coreAlgebra.IntNum(2));
    CoreType coreARsh = coreAlgebra.ARsh(coreAlgebra.IntNum(-8), coreAlgebra.IntNum(1));
    CoreType coreLRsh = coreAlgebra.LRsh(coreAlgebra.IntNum(-8), coreAlgebra.IntNum(1));
    check(coreExp10.nature == CoreNature::Real && coreRemainder.nature == CoreNature::Real &&
              coreARsh.nature == CoreNature::Integer && coreLRsh.nature == CoreNature::Integer,
          "core type algebra: added numeric operations have their structural nature");

    interval bargraphRange =
        gAlgebra.HBargraph(interval(0), interval(-1), interval(1), interval(-0.5, 0.5));
    interval boundedRange =
        gAlgebra.AssertBounds(interval(-1), interval(1), interval(-2, 2));
    interval exp10Range = gAlgebra.Exp10(interval(2));
    check(bargraphRange.lo() == -0.5 && bargraphRange.hi() == 0.5 &&
              boundedRange.lo() == -1 && boundedRange.hi() == 1 && exp10Range.lo() == 100 &&
              exp10Range.hi() == 100,
          "interval algebra: added operations preserve and refine ranges");

    // --- recursive pretty-printing ----------------------------------------
    Tree recVar  = tree("R");
    Tree recBody = cons(sigAdd(sigInput(0), ref(recVar)), ::nil());
    Tree recSig  = sigProj(0, rec(recVar, recBody));
    std::ostringstream recOut;
    recOut << ppsig(recSig);
    check(recOut.str() == "proj0(R)\nwith {\n  R := (+(IN[0],R))\n}",
          "ppsig: symbolic recursion is printed as a trailing definition: " + recOut.str());

    Tree nestedVar  = tree("S");
    Tree nestedBody = cons(sigAdd(ref(recVar), ref(nestedVar)), ::nil());
    Tree nestedRec  = sigProj(0, rec(nestedVar, nestedBody));
    Tree outerBody  = cons(sigAdd(ref(recVar), nestedRec), ::nil());
    Tree nestedSig  = sigProj(0, rec(recVar, outerBody));
    std::ostringstream nestedOut;
    nestedOut << ppsig(nestedSig);
    check(nestedOut.str() ==
              "proj0(R)\nwith {\n  R := (+(R,proj0(S)))\n  S := (+(R,S))\n}",
          "ppsig: nested recursive definitions are collected once: " + nestedOut.str());

    // --- generic bottom-up attributes -------------------------------------
    Tree shared = tree(gAttrAdd, tree(1), tree(2));
    Tree dag    = tree(gAttrAdd, shared, shared);
    IntegerAttributes dagAttributes {IntegerTreeEvaluator(),
                                     sigs::attributes::SymbolicTreeDependencies(),
                                     ExactIntegerFixpoint()};
    check(dagAttributes.evaluate(dag) == 6,
          "bottom-up attributes: acyclic evaluation");
    check(dagAttributes.report().evaluations_per_node.at(shared) == 1,
          "bottom-up attributes: shared subtree evaluated once");
    check(dagAttributes.report().cyclic_components == 0,
          "bottom-up attributes: acyclic graph has no cyclic component");

    // R = min(R + 1, 3). ref(attrVar) and rec(attrVar, ...) are the same
    // symbolic hash-consed node; RECDEF closes the dependency cycle.
    Tree attrVar = tree("ATTR_R");
    Tree attrRec = ref(attrVar);
    Tree attrBody = tree(gAttrMin, tree(gAttrAdd, attrRec, tree(1)), tree(3));
    check(rec(attrVar, attrBody) == attrRec,
          "bottom-up attributes: symbolic recursion reuses the same Tree");

    IntegerAttributes recursiveAttributes {IntegerTreeEvaluator(),
                                           sigs::attributes::SymbolicTreeDependencies(),
                                           ExactIntegerFixpoint()};
    check(recursiveAttributes.evaluate(attrRec) == 3,
          "bottom-up attributes: recursive least fixed point is 3");
    check(recursiveAttributes.report().cyclic_components == 1,
          "bottom-up attributes: recursive dependency detected by Tarjan");
    check(recursiveAttributes.report().evaluations_per_node.at(attrRec) > 1,
          "bottom-up attributes: only the cyclic component is iterated");

    // X = (X + 2) / 2 converges asymptotically to 2: exact equality is not a
    // suitable stopping condition, whereas the supplied predicate is.
    Tree realVar  = tree("ATTR_X");
    Tree realRec  = ref(realVar);
    Tree realBody = tree(gAttrAverage, realRec, tree(2));
    rec(realVar, realBody);
    RealAttributes approximateAttributes {RealTreeEvaluator(),
                                          sigs::attributes::SymbolicTreeDependencies(),
                                          ApproximateRealFixpoint()};
    const double approximateResult = approximateAttributes.evaluate(realRec);
    check(std::abs(approximateResult - 2.0) < 1e-5,
          "bottom-up attributes: configurable approximate fixed-point predicate");

    // RG is a group of three signals. RG[0] is constant, RG[1] only depends
    // on RG[0], and RG[2] is genuinely recursive. Only projection 2 should
    // belong to a cyclic component.
    Tree groupVar = tree("ATTR_GROUP");
    Tree groupRef = ref(groupVar);
    Tree groupP0  = sigProj(0, groupRef);
    Tree groupP1  = sigProj(1, groupRef);
    Tree groupP2  = sigProj(2, groupRef);
    Tree groupDef = list3(tree(1), tree(gAttrAdd, groupP0, tree(1)),
                          tree(gAttrMin, tree(gAttrAdd, groupP2, tree(1)), tree(3)));
    rec(groupVar, groupDef);

    Tree groupRoot = tree(gAttrAdd, tree(gAttrAdd, groupP0, groupP1), groupP2);
    GroupedIntegerAttributes groupAttributes {
        GroupedIntegerEvaluator(), sigs::attributes::SignalProjectionDependencies(),
        ExactGroupedIntegerFixpoint()};
    check(groupAttributes.evaluate(
              sigs::attributes::SignalAttributeNode::expression(groupRoot)) == 6,
          "bottom-up attributes: recursive signal group evaluates to 1 + 2 + 3");
    check(groupAttributes.report().cyclic_components == 1,
          "bottom-up attributes: only a genuinely recursive projection forms a cycle");

    const auto p0 = sigs::attributes::SignalAttributeNode::recursiveProjection(groupRef, 0);
    const auto p1 = sigs::attributes::SignalAttributeNode::recursiveProjection(groupRef, 1);
    const auto p2 = sigs::attributes::SignalAttributeNode::recursiveProjection(groupRef, 2);
    check(groupAttributes.report().evaluations_per_node.at(p0) == 1 &&
              groupAttributes.report().evaluations_per_node.at(p1) == 1,
          "bottom-up attributes: acyclic group projections are evaluated once");
    check(groupAttributes.report().evaluations_per_node.at(p2) > 1,
          "bottom-up attributes: recursive group projection is iterated");

    // A Faust input ranges over [-1, 1]. A gain slider ranges over [0, 1],
    // therefore their product still ranges over [-1, 1].
    Tree gain = sigVSlider(tree("gain"), sigReal(0.5), sigReal(0.0), sigReal(1.0),
                           sigReal(0.01));
    Tree audioInput = sigInput(0);
    Tree amplified  = sigMul(audioInput, gain);
    IntervalAttributes directIntervals {
        IntervalSignalEvaluator(), sigs::attributes::SignalProjectionDependencies(),
        IntervalEqualityPolicy()};
    const itv::interval directRange = directIntervals.evaluate(
        sigs::attributes::SignalAttributeNode::expression(amplified));
    const itv::interval inputRange = directIntervals.attribute(
        sigs::attributes::SignalAttributeNode::expression(audioInput));
    const itv::interval gainRange = directIntervals.attribute(
        sigs::attributes::SignalAttributeNode::expression(gain));
    check(inputRange.lo() == -1.0 && inputRange.hi() == 1.0,
          "bottom-up intervals: Faust input is [-1,1]");
    check(gainRange.lo() == 0.0 && gainRange.hi() == 1.0,
          "bottom-up intervals: gain slider is [0,1]");
    check(directRange.lo() == -1.0 && directRange.hi() == 1.0,
          "bottom-up intervals: input [-1,1] times gain [0,1] is [-1,1]");

    // The same analysis through a Faust recursive group:
    //   G0 = input(0), G1 = gain, G2 = G0 * G1.
    Tree intervalGroupVar = tree("INTERVAL_GROUP");
    Tree intervalGroupRef = ref(intervalGroupVar);
    Tree intervalP0       = sigProj(0, intervalGroupRef);
    Tree intervalP1       = sigProj(1, intervalGroupRef);
    Tree intervalP2       = sigProj(2, intervalGroupRef);
    rec(intervalGroupVar, list3(sigInput(0), gain, sigMul(intervalP0, intervalP1)));

    IntervalAttributes groupedIntervals {
        IntervalSignalEvaluator(), sigs::attributes::SignalProjectionDependencies(),
        IntervalEqualityPolicy()};
    const itv::interval groupedRange = groupedIntervals.evaluate(
        sigs::attributes::SignalAttributeNode::expression(intervalP2));
    check(groupedRange.lo() == -1.0 && groupedRange.hi() == 1.0,
          "bottom-up intervals: projected group gain signal is [-1,1]");
    check(groupedIntervals.report().cyclic_components == 0,
          "bottom-up intervals: projection dependencies without feedback stay acyclic");

    // X = input + 0.5 * X has the interval fixed point [-2, 2]. The lower
    // approximation grows from [0,0], the upper approximation shrinks from
    // [-4,4], and the iteration stops at its explicit threshold.
    Tree enclosureVar = tree("INTERVAL_ENCLOSURE");
    Tree enclosureRef = ref(enclosureVar);
    Tree enclosureProj = sigProj(0, enclosureRef);
    Tree enclosureBody = list1(sigAdd(sigInput(0), sigMul(sigReal(0.5), enclosureProj)));
    rec(enclosureVar, enclosureBody);

    EnclosedIntervalAttributes enclosureAnalysis {
        IntervalEnclosureEvaluator(), sigs::attributes::SignalProjectionDependencies(),
        IntervalEnclosureTermination()};
    enclosureAnalysis.evaluate(sigs::attributes::SignalAttributeNode::expression(enclosureProj));
    const IntervalEnclosure enclosure = enclosureAnalysis.attribute(
        sigs::attributes::SignalAttributeNode::recursiveProjection(enclosureRef, 0));
    check(enclosure.lower.lo() >= enclosure.upper.lo() &&
              enclosure.lower.hi() <= enclosure.upper.hi(),
          "interval enclosure: lower approximation remains inside upper approximation");
    check(enclosure.upper.lo() <= -2.0 && enclosure.upper.hi() >= 2.0,
          "interval enclosure: upper approximation safely contains [-2,2]");
    check(enclosure.upper.size() < 8.0,
          "interval enclosure: upper approximation is tighter than its initial bound");
    check(enclosure.iteration >= IntervalEnclosureTermination::kLimit,
          "interval enclosure: termination is carried by the attribute counter");
    check(enclosureAnalysis.report().cyclic_components == 1,
          "interval enclosure: recursive equation forms one cyclic component");

    // --- recursive groups scalarized to singleton groups ------------------
    Tree scalarVar = tree("SCALAR_GROUP");
    Tree scalarRef = ref(scalarVar);
    Tree scalarP0  = sigProj(0, scalarRef);
    Tree scalarP1  = sigProj(1, scalarRef);
    Tree scalarShared = sigMul(sigInput(0), sigReal(0.25));
    Tree scalarBody = list2(sigAdd(scalarShared, scalarP1),
                            sigMul(scalarP0, scalarShared));
    Tree scalarGroup = rec(scalarVar, scalarBody);
    Tree scalarSource = sigAdd(sigProj(0, scalarGroup), sigProj(1, scalarGroup));

    check(!isSigScalarized(scalarSource),
          "scalarization: two-component source is not in singleton normal form");
    Tree scalarResult = sigScalarize(scalarSource);

    std::cout << "\n=== Scalarization: before ===\n"
              << ppsig(scalarSource)
              << "\n=== Scalarization: after ===\n"
              << ppsig(scalarResult)
              << "\n=== End scalarization ===\n"
              << std::endl;

    check(isSigScalarized(scalarResult),
          "scalarization: every recursive group is a singleton");

    Tree scalarSourceId        = nullptr;
    Tree scalarSourceBodyAfter = nullptr;
    check(isRec(scalarGroup, scalarSourceId, scalarSourceBodyAfter) &&
              scalarSourceBodyAfter == scalarBody,
          "scalarization: source RECDEF is unchanged");

    int  scalarRootOp = 0;
    Tree scalarResultP0 = nullptr;
    Tree scalarResultP1 = nullptr;
    int  scalarIndex0 = -1;
    int  scalarIndex1 = -1;
    Tree scalarGroup0 = nullptr;
    Tree scalarGroup1 = nullptr;
    check(isSigBinOp(scalarResult, &scalarRootOp, scalarResultP0, scalarResultP1) &&
              scalarRootOp == kAdd &&
              isProj(scalarResultP0, &scalarIndex0, scalarGroup0) && scalarIndex0 == 0 &&
              isProj(scalarResultP1, &scalarIndex1, scalarGroup1) && scalarIndex1 == 0 &&
              scalarGroup0 != scalarGroup1,
          "scalarization: source projections target two distinct singleton groups");

    Tree scalarId0   = nullptr;
    Tree scalarId1   = nullptr;
    Tree scalarBody0 = nullptr;
    Tree scalarBody1 = nullptr;
    check(isRec(scalarGroup0, scalarId0, scalarBody0) && len(scalarBody0) == 1 &&
              isRec(scalarGroup1, scalarId1, scalarBody1) && len(scalarBody1) == 1,
          "scalarization: both target RECDEF properties contain one signal");

    int  scalarDef0Op = 0;
    int  scalarDef1Op = 0;
    Tree scalarShared0     = nullptr;
    Tree scalarCross1      = nullptr;
    Tree scalarCross0      = nullptr;
    Tree scalarShared1     = nullptr;
    Tree scalarCrossGroup1 = nullptr;
    Tree scalarCrossGroup0 = nullptr;
    check(isSigBinOp(nth(scalarBody0, 0), &scalarDef0Op, scalarShared0, scalarCross1) &&
              scalarDef0Op == kAdd &&
              isProj(scalarCross1, &scalarIndex0, scalarCrossGroup1) && scalarIndex0 == 0 &&
              scalarCrossGroup1 == scalarGroup1 &&
              isSigBinOp(nth(scalarBody1, 0), &scalarDef1Op, scalarCross0, scalarShared1) &&
              scalarDef1Op == kMul &&
              isProj(scalarCross0, &scalarIndex1, scalarCrossGroup0) && scalarIndex1 == 0 &&
              scalarCrossGroup0 == scalarGroup0,
          "scalarization: mutual dependencies are rewritten simultaneously");
    check(scalarShared0 == scalarShared1 && scalarShared0 == scalarShared,
          "scalarization: non-recursive sharing is preserved");

    Tree scalarResult2 = sigScalarize(scalarResult);
    check(isSigScalarized(scalarResult2) && areEquiv(scalarResult2, scalarResult),
          "scalarization: singleton normal form is stable modulo alpha-renaming");

    Tree scalarListSource = list2(sigProj(0, scalarGroup), sigProj(1, scalarGroup));
    Tree scalarListResult = sigScalarize(scalarListSource);
    int  scalarListIndex0 = -1;
    int  scalarListIndex1 = -1;
    Tree scalarListGroup0 = nullptr;
    Tree scalarListGroup1 = nullptr;
    check(isList(scalarListResult) && len(scalarListResult) == 2 &&
              isProj(nth(scalarListResult, 0), &scalarListIndex0, scalarListGroup0) &&
              scalarListIndex0 == 0 &&
              isProj(nth(scalarListResult, 1), &scalarListIndex1, scalarListGroup1) &&
              scalarListIndex1 == 0 && scalarListGroup0 != scalarListGroup1 &&
              isSigScalarized(scalarListResult),
          "scalarization: a list of signals shares one scalarized group family");

    // --- algebraic reconstruction modulo alpha-equivalence ----------------
    // The outer group contains a shared input/gain product, dependencies
    // between its projections and a nested recursive group which itself
    // refers to the outer binder. treeRewrite must freshen both binders,
    // preserve sharing, and leave both source RECDEF properties untouched.
    Tree foldOuterVar = tree("FOLD_OUTER");
    Tree foldOuterRef = ref(foldOuterVar);
    Tree foldInnerVar = tree("FOLD_INNER");
    Tree foldInnerRef = ref(foldInnerVar);

    Tree foldGain = sigVSlider(tree("fold_gain"), sigReal(0.5), sigReal(0.0),
                               sigReal(1.0), sigReal(0.01));
    Tree foldShared = sigMul(sigInput(0), foldGain);

    Tree foldInnerP0 = sigProj(0, foldInnerRef);
    Tree foldInnerP1 = sigProj(1, foldInnerRef);
    Tree foldInnerBody = list2(sigAdd(sigProj(0, foldOuterRef), foldInnerP1),
                               sigMul(foldInnerP0, foldShared));
    Tree foldInnerGroup = rec(foldInnerVar, foldInnerBody);
    Tree foldNested     = sigProj(0, foldInnerGroup);

    Tree foldOuterP0 = sigProj(0, foldOuterRef);
    Tree foldOuterP1 = sigProj(1, foldOuterRef);
    Tree foldOuterP2 = sigProj(2, foldOuterRef);
    Tree foldOuterBody = list3(foldShared, sigAdd(foldOuterP0, foldNested),
                               sigMul(foldOuterP1, foldOuterP2));
    Tree foldOuterGroup = rec(foldOuterVar, foldOuterBody);
    Tree foldSource = sigAdd(sigProj(1, foldOuterGroup), sigProj(2, foldOuterGroup));

    TreeFaustRebuildRule rebuildRule;
    Tree rebuilt = treeRewrite(foldSource, rebuildRule);
    check(rebuilt != foldSource,
          "algebraic reconstruction: recursive binders are freshly renamed");
    check(areEquiv(rebuilt, foldSource),
          "algebraic reconstruction: complex recursive signal is alpha-equivalent");
    check(rebuildRule.applications > 0,
          "algebraic reconstruction: Faust dispatcher was applied");

    Tree sourceOuterId;
    Tree sourceOuterBodyAfter;
    Tree sourceInnerId;
    Tree sourceInnerBodyAfter;
    check(isRec(foldOuterGroup, sourceOuterId, sourceOuterBodyAfter) &&
              sourceOuterBodyAfter == foldOuterBody,
          "algebraic reconstruction: outer source RECDEF is unchanged");
    check(isRec(foldInnerGroup, sourceInnerId, sourceInnerBodyAfter) &&
              sourceInnerBodyAfter == foldInnerBody,
          "algebraic reconstruction: inner source RECDEF is unchanged");

    int  rebuiltProjection = -1;
    Tree rebuiltOuterGroup = nullptr;
    Tree rebuiltLeft       = nullptr;
    Tree rebuiltRight      = nullptr;
    int  rebuiltRootOp = 0;
    check(isSigBinOp(rebuilt, &rebuiltRootOp, rebuiltLeft, rebuiltRight) &&
              rebuiltRootOp == kAdd &&
              isProj(rebuiltLeft, &rebuiltProjection, rebuiltOuterGroup) &&
              rebuiltOuterGroup != foldOuterGroup,
          "algebraic reconstruction: outer group uses a fresh symbolic node");

    Tree rebuiltOuterId   = nullptr;
    Tree rebuiltOuterBody = nullptr;
    check(isRec(rebuiltOuterGroup, rebuiltOuterId, rebuiltOuterBody) &&
              len(rebuiltOuterBody) == 3,
          "algebraic reconstruction: rebuilt outer group has three components");

    Tree rebuiltOuterSecond     = nth(rebuiltOuterBody, 1);
    Tree rebuiltOuterP0         = nullptr;
    Tree rebuiltNestedProjection = nullptr;
    int  rebuiltSecondOp = 0;
    Tree rebuiltInnerGroup = nullptr;
    check(isSigBinOp(rebuiltOuterSecond, &rebuiltSecondOp, rebuiltOuterP0,
                     rebuiltNestedProjection) &&
              rebuiltSecondOp == kAdd &&
              isProj(rebuiltNestedProjection, &rebuiltProjection, rebuiltInnerGroup) &&
              rebuiltInnerGroup != foldInnerGroup,
          "algebraic reconstruction: nested group is freshly renamed");

    Tree rebuiltInnerId         = nullptr;
    Tree rebuiltInnerBody       = nullptr;
    Tree rebuiltInnerSecondLeft = nullptr;
    Tree rebuiltShared          = nullptr;
    int  rebuiltInnerSecondOp = 0;
    check(isRec(rebuiltInnerGroup, rebuiltInnerId, rebuiltInnerBody) &&
              isSigBinOp(nth(rebuiltInnerBody, 1), &rebuiltInnerSecondOp,
                         rebuiltInnerSecondLeft, rebuiltShared) &&
              rebuiltInnerSecondOp == kMul && rebuiltShared == nth(rebuiltOuterBody, 0),
          "algebraic reconstruction: sharing survives nested recursion");

    // --- integer signals ----------------------------------------------------
    Tree n = sigAdd(sigInt(1), sigInt(2));
    typeAnnotation(n, false);
    check(getCertifiedSigType(n)->nature() == kInt, "type: 1 + 2 is kInt");

    std::cout << (gFailed ? "FAILED" : "PASSED") << " (" << gFailed << " failure(s))" << std::endl;
    return gFailed ? 1 : 0;
}
