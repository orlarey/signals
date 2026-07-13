/************************************************************************
 ************************************************************************
    FAUST signal library
    Copyright (C) 2003-2026 GRAME
    ---------------------------------------------------------------------
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License.
 ************************************************************************
 ************************************************************************/

#include "sigCoreTypeAlgebra.hh"

#include <algorithm>
#include <sstream>
#include <utility>

#include "tlib-error.hh"

//--------------------------------------------------------------------------
// Public carrier contract: exact structural signal type
//--------------------------------------------------------------------------

// CoreType uses independent ordered enums so its exact finite lattice does
// not depend on the legacy AudioType representation or interval library.

/**
 * Exact structural signal type used independently of range analysis.
 * Scalar fields form finite ordered domains; elements reserve structural
 * content for tables and tuples without mixing numeric intervals into the
 * carrier. Bottom represents an attribute not computed yet.
 */

namespace {

// Construct scalar values centrally so local rules expose only the qualities
// they intentionally change.
CoreType scalar(CoreNature nature, CoreVariability variability = CoreVariability::Constant,
                CoreComputability computability = CoreComputability::Compile,
                CoreVectorability vectorability = CoreVectorability::Vector,
                CoreBoolean boolean = CoreBoolean::Numeric)
{
    return {CoreTypeShape::Scalar, nature, variability, computability, vectorability, boolean, {}};
}

CoreType tuple(std::vector<CoreType> elements)
{
    CoreType result;
    result.shape    = CoreTypeShape::Tuple;
    result.elements = std::move(elements);
    return result;
}

[[noreturn]] CoreType unsupported(const char* operation)
{
    std::ostringstream message;
    message << "SigCoreTypeAlgebra: operation " << operation
            << " has not been migrated from the legacy type inference system";
    tlib::error(message.str());
}

// Core qualities are ordered from the most specific to the most general, so
// joining two scalar dependencies is a component-wise maximum.
template <typename T>
T joinQuality(T x, T y)
{
    return std::max(x, y);
}

CoreType mergeScalar(const CoreType& x, const CoreType& y)
{
    if (!x.isScalar() || !y.isScalar()) {
        return unsupported("merge incompatible shapes");
    }
    return scalar(joinQuality(x.nature, y.nature),
                  joinQuality(x.variability, y.variability),
                  joinQuality(x.computability, y.computability),
                  joinQuality(x.vectorability, y.vectorability),
                  joinQuality(x.boolean, y.boolean));
}

CoreType withNature(CoreType type, CoreNature nature)
{
    if (!type.isScalar()) {
        return unsupported("cast a non-scalar value");
    }
    type.nature = nature;
    return type;
}

CoreType withVariability(CoreType type, CoreVariability variability)
{
    if (!type.isScalar()) {
        return unsupported("promote a non-scalar value");
    }
    type.variability = joinQuality(type.variability, variability);
    return type;
}

CoreType asBoolean(CoreType type)
{
    type         = withNature(std::move(type), CoreNature::Integer);
    type.boolean = CoreBoolean::Boolean;
    return type;
}

// This target algebra starts with the closed scalar subset needed to compare
// independent structural attributes against the legacy type inference system.
class SigCoreTypeAlgebra final : public FaustAlgebra<CoreType> {
   public:
    CoreType Nil() override { return tuple({}); }
    CoreType IntNum(int) override { return scalar(CoreNature::Integer); }
    CoreType Int64Num(int64_t) override { return scalar(CoreNature::Integer); }
    CoreType FloatNum(double) override { return scalar(CoreNature::Real); }

    // Labels and channel numbers are syntax consumed by their parent rule;
    // Bottom avoids inventing an audio type for those operands.
    CoreType Label(const std::string&) override { return {}; }

    // Recursive iteration belongs to the generic attribute engine rather than
    // to local constructor semantics.
    CoreType FixPointUpdate(const CoreType&, const CoreType&) override
    {
        return unsupported("FixPointUpdate");
    }

    CoreType Input(const CoreType&) override
    {
        return scalar(CoreNature::Real, CoreVariability::Sample, CoreComputability::Execute);
    }

    CoreType Output(const CoreType&, const CoreType& x) override
    {
        return withVariability(x, CoreVariability::Sample);
    }

    CoreType Button(const CoreType&) override { return guiType(); }
    CoreType Checkbox(const CoreType&) override { return guiType(); }

    CoreType VSlider(const CoreType&, const CoreType&, const CoreType&, const CoreType&,
                     const CoreType&) override
    {
        return guiType();
    }

    CoreType HSlider(const CoreType&, const CoreType&, const CoreType&, const CoreType&,
                     const CoreType&) override
    {
        return guiType();
    }

    // A bargraph preserves its displayed signal type and merely raises its
    // variability to the UI block rate when necessary.
    CoreType HBargraph(const CoreType&, const CoreType&, const CoreType&,
                       const CoreType& signal) override
    {
        return withVariability(signal, CoreVariability::Block);
    }

    CoreType VBargraph(const CoreType&, const CoreType&, const CoreType&,
                       const CoreType& signal) override
    {
        return withVariability(signal, CoreVariability::Block);
    }

    CoreType NumEntry(const CoreType&, const CoreType&, const CoreType&, const CoreType&,
                      const CoreType&) override
    {
        return guiType();
    }

    // Effect and control wrappers keep the structural type of their value
    // operand; their second operand affects execution, not the returned value.
    CoreType Attach(const CoreType& x, const CoreType&) override { return x; }
    CoreType Enable(const CoreType& x, const CoreType&) override { return x; }
    CoreType Control(const CoreType& x, const CoreType&) override { return x; }

    CoreType Abs(const CoreType& x) override { return x; }
    CoreType Highest(const CoreType&) override { return unsupported("Highest"); }
    CoreType Lowest(const CoreType&) override { return unsupported("Lowest"); }
    CoreType Add(const CoreType& x, const CoreType& y) override { return mergeScalar(x, y); }
    CoreType Sub(const CoreType& x, const CoreType& y) override { return mergeScalar(x, y); }
    CoreType Mul(const CoreType& x, const CoreType& y) override { return mergeScalar(x, y); }

    // Faust division is real even when both operands are integer.
    CoreType Div(const CoreType& x, const CoreType& y) override
    {
        return withNature(mergeScalar(x, y), CoreNature::Real);
    }

    CoreType Inv(const CoreType&) override { return unsupported("Inv"); }
    CoreType Neg(const CoreType& x) override { return x; }
    CoreType Mod(const CoreType& x, const CoreType& y) override { return mergeScalar(x, y); }
    CoreType Acos(const CoreType&) override { return unsupported("Acos"); }
    CoreType Acosh(const CoreType&) override { return unsupported("Acosh"); }
    CoreType And(const CoreType& x, const CoreType& y) override { return integerBinary(x, y); }
    CoreType Asin(const CoreType&) override { return unsupported("Asin"); }
    CoreType Asinh(const CoreType&) override { return unsupported("Asinh"); }
    CoreType Atan(const CoreType&) override { return unsupported("Atan"); }
    CoreType Atan2(const CoreType&, const CoreType&) override { return unsupported("Atan2"); }
    CoreType Atanh(const CoreType&) override { return unsupported("Atanh"); }
    CoreType Ceil(const CoreType&) override { return unsupported("Ceil"); }
    CoreType Cos(const CoreType&) override { return unsupported("Cos"); }
    CoreType Cosh(const CoreType&) override { return unsupported("Cosh"); }
    CoreType Eq(const CoreType& x, const CoreType& y) override { return comparison(x, y); }
    CoreType Exp(const CoreType&) override { return unsupported("Exp"); }
    CoreType Exp10(const CoreType& x) override { return withNature(x, CoreNature::Real); }
    CoreType FloatCast(const CoreType& x) override { return withNature(x, CoreNature::Real); }
    CoreType BitCast(const CoreType& x) override { return withNature(x, CoreNature::Integer); }
    CoreType Floor(const CoreType&) override { return unsupported("Floor"); }
    CoreType Ge(const CoreType& x, const CoreType& y) override { return comparison(x, y); }
    CoreType Gt(const CoreType& x, const CoreType& y) override { return comparison(x, y); }
    CoreType IntCast(const CoreType& x) override { return withNature(x, CoreNature::Integer); }
    CoreType Le(const CoreType& x, const CoreType& y) override { return comparison(x, y); }
    CoreType Log(const CoreType&) override { return unsupported("Log"); }
    CoreType Log10(const CoreType&) override { return unsupported("Log10"); }
    CoreType Lsh(const CoreType& x, const CoreType& y) override { return integerBinary(x, y); }
    CoreType Lt(const CoreType& x, const CoreType& y) override { return comparison(x, y); }
    CoreType Max(const CoreType& x, const CoreType& y) override { return mergeScalar(x, y); }
    CoreType Min(const CoreType& x, const CoreType& y) override { return mergeScalar(x, y); }
    CoreType Ne(const CoreType& x, const CoreType& y) override { return comparison(x, y); }
    CoreType Not(const CoreType&) override { return unsupported("Not"); }
    CoreType Or(const CoreType& x, const CoreType& y) override { return integerBinary(x, y); }
    CoreType Pow(const CoreType&, const CoreType&) override { return unsupported("Pow"); }
    CoreType Remainder(const CoreType& x, const CoreType& y) override
    {
        return withNature(mergeScalar(x, y), CoreNature::Real);
    }
    CoreType Rint(const CoreType&) override { return unsupported("Rint"); }
    CoreType Round(const CoreType&) override { return unsupported("Round"); }
    CoreType ARsh(const CoreType& x, const CoreType& y) override { return integerBinary(x, y); }
    CoreType LRsh(const CoreType& x, const CoreType& y) override { return integerBinary(x, y); }

    CoreType Select2(const CoreType& selector, const CoreType& x, const CoreType& y) override
    {
        CoreType result = mergeScalar(x, y);
        if (!selector.isScalar()) {
            return unsupported("Select2 selector shape");
        }
        // The selected values determine nature and booleanity; the selector
        // contributes only evaluation-time qualities.
        result.variability   = joinQuality(result.variability, selector.variability);
        result.computability = joinQuality(result.computability, selector.computability);
        result.vectorability = joinQuality(result.vectorability, selector.vectorability);
        return result;
    }

    CoreType Sin(const CoreType&) override { return unsupported("Sin"); }
    CoreType Sinh(const CoreType&) override { return unsupported("Sinh"); }
    CoreType Sqrt(const CoreType&) override { return unsupported("Sqrt"); }
    CoreType Tan(const CoreType&) override { return unsupported("Tan"); }
    CoreType Tanh(const CoreType&) override { return unsupported("Tanh"); }
    CoreType Xor(const CoreType& x, const CoreType& y) override { return integerBinary(x, y); }

    CoreType Mem(const CoreType& x) override
    {
        return withVariability(x, CoreVariability::Sample);
    }

    CoreType Delay(const CoreType& x, const CoreType&) override
    {
        return withVariability(x, CoreVariability::Sample);
    }

    CoreType Prefix(const CoreType& x, const CoreType& y) override
    {
        return withVariability(mergeScalar(x, y), CoreVariability::Sample);
    }

    // Bounds refine only the range domain; every finite structural quality is
    // inherited from the bounded signal.
    CoreType AssertBounds(const CoreType&, const CoreType&, const CoreType& x) override
    {
        return x;
    }

    CoreType RDTbl(const CoreType&, const CoreType&) override { return unsupported("RDTbl"); }
    CoreType WRTbl(const CoreType&, const CoreType&, const CoreType&, const CoreType&) override
    {
        return unsupported("WRTbl");
    }

    CoreType Gen(const CoreType& x) override { return x; }
    CoreType SoundFile(const CoreType&) override { return unsupported("SoundFile"); }
    CoreType SoundFileRate(const CoreType&, const CoreType&) override
    {
        return unsupported("SoundFileRate");
    }
    CoreType SoundFileLength(const CoreType&, const CoreType&) override
    {
        return unsupported("SoundFileLength");
    }
    CoreType SoundFileBuffer(const CoreType&, const CoreType&, const CoreType&,
                             const CoreType&) override
    {
        return unsupported("SoundFileBuffer");
    }
    CoreType Waveform(const std::vector<CoreType>&) override { return unsupported("Waveform"); }
    CoreType ForeignFunction(const std::vector<CoreType>&) override
    {
        return unsupported("ForeignFunction");
    }
    CoreType ForeignVar(const CoreType&, const CoreType&, const CoreType&) override
    {
        return unsupported("ForeignVar");
    }
    CoreType ForeignConst(const CoreType&, const CoreType&, const CoreType&) override
    {
        return unsupported("ForeignConst");
    }

   private:
    static CoreType guiType()
    {
        return scalar(CoreNature::Real, CoreVariability::Block, CoreComputability::Execute);
    }

    static CoreType integerBinary(const CoreType& x, const CoreType& y)
    {
        return withNature(mergeScalar(x, y), CoreNature::Integer);
    }

    static CoreType comparison(const CoreType& x, const CoreType& y)
    {
        return asBoolean(mergeScalar(x, y));
    }
};

}  // namespace

//--------------------------------------------------------------------------
// Public API: exact structural signal typing
//--------------------------------------------------------------------------

/**
 * Return the experimental Faust algebra whose carrier is CoreType. The
 * initial implementation covers scalar constants, inputs and outputs, basic
 * user-interface elements, casts, numeric binary operations, selection, and
 * simple delays with the structural rules of the legacy type system.
 *
 * The algebra never computes intervals or resolution. Traversal, recursive
 * dependencies, fixed-point iteration, validation, and legacy Type
 * materialization remain separate phases. Operations not yet migrated report
 * an explicit error instead of manufacturing an imprecise structural type.
 *
 * The returned stateless singleton remains owned by the signal library and is
 * valid until process termination.
 */
FaustAlgebra<CoreType>& sigCoreTypeAlgebra()
{
    static SigCoreTypeAlgebra algebra;
    return algebra;
}
