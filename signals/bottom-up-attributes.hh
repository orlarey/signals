/*
 * Experimental bottom-up attribute evaluation for hash-consed symbolic trees.
 *
 * This header deliberately keeps the local evaluator and the fixpoint policy
 * independent from the graph/scheduling machinery.  It is a prototype: the
 * API may change after it has been exercised by real Faust algebras.
 */

#pragma once

#include <cstddef>
#include <deque>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

#include "DirectedGraph.hh"
#include "DirectedGraphAlgorythm.hh"
#include "list.hh"
#include "signals.hh"
#include "tree.hh"

namespace sigs {
namespace attributes {

template <typename Node>
struct EvaluationReport {
    std::size_t              nodes                = 0;
    std::size_t              edges                = 0;
    std::size_t              components           = 0;
    std::size_t              cyclic_components    = 0;
    std::size_t              evaluations           = 0;
    std::map<Node, size_t>   evaluations_per_node;
};

/**
 * Resolve dependencies of symbolic TLIB trees.
 *
 * Ordinary nodes depend on their branches.  A symbolic recursive node
 * SYMREC(id) depends on the body stored in its RECDEF property.  Recursive
 * occurrences are the very same hash-consed Tree, so walking the body closes
 * the graph cycle without manufacturing a separate reference node.
 */
class SymbolicTreeDependencies {
   public:
    using node_type = Tree;

    std::vector<Tree> operator()(Tree node) const
    {
        Tree symbolic_id;
        Tree body;
        if (isRec(node, symbolic_id, body)) {
            if (!body) {
                throw std::runtime_error("symbolic recursive tree without RECDEF");
            }
            return {body};
        }

        Tree debruijn_body;
        int  debruijn_level = 0;
        if (isRec(node, debruijn_body) || isRef(node, debruijn_level)) {
            throw std::runtime_error("de Bruijn recursive trees are not supported");
        }

        std::vector<Tree> result;
        result.reserve(std::size_t(node->arity()));
        for (int i = 0; i < node->arity(); ++i) {
            result.push_back(node->branch(i));
        }
        return result;
    }
};

/**
 * A scalar unit of attribute computation for Faust signals.
 *
 * An expression node denotes an ordinary signal Tree.  A recursive projection
 * node denotes one component of the group carried by SYMREC(id).  Keeping the
 * projection index in the graph node prevents unrelated components of a
 * recursive group from being merged into one artificial cycle.
 */
struct SignalAttributeNode {
    Tree tree       = nullptr;
    int  projection = -1;

    static SignalAttributeNode expression(Tree tree) { return {tree, -1}; }
    static SignalAttributeNode recursiveProjection(Tree group, int index)
    {
        return {group, index};
    }

    bool isRecursiveProjection() const { return projection >= 0; }

    friend bool operator<(const SignalAttributeNode& lhs, const SignalAttributeNode& rhs)
    {
        return lhs.tree < rhs.tree || (lhs.tree == rhs.tree && lhs.projection < rhs.projection);
    }

    friend bool operator==(const SignalAttributeNode& lhs, const SignalAttributeNode& rhs)
    {
        return lhs.tree == rhs.tree && lhs.projection == rhs.projection;
    }
};

/**
 * Resolve scalar dependencies of Faust recursive signal groups.
 *
 *   expression(sigProj(k, R)) -> recursiveProjection(R, k)
 *   recursiveProjection(R, k) -> expression(RECDEF(R)[k])
 *
 * A bare recursive group has a tuple value and is therefore rejected by this
 * scalar resolver.  It must be consumed through a projection.
 */
class SignalProjectionDependencies {
   public:
    using node_type = SignalAttributeNode;

    std::vector<node_type> operator()(const node_type& node) const
    {
        if (!node.tree) {
            throw std::runtime_error("null signal attribute node");
        }

        if (node.isRecursiveProjection()) {
            Tree id;
            Tree body;
            if (!isRec(node.tree, id, body) || !body) {
                throw std::runtime_error("recursive projection without a symbolic group");
            }
            if (!isList(body) || node.projection >= len(body)) {
                throw std::runtime_error("recursive projection outside its signal group");
            }
            return {node_type::expression(nth(body, node.projection))};
        }

        int  projection = -1;
        Tree group;
        if (isProj(node.tree, &projection, group)) {
            Tree id;
            Tree body;
            if (!isRec(group, id, body) || !body) {
                throw std::runtime_error("signal projection does not target a symbolic group");
            }
            if (!isList(body) || projection < 0 || projection >= len(body)) {
                throw std::runtime_error("signal projection outside its recursive group");
            }
            return {node_type::recursiveProjection(group, projection)};
        }

        Tree symbolic_id;
        Tree body;
        if (isRec(node.tree, symbolic_id, body)) {
            throw std::runtime_error("a recursive signal group must be accessed by projection");
        }

        Tree debruijn_body;
        int  debruijn_level = 0;
        if (isRec(node.tree, debruijn_body) || isRef(node.tree, debruijn_level)) {
            throw std::runtime_error("de Bruijn recursive trees are not supported");
        }

        int64_t int64_value = 0;
        double  real_value  = 0;
        int     int_value   = 0;
        if (isSigInt(node.tree, &int_value) || isSigInt64(node.tree, &int64_value) ||
            isSigReal(node.tree, &real_value) || isSigInput(node.tree, &int_value)) {
            return {};
        }

        Tree x;
        Tree y;
        int  operation = 0;
        if (isSigBinOp(node.tree, &operation, x, y)) {
            return {node_type::expression(x), node_type::expression(y)};
        }

        Tree label;
        Tree init;
        Tree lo;
        Tree hi;
        Tree step;
        if (isSigVSlider(node.tree, label, init, lo, hi, step) ||
            isSigHSlider(node.tree, label, init, lo, hi, step) ||
            isSigNumEntry(node.tree, label, init, lo, hi, step)) {
            return {node_type::expression(init), node_type::expression(lo),
                    node_type::expression(hi), node_type::expression(step)};
        }

        std::vector<node_type> result;
        result.reserve(std::size_t(node.tree->arity()));
        for (int i = 0; i < node.tree->arity(); ++i) {
            result.push_back(node_type::expression(node.tree->branch(i)));
        }
        return result;
    }
};

/**
 * Evaluate attributes component by component.  An edge n -> d means that n
 * depends on d.  Strongly connected components are evaluated with a local
 * worklist; acyclic singleton components are evaluated exactly once.
 */
template <typename Attribute, typename Evaluator, typename DependencyResolver,
          typename FixpointPolicy>
class BottomUpAttributes {
   public:
    using node_type   = typename DependencyResolver::node_type;
    using report_type = EvaluationReport<node_type>;

   private:
    Evaluator          fEvaluator;
    DependencyResolver fDependencies;
    FixpointPolicy     fPolicy;
    std::size_t        fEvaluationBudget;

    digraph<node_type>              fGraph;
    std::map<node_type, Attribute>   fAttributes;
    report_type                     fReport;

    void buildGraph(const node_type& root)
    {
        std::vector<node_type> pending {root};
        std::set<node_type>    visited;

        while (!pending.empty()) {
            node_type node = pending.back();
            pending.pop_back();
            if (!visited.insert(node).second) {
                continue;
            }

            fGraph.add(node);
            for (const node_type& dependency : fDependencies(node)) {
                fGraph.add(node, dependency);
                ++fReport.edges;
                pending.push_back(dependency);
            }
        }
        fReport.nodes = fGraph.nodes().size();
    }

    Attribute candidate(const node_type& node, const std::map<node_type, Attribute>& local,
                        const std::set<node_type>& component)
    {
        std::vector<Attribute> inputs;
        const std::vector<node_type> dependencies = fDependencies(node);
        inputs.reserve(dependencies.size());
        for (const node_type& dependency : dependencies) {
            if (component.find(dependency) != component.end()) {
                inputs.push_back(local.at(dependency));
            } else {
                inputs.push_back(fAttributes.at(dependency));
            }
        }

        ++fReport.evaluations;
        ++fReport.evaluations_per_node[node];
        if (fReport.evaluations > fEvaluationBudget) {
            throw std::runtime_error("bottom-up attribute evaluation did not converge");
        }
        return fEvaluator(node, inputs);
    }

    void evaluateAcyclic(const node_type& node)
    {
        static const std::set<node_type> empty_component;
        static const std::map<node_type, Attribute> empty_local;
        fAttributes.insert_or_assign(node, candidate(node, empty_local, empty_component));
    }

    void evaluateCyclic(const std::set<node_type>& component)
    {
        std::map<node_type, Attribute> local;
        std::map<node_type, std::set<node_type>> dependents;
        std::deque<node_type> worklist;
        std::set<node_type>   queued;

        for (const node_type& node : component) {
            local.emplace(node, fPolicy.initial(node));
            worklist.push_back(node);
            queued.insert(node);
        }

        for (const node_type& user : component) {
            for (const auto& edge : fGraph.destinations(user)) {
                const node_type& dependency = edge.first;
                if (component.find(dependency) != component.end()) {
                    dependents[dependency].insert(user);
                }
            }
        }

        while (!worklist.empty()) {
            node_type node = worklist.front();
            worklist.pop_front();
            queued.erase(node);

            const Attribute next = candidate(node, local, component);

            const bool finished = fPolicy.reached(node, local.at(node), next);
            local.insert_or_assign(node, next);
            if (!finished) {
                for (const node_type& user : dependents[node]) {
                    if (queued.insert(user).second) {
                        worklist.push_back(user);
                    }
                }
            }
        }

        for (const auto& entry : local) {
            fAttributes.insert_or_assign(entry.first, entry.second);
        }
    }

   public:
    BottomUpAttributes(Evaluator evaluator, DependencyResolver dependencies, FixpointPolicy policy,
                       std::size_t evaluation_budget = 100000)
        : fEvaluator(std::move(evaluator)),
          fDependencies(std::move(dependencies)),
          fPolicy(std::move(policy)),
          fEvaluationBudget(evaluation_budget)
    {
    }

    Attribute evaluate(const node_type& root)
    {
        fGraph      = digraph<node_type>();
        fAttributes.clear();
        fReport = report_type();
        buildGraph(root);

        Tarjan<node_type> tarjan(fGraph);
        const auto&       partition = tarjan.partition();
        fReport.components           = partition.size();

        std::map<node_type, std::size_t> component_of;
        std::vector<std::set<node_type>> components(partition.begin(), partition.end());
        for (std::size_t i = 0; i < components.size(); ++i) {
            for (const node_type& node : components[i]) {
                component_of[node] = i;
            }
        }

        digraph<std::size_t> dag;
        for (std::size_t i = 0; i < components.size(); ++i) {
            dag.add(i);
        }
        for (const node_type& node : fGraph.nodes()) {
            const std::size_t source = component_of.at(node);
            for (const auto& edge : fGraph.destinations(node)) {
                const std::size_t destination = component_of.at(edge.first);
                if (source != destination) {
                    dag.add(source, destination);
                }
            }
        }

        for (std::size_t index : serialize(dag)) {
            const auto& component = components[index];
            const bool cyclic = component.size() > 1 ||
                                fGraph.areConnected(*component.begin(), *component.begin());
            if (cyclic) {
                ++fReport.cyclic_components;
                evaluateCyclic(component);
            } else {
                evaluateAcyclic(*component.begin());
            }
        }
        return fAttributes.at(root);
    }

    const Attribute& attribute(const node_type& node) const { return fAttributes.at(node); }
    const report_type& report() const { return fReport; }
};

}  // namespace attributes
}  // namespace sigs
