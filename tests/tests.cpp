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

    std::cout << (gFailed ? "FAILED" : "PASSED") << " (" << gFailed << " failure(s))" << std::endl;
    return gFailed ? 1 : 0;
}
