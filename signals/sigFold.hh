/************************************************************************
 ************************************************************************
    FAUST signal library
    Copyright (C) 2003-2026 GRAME, Centre National de Creation Musicale
    SPDX-License-Identifier: LGPL-2.1-or-later
 ************************************************************************
 ************************************************************************/

/** \file sigFold.hh
 * Experimental algebraic fold over the first non-recursive signal subset.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>

#include "binop.hh"
#include "sigOpcode.hh"
#include "sigs-export.hh"
#include "tree.hh"

namespace sigs {

//--------------------------------------------------------------------------
// Public API: signal node decoding and experimental fold
//--------------------------------------------------------------------------

/**
 * Separate semantic children from static metadata in a signal constructor.
 *
 * The existing Tree layout is intentionally preserved: for example SIGBINOP
 * stores its SOperator in branch 0 and its two signals in branches 1 and 2.
 * The view validates this multisorted layout before a fold interprets it.
 */
class SIGS_API SignalNodeView {
   private:
    Tree         fSignal;
    SignalOpcode fOpcode;

   public:
    /**
     * Validate a symbol-headed signal node and decode its registered opcode.
     *
     * Numeric atoms are handled directly by foldSignal and are therefore not
     * valid inputs. Untagged symbols, foreign signatures and malformed
     * opcodes are reported explicitly.
     */
    explicit SignalNodeView(Tree signal);

    Tree source() const { return fSignal; }
    SignalOpcode opcode() const { return fOpcode; }

    /** Return the number of branches that are signals rather than metadata. */
    std::size_t childCount() const;

    /**
     * Return semantic child \p index after validating the constructor arity.
     * Metadata branches are never exposed through this accessor.
     */
    Tree child(std::size_t index) const;

    /**
     * Return integer metadata \p index after validating its representation.
     * The current subset uses it for input channels and SOperator values.
     */
    int integerMetadata(std::size_t index) const;
};

/**
 * Report sharing behavior without exposing or persisting the fold memo.
 * foldSignal resets both counters before each evaluation.
 */
struct SignalFoldReport {
    std::size_t evaluations = 0;
    std::size_t cacheHits   = 0;
};

namespace detail {

/**
 * One fold invocation owns one local memo, so shared sub-signals are evaluated
 * once without leaving analysis properties attached to long-lived Trees.
 */
template <typename Result, typename Algebra>
class SignalFolder {
   private:
    Algebra&                         fAlgebra;
    SignalFoldReport&                fReport;
    std::unordered_map<Tree, Result> fMemo;

    Result foldBinary(const SignalNodeView& view)
    {
        const Result x = fold(view.child(0));
        const Result y = fold(view.child(1));
        switch (view.integerMetadata(0)) {
            case kAdd:
                return fAlgebra.Add(x, y);
            case kSub:
                return fAlgebra.Sub(x, y);
            case kMul:
                return fAlgebra.Mul(x, y);
            case kDiv:
                return fAlgebra.Div(x, y);
            default:
                throw std::runtime_error("foldSignal: binary operation not in the initial subset");
        }
    }

    Result foldUncached(Tree signal)
    {
        // Non-symbol values are atoms in the tree signature. Reject a Tree
        // that reuses such a Node as a constructor head with branches.
        if (signal->node().type() != kSymNode && signal->arity() != 0) {
            throw std::runtime_error("foldSignal: non-symbol atom has branches");
        }
        switch (signal->node().type()) {
            case kIntNode:
                return fAlgebra.IntNum(signal->node().getInt());
            case kInt64Node:
                return fAlgebra.Int64Num(signal->node().getInt64());
            case kDoubleNode:
                return fAlgebra.FloatNum(signal->node().getDouble());
            default: {
                SignalNodeView view(signal);
                if (view.opcode() != SignalOpcode::BinOp) {
                    throw std::runtime_error(
                        "foldSignal: constructor not in the initial numeric subset");
                }
                return foldBinary(view);
            }
        }
    }

   public:
    SignalFolder(Algebra& algebra, SignalFoldReport& report)
        : fAlgebra(algebra), fReport(report)
    {
    }

    Result fold(Tree signal)
    {
        auto cached = fMemo.find(signal);
        if (cached != fMemo.end()) {
            ++fReport.cacheHits;
            return cached->second;
        }

        ++fReport.evaluations;
        Result result = foldUncached(signal);
        fMemo.emplace(signal, result);
        return result;
    }
};

}  // namespace detail

/**
 * Fold an acyclic numeric signal into \p Result using \p algebra.
 *
 * This first vertical slice accepts integer/real atoms and SIGBINOP with Add,
 * Sub, Mul or Div. It dispatches through registered signature/opcode tags,
 * memoizes by Tree identity, and fails explicitly on every unsupported form.
 * Algebra is intentionally duck-typed during the experiment; it must provide
 * IntNum, Int64Num, FloatNum, Add, Sub, Mul and Div.
 */
template <typename Result, typename Algebra>
Result foldSignal(Tree signal, Algebra& algebra, SignalFoldReport* report = nullptr)
{
    if (!signal) {
        throw std::runtime_error("foldSignal: null signal");
    }
    SignalFoldReport localReport;
    SignalFoldReport& activeReport = report ? *report : localReport;
    activeReport                    = {};
    detail::SignalFolder<Result, Algebra> folder(algebra, activeReport);
    return folder.fold(signal);
}

}  // namespace sigs
