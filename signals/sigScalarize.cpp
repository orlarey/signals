/************************************************************************
 ************************************************************************
    FAUST signal library
    Copyright (C) 2003-2026 GRAME
    ---------------------------------------------------------------------
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License.
 ************************************************************************
 ************************************************************************/

#include "sigScalarize.hh"

#include <map>
#include <set>
#include <unordered_map>
#include <vector>

#include "list.hh"
#include "signals.hh"
#include "tlib-error.hh"

namespace {

struct ScalarGroup {
    std::vector<Tree> variables;
    std::vector<Tree> groups;
    std::vector<Tree> projections;
    bool              complete = false;
};

class Scalarizer {
   private:
    std::map<Tree, ScalarGroup>      fGroups;
    std::unordered_map<Tree, Tree>   fMemo;

    ScalarGroup& scalarizeGroup(Tree group)
    {
        auto found = fGroups.find(group);
        if (found != fGroups.end()) {
            return found->second;
        }

        Tree variable;
        Tree definitions;
        TLIB_ASSERT(isRec(group, variable, definitions));
        TLIB_ASSERT(definitions && isList(definitions));

        const int count = len(definitions);
        TLIB_ASSERT(count > 0);

        auto inserted = fGroups.emplace(group, ScalarGroup());
        ScalarGroup& result = inserted.first->second;
        result.variables.reserve(std::size_t(count));
        result.groups.reserve(std::size_t(count));
        result.projections.reserve(std::size_t(count));

        // Allocate every target before descending into any definition. This
        // makes all mutually recursive projections immediately resolvable.
        for (int index = 0; index < count; ++index) {
            Tree freshVariable = tree(unique("S"));
            Tree freshGroup    = ref(freshVariable);
            result.variables.push_back(freshVariable);
            result.groups.push_back(freshGroup);
            result.projections.push_back(sigProj(0, freshGroup));
        }

        for (int index = 0; index < count; ++index) {
            Tree definition = rewrite(nth(definitions, index));
            Tree completed  = rec(result.variables[std::size_t(index)], list1(definition));
            TLIB_ASSERT(completed == result.groups[std::size_t(index)]);
        }
        result.complete = true;
        return result;
    }

    Tree rewrite(Tree signal)
    {
        auto cached = fMemo.find(signal);
        if (cached != fMemo.end()) {
            return cached->second;
        }

        int  projection = -1;
        Tree group;
        if (isProj(signal, &projection, group)) {
            ScalarGroup& scalar = scalarizeGroup(group);
            TLIB_ASSERT(projection >= 0 && projection < int(scalar.projections.size()));
            Tree result = scalar.projections[std::size_t(projection)];
            fMemo.emplace(signal, result);
            return result;
        }

        Tree variable;
        Tree definitions;
        if (isRec(signal, variable, definitions)) {
            // A recursive group is a tuple-valued structural object. Signal
            // trees must consume it through sigProj, handled above.
            TLIB_ASSERT(false);
        }

        Tree debruijnBody;
        int  debruijnLevel = 0;
        TLIB_ASSERT(!isRec(signal, debruijnBody) && !isRef(signal, debruijnLevel));

        const int arity = signal->arity();
        if (arity == 0) {
            fMemo.emplace(signal, signal);
            return signal;
        }

        bool changed = false;
        tvec branches(arity);
        for (int index = 0; index < arity; ++index) {
            branches[std::size_t(index)] = rewrite(signal->branch(index));
            changed = changed || branches[std::size_t(index)] != signal->branch(index);
        }

        Tree result = changed ? tree(signal->node(), branches) : signal;
        fMemo.emplace(signal, result);
        return result;
    }

   public:
    Tree run(Tree signal) { return rewrite(signal); }
};

bool checkScalarized(Tree signal, std::set<Tree>& visited)
{
    if (!visited.insert(signal).second) {
        return true;
    }

    int  projection = -1;
    Tree group;
    if (isProj(signal, &projection, group)) {
        Tree variable;
        Tree definitions;
        if (projection != 0 || !isRec(group, variable, definitions) || !definitions ||
            !isList(definitions) || len(definitions) != 1) {
            return false;
        }
        visited.insert(group);
        return checkScalarized(nth(definitions, 0), visited);
    }

    Tree variable;
    Tree definitions;
    if (isRec(signal, variable, definitions)) {
        return false;
    }

    Tree debruijnBody;
    int  debruijnLevel = 0;
    if (isRec(signal, debruijnBody) || isRef(signal, debruijnLevel)) {
        return false;
    }

    for (int index = 0; index < signal->arity(); ++index) {
        if (!checkScalarized(signal->branch(index), visited)) {
            return false;
        }
    }
    return true;
}

}  // namespace

Tree sigScalarize(Tree signal)
{
    TLIB_ASSERT(signal);
    return Scalarizer().run(signal);
}

bool isSigScalarized(Tree signal)
{
    if (!signal) {
        return false;
    }
    std::set<Tree> visited;
    return checkScalarized(signal, visited);
}

