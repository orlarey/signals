/************************************************************************
 ************************************************************************
    FAUST signal library
    Copyright (C) 2003-2026 GRAME
    ---------------------------------------------------------------------
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License.
 ************************************************************************
 ************************************************************************/

#include "sigTypeAlgebra.hh"

#include <sstream>

#include "interval.hh"
#include "sigs-state.hh"
#include "tlib-error.hh"

namespace {

// Keep construction of legacy Type values in one place so each algebra rule
// states only the semantic promotions that distinguish that operation.
Type constantType(int nature, const interval& values)
{
    return makeSimpleType(nature, kKonst, kComp, kVect, kNum, values);
}

Type binaryType(const Type& x, const Type& y, const interval& values)
{
    return castInterval(x | y, values);
}

[[noreturn]] Type unsupported(const char* operation)
{
    std::ostringstream message;
    message << "SigTypeAlgebra: operation " << operation
            << " has not been migrated from the legacy type inference system";
    tlib::error(message.str());
}

// This first algebra deliberately implements a closed non-recursive subset.
// Explicit failures make coverage gaps visible while the legacy typer remains
// the reference implementation during the migration.
class SigTypeAlgebra final : public FaustAlgebra<Type> {
   public:
    Type Nil() override { return new TupletType(); }
    Type IntNum(int x) override { return constantType(kInt, gAlgebra.IntNum(x)); }
    Type Int64Num(int64_t x) override { return constantType(kInt, gAlgebra.Int64Num(x)); }
    Type FloatNum(double x) override { return constantType(kReal, gAlgebra.FloatNum(x)); }

    // Labels carry syntax rather than signal values, so a neutral empty tuple
    // lets UI rules accept them without inventing an audio type.
    Type Label(const std::string&) override { return Nil(); }

    // The attribute engine, not the local algebra, will own recursive state
    // and the convergence predicate.
    Type FixPointUpdate(const Type&, const Type&) override
    {
        return unsupported("FixPointUpdate");
    }

    Type Input(const Type&) override { return sigs::g.TINPUT; }
    Type Output(const Type&, const Type& x) override { return sampCast(x); }

    Type Button(const Type&) override
    {
        return castInterval(sigs::g.TGUI, gAlgebra.Button(interval(0)));
    }

    Type Checkbox(const Type&) override
    {
        return castInterval(sigs::g.TGUI, gAlgebra.Checkbox(interval(0)));
    }

    Type VSlider(const Type&, const Type& init, const Type& lo, const Type& hi,
                 const Type& step) override
    {
        return castInterval(sigs::g.TGUI,
                            gAlgebra.VSlider(interval(0), init->getInterval(), lo->getInterval(),
                                             hi->getInterval(), step->getInterval()));
    }

    Type HSlider(const Type&, const Type& init, const Type& lo, const Type& hi,
                 const Type& step) override
    {
        return castInterval(sigs::g.TGUI,
                            gAlgebra.HSlider(interval(0), init->getInterval(), lo->getInterval(),
                                             hi->getInterval(), step->getInterval()));
    }

    Type HBargraph(const Type&, const Type&, const Type&) override
    {
        return unsupported("HBargraph");
    }

    Type VBargraph(const Type&, const Type&, const Type&) override
    {
        return unsupported("VBargraph");
    }

    Type NumEntry(const Type&, const Type& init, const Type& lo, const Type& hi,
                  const Type& step) override
    {
        return castInterval(sigs::g.TGUI,
                            gAlgebra.NumEntry(interval(0), init->getInterval(), lo->getInterval(),
                                              hi->getInterval(), step->getInterval()));
    }

    // Attach evaluates its second argument only for effects and keeps the
    // value and therefore the type of its first argument.
    Type Attach(const Type& x, const Type&) override { return x; }

    Type Abs(const Type& x) override
    {
        return castInterval(x, gAlgebra.Abs(x->getInterval()));
    }

    Type Highest(const Type&) override { return unsupported("Highest"); }
    Type Lowest(const Type&) override { return unsupported("Lowest"); }

    Type Add(const Type& x, const Type& y) override
    {
        return binaryType(x, y, gAlgebra.Add(x->getInterval(), y->getInterval()));
    }

    Type Sub(const Type& x, const Type& y) override
    {
        return binaryType(x, y, gAlgebra.Sub(x->getInterval(), y->getInterval()));
    }

    Type Mul(const Type& x, const Type& y) override
    {
        return binaryType(x, y, gAlgebra.Mul(x->getInterval(), y->getInterval()));
    }

    Type Div(const Type& x, const Type& y) override
    {
        return floatCast(binaryType(x, y, gAlgebra.Div(x->getInterval(), y->getInterval())));
    }

    Type Inv(const Type&) override { return unsupported("Inv"); }

    Type Neg(const Type& x) override
    {
        return castInterval(x, gAlgebra.Neg(x->getInterval()));
    }

    Type Mod(const Type& x, const Type& y) override
    {
        return binaryType(x, y, gAlgebra.Mod(x->getInterval(), y->getInterval()));
    }

    Type Acos(const Type&) override { return unsupported("Acos"); }
    Type Acosh(const Type&) override { return unsupported("Acosh"); }

    Type And(const Type& x, const Type& y) override
    {
        return intCast(binaryType(x, y, gAlgebra.And(x->getInterval(), y->getInterval())));
    }

    Type Asin(const Type&) override { return unsupported("Asin"); }
    Type Asinh(const Type&) override { return unsupported("Asinh"); }
    Type Atan(const Type&) override { return unsupported("Atan"); }
    Type Atan2(const Type&, const Type&) override { return unsupported("Atan2"); }
    Type Atanh(const Type&) override { return unsupported("Atanh"); }
    Type Ceil(const Type&) override { return unsupported("Ceil"); }
    Type Cos(const Type&) override { return unsupported("Cos"); }
    Type Cosh(const Type&) override { return unsupported("Cosh"); }

    Type Eq(const Type& x, const Type& y) override
    {
        return boolCast(binaryType(x, y, gAlgebra.Eq(x->getInterval(), y->getInterval())));
    }

    Type Exp(const Type&) override { return unsupported("Exp"); }
    Type FloatCast(const Type& x) override { return floatCast(x); }
    Type BitCast(const Type& x) override { return bitCast(x); }
    Type Floor(const Type&) override { return unsupported("Floor"); }

    Type Ge(const Type& x, const Type& y) override
    {
        return boolCast(binaryType(x, y, gAlgebra.Ge(x->getInterval(), y->getInterval())));
    }

    Type Gt(const Type& x, const Type& y) override
    {
        return boolCast(binaryType(x, y, gAlgebra.Gt(x->getInterval(), y->getInterval())));
    }

    Type IntCast(const Type& x) override { return intCast(x); }

    Type Le(const Type& x, const Type& y) override
    {
        return boolCast(binaryType(x, y, gAlgebra.Le(x->getInterval(), y->getInterval())));
    }

    Type Log(const Type&) override { return unsupported("Log"); }
    Type Log10(const Type&) override { return unsupported("Log10"); }

    Type Lsh(const Type& x, const Type& y) override
    {
        return intCast(binaryType(x, y, gAlgebra.Lsh(x->getInterval(), y->getInterval())));
    }

    Type Lt(const Type& x, const Type& y) override
    {
        return boolCast(binaryType(x, y, gAlgebra.Lt(x->getInterval(), y->getInterval())));
    }

    Type Max(const Type& x, const Type& y) override
    {
        return binaryType(x, y, gAlgebra.Max(x->getInterval(), y->getInterval()));
    }

    Type Min(const Type& x, const Type& y) override
    {
        return binaryType(x, y, gAlgebra.Min(x->getInterval(), y->getInterval()));
    }

    Type Ne(const Type& x, const Type& y) override
    {
        return boolCast(binaryType(x, y, gAlgebra.Ne(x->getInterval(), y->getInterval())));
    }

    Type Not(const Type&) override { return unsupported("Not"); }

    Type Or(const Type& x, const Type& y) override
    {
        return intCast(binaryType(x, y, gAlgebra.Or(x->getInterval(), y->getInterval())));
    }

    Type Pow(const Type&, const Type&) override { return unsupported("Pow"); }
    Type Remainder(const Type&) override { return unsupported("Remainder"); }
    Type Rint(const Type&) override { return unsupported("Rint"); }
    Type Round(const Type&) override { return unsupported("Round"); }

    Type Rsh(const Type& x, const Type& y) override
    {
        return intCast(binaryType(x, y, gAlgebra.Rsh(x->getInterval(), y->getInterval())));
    }

    Type Select2(const Type& selector, const Type& x, const Type& y) override
    {
        SimpleType* sx = isSimpleType(x);
        SimpleType* sy = isSimpleType(y);
        TLIB_ASSERT(sx && sy);
        return makeSimpleType(
            sx->nature() | sy->nature(),
            sx->variability() | sy->variability() | selector->variability(),
            sx->computability() | sy->computability() | selector->computability(),
            sx->vectorability() | sy->vectorability() | selector->vectorability(),
            sx->boolean() | sy->boolean(), itv::reunion(sx->getInterval(), sy->getInterval()));
    }

    Type Sin(const Type&) override { return unsupported("Sin"); }
    Type Sinh(const Type&) override { return unsupported("Sinh"); }
    Type Sqrt(const Type&) override { return unsupported("Sqrt"); }
    Type Tan(const Type&) override { return unsupported("Tan"); }
    Type Tanh(const Type&) override { return unsupported("Tanh"); }

    Type Xor(const Type& x, const Type& y) override
    {
        return intCast(binaryType(x, y, gAlgebra.Xor(x->getInterval(), y->getInterval())));
    }

    Type Mem(const Type& x) override
    {
        return castInterval(sampCast(x), itv::reunion(x->getInterval(), interval(0)));
    }

    Type Delay(const Type& x, const Type&) override
    {
        return castInterval(sampCast(x), itv::reunion(x->getInterval(), interval(0)));
    }

    Type Prefix(const Type& x, const Type& y) override
    {
        checkInit(x);
        return castInterval(sampCast(x | y), itv::reunion(x->getInterval(), y->getInterval()));
    }

    Type RDTbl(const Type&, const Type&) override { return unsupported("RDTbl"); }
    Type WRTbl(const Type&, const Type&, const Type&, const Type&) override
    {
        return unsupported("WRTbl");
    }

    Type Gen(const Type& x) override { return x; }
    Type SoundFile(const Type&) override { return unsupported("SoundFile"); }
    Type SoundFileRate(const Type&, const Type&) override
    {
        return unsupported("SoundFileRate");
    }
    Type SoundFileLength(const Type&, const Type&) override
    {
        return unsupported("SoundFileLength");
    }
    Type SoundFileBuffer(const Type&, const Type&, const Type&, const Type&) override
    {
        return unsupported("SoundFileBuffer");
    }
    Type Waveform(const std::vector<Type>&) override { return unsupported("Waveform"); }

    Type ForeignFunction(const std::vector<Type>&) override
    {
        return unsupported("ForeignFunction");
    }
    Type ForeignVar(const Type&, const Type&, const Type&) override
    {
        return unsupported("ForeignVar");
    }
    Type ForeignConst(const Type&, const Type&, const Type&) override
    {
        return unsupported("ForeignConst");
    }
};

}  // namespace

//--------------------------------------------------------------------------
// Public API: experimental signal type algebra
//--------------------------------------------------------------------------

/**
 * Return the experimental Faust algebra whose carrier is the existing signal
 * Type. The first implementation covers constants, inputs, outputs, numeric
 * binary operations, casts, and basic user-interface elements with the same
 * local rules as the legacy type inference system.
 *
 * The algebra computes only local typing rules: traversal, recursive
 * dependencies, fixed-point iteration, causality checks, and Tree annotation
 * belong to the future attribute engine. Operations not yet migrated report
 * an explicit error instead of returning an unsound placeholder type.
 *
 * The returned stateless singleton remains owned by the signal library and is
 * valid until process termination.
 */
FaustAlgebra<Type>& sigTypeAlgebra()
{
    // Function-local construction avoids static initialization order issues
    // with the signal-library state used by individual algebra operations.
    static SigTypeAlgebra algebra;
    return algebra;
}

