/************************************************************************
 ************************************************************************
    FAUST signal library
    Copyright (C) 2003-2026 GRAME, Centre National de Creation Musicale
    SPDX-License-Identifier: LGPL-2.1-or-later
 ************************************************************************
 ************************************************************************/

#include "sigFold.hh"

#include <string>

#include "tlib-error.hh"

namespace sigs {

namespace {

[[noreturn]] void invalidNode(const std::string& reason)
{
    tlib::error("SignalNodeView: " + reason);
}

void requireArity(Tree signal, int expected)
{
    if (signal->arity() != expected) {
        invalidNode("invalid constructor arity");
    }
}

}  // namespace

//--------------------------------------------------------------------------
// Public API: signal node decoding
//--------------------------------------------------------------------------

/**
 * Validate a symbol-headed signal node and decode its registered opcode.
 *
 * Numeric atoms are handled directly by foldSignal and are therefore not
 * valid inputs. Untagged symbols, foreign signatures and malformed opcodes are
 * reported explicitly.
 */
SignalNodeView::SignalNodeView(Tree signal) : fSignal(signal), fOpcode(SignalOpcode::Input)
{
    if (!signal) {
        invalidNode("null signal");
    }
    if (signal->node().type() != kSymNode) {
        invalidNode("constructor head is not a symbol");
    }

    SymbolTag tag;
    if (!getSymbolTag(signal->node().getSym(), tag)) {
        invalidNode("untagged constructor");
    }
    if (tag.signature != signalSignature().identity()) {
        invalidNode("constructor belongs to another signature");
    }

    const std::uint8_t localOpcode = tag.localOpcode();
    if (localOpcode >= static_cast<std::uint8_t>(SignalOpcode::Count)) {
        invalidNode("unknown signal opcode");
    }
    fOpcode = static_cast<SignalOpcode>(localOpcode);
}

/** Return the number of branches that are signals rather than metadata. */
std::size_t SignalNodeView::childCount() const
{
    switch (fOpcode) {
        case SignalOpcode::Input:
            requireArity(fSignal, 1);
            return 0;
        case SignalOpcode::BinOp:
            requireArity(fSignal, 3);
            return 2;
        case SignalOpcode::IntCast:
        case SignalOpcode::BitCast:
        case SignalOpcode::FloatCast:
        case SignalOpcode::Delay1:
            requireArity(fSignal, 1);
            return 1;
        case SignalOpcode::Delay:
            requireArity(fSignal, 2);
            return 2;
        default:
            break;
    }
    invalidNode("constructor layout is not implemented");
}

/**
 * Return semantic child \p index after validating the constructor arity.
 * Metadata branches are never exposed through this accessor.
 */
Tree SignalNodeView::child(std::size_t index) const
{
    const std::size_t count = childCount();
    if (index >= count) {
        invalidNode("semantic child index out of range");
    }
    return (fOpcode == SignalOpcode::BinOp) ? fSignal->branch(int(index + 1))
                                            : fSignal->branch(int(index));
}

/**
 * Return integer metadata \p index after validating its representation.
 * The current subset uses it for input channels and SOperator values.
 */
int SignalNodeView::integerMetadata(std::size_t index) const
{
    if (index != 0 || (fOpcode != SignalOpcode::Input && fOpcode != SignalOpcode::BinOp)) {
        invalidNode("integer metadata index out of range");
    }
    childCount();  // Validate the complete constructor layout first.
    Tree metadata = fSignal->branch(0);
    if (metadata->node().type() != kIntNode || metadata->arity() != 0) {
        invalidNode("integer metadata has an invalid representation");
    }
    return metadata->node().getInt();
}

}  // namespace sigs
