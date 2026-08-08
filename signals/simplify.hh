#pragma once

#include "signals.hh"

// SIGLIB seam file -- exists ONLY in the standalone copy, never in
// faust/compiler/signals. In the compiler build, "simplify.hh" resolves to
// the normalize layer's full rewriting-based simplification, which the
// FIR/IIR algebra uses (through its isRecFree guard) to fold coefficient
// expressions. The standalone library has no normalize layer : this shim
// keeps the algebra buildable with simplification as the identity.
// Consequence : coefficient folding is weaker here (a merged kernel may
// keep 0.25+0.125 unfolded where the compiler gets 0.375) -- less complete,
// never wrong.
inline Tree simplify(Tree t)
{
    return t;
}
