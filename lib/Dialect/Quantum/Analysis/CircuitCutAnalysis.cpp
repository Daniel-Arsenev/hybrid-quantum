//===--- CircuitCutAnalysis.cpp - Wire / Gate Cut Analysis--------*- C++-*-===//
//
// @author  Daniel Arsenev
//===----------------------------------------------------------------------===//

#include "quantum-mlir/Dialect/Quantum/Analysis/CircuitCutAnalysis.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/Operation.h"
#include "mlir/IR/Value.h"
#include "ortools/sat/cp_model.h"
#include "quantum-mlir/Dialect/Quantum/IR/QuantumOps.h"
#include "quantum-mlir/Dialect/Quantum/IR/QuantumTypes.h"
#include "quantum-mlir/Dialect/Quantum/Interfaces/GateOpInterface.h"
#include "quantum-mlir/Dialect/Quantum/Transforms/Passes.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mlir::quantum {

#define GEN_PASS_DEF_CIRCUITCUTANALYSIS
#include "quantum-mlir/Dialect/Quantum/Transforms/Passes.h.inc"

} // namespace mlir::quantum

using namespace mlir;
using namespace mlir::quantum;

// INDEX ALIASES
////////////////

using OperandIndex = uint64_t; // indexes a qubit operand slot within a gate

using SubGateIndex = uint64_t;   // indexes a subgate
using GateIndex = uint64_t;      // indexes the gates vector
using MultiGateIndex = uint64_t; // indexes a multigate
using WireIndex = uint64_t;      // indexes the wires vector
using LineIndex = uint64_t;      // indexes the lines vector

using SubCircuitIndex = uint64_t; // a subcircuit color
using LayerIndex = uint64_t;      // a schedule layer
using SlotIndex = uint64_t;       // a solver slot, the real subgates first, then the idle slots

// CIRCUIT GRAPH
////////////////

/// An n qubit gate acting on n physical qubits is one Gate. Its qubit operands
/// are its subgates, and separating them across subcircuits is a gate cut.
struct Gate {
    Operation* operation;               // the MLIR operation this gate comes from
    std::vector<SubGateIndex> subGates; // [OperandIndex]
};

/// A qubit going from source to destination, the next subgate that uses it. passthrough lists
/// every value it takes along the way, both ends included.
struct Wire {
    SubGateIndex source;
    SubGateIndex destination;
    WireValues passthrough;
};

/// An n qubit gate operation is one MultiGate, one Gate per qubit index of its operands.
/// Lookup from a gate operation and qubit index to a GateIndex, which we use while tracing
/// wires.
struct MultiGate {
    std::vector<GateIndex> gates; // [multiQubitIndex] within this multigate
};

/// The whole circuit as one graph
struct CircuitGraph {
    uint64_t subGateCount = 0; // number of subgates
    std::vector<Gate> gates;   // [GateIndex]
    std::vector<Wire> wires;   // [WireIndex]

    std::vector<GateIndex> subGateToGate; // [SubGateIndex]

    std::vector<std::optional<WireIndex>> incomingWire; // [SubGateIndex], nullopt if qubit line start
    std::vector<std::optional<WireIndex>> outgoingWire; // [SubGateIndex], nullopt if qubit line end

    std::vector<std::vector<GateIndex>> gateSuccessors;   // [GateIndex]
    std::vector<std::vector<GateIndex>> gatePredecessors; // [GateIndex]

    std::vector<std::vector<SubGateIndex>> lines; // [LineIndex], ordered subgate chains

    std::vector<LayerIndex> schedule; // [SubGateIndex], the scheduled layer of each subgate
};

// MLIR TRACING
///////////////

/// ReachedGate carries the MLIR operation for the traced destination gate
struct ReachedGate {
    Operation* gate;
    OperandIndex operandIndex;
    uint64_t multiQubitIndex;
};

/// Number of qubit typed values among the values
static uint64_t qubitCount(ValueRange values)
{
    return llvm::count_if(values, [](Value value) { return isa<QubitType>(value.getType()); });
}

/// is the gate a n qubit gate, as opposed to a single qubit gate
static bool isMultiQubitGate(Operation* op) { return isa<GateOpInterface>(op) && qubitCount(op->getOperands()) > 1; }

/// Trace one qubit through splits, merges and single qubit gates to the next n qubit gate.
/// Returns nullopt if the qubit ends first, e.g. by measurement or return.
static std::pair<std::optional<ReachedGate>, WireValues> traceThroughOps(SingleQubit ref)
{
    WireValues passthrough;
    while (true) {
        passthrough.push_back(ref);

        auto uses = ref.multiQubit.getUses();
        if (uses.empty()) {
            return {std::nullopt, std::move(passthrough)};
        }
        assert(ref.multiQubit.hasOneUse() && "quantum values are linear: expected a single use");

        OpOperand &use = *uses.begin();
        Operation* destination = use.getOwner();
        uint64_t landedOperandNumber = use.getOperandNumber();

        // We return at an n qubit gate, with the index of the qubit operand we landed on
        if (isMultiQubitGate(destination)) {
            uint64_t qubitOperandIndex = qubitCount(destination->getOperands().take_front(landedOperandNumber));
            return {ReachedGate{destination, qubitOperandIndex, ref.multiQubitIndex}, std::move(passthrough)};
        }

        // A single qubit gate has exactly one qubit result, possibly parametrized by non qubit
        // operands. We record it and follow that qubit result.
        if (isa<GateOpInterface>(destination)) {
            auto qubitResult =
                llvm::find_if(destination->getResults(), [](Value result) { return isa<QubitType>(result.getType()); });
            if (qubitResult == destination->getResults().end()) {
                return {std::nullopt, std::move(passthrough)};
            }
            ref = {*qubitResult, ref.multiQubitIndex};
        }

        // We are splitting the m-wide qubit value, so we find the narrower qubit value
        // we are in and adjust the index accordingly
        else if (isa<SplitOp>(destination)) {
            uint64_t multiQubitIndex = ref.multiQubitIndex;
            for (Value result : destination->getResults()) {
                auto qubitType = cast<QubitType>(result.getType());
                uint64_t multiGateWidth = qubitType.getSize();
                if (multiQubitIndex >= multiGateWidth) {
                    multiQubitIndex -= multiGateWidth;
                } else {
                    ref = {result, multiQubitIndex};
                    break;
                }
            }
        }

        // We are merging the m-wide qubit value with some others, so we adjust the index
        // accordingly
        else if (isa<MergeOp>(destination)) {
            uint64_t multiQubitIndex = ref.multiQubitIndex;
            for (uint64_t i = 0; i < landedOperandNumber; ++i) {
                auto qubitType = cast<QubitType>(destination->getOperand(i).getType());
                multiQubitIndex += qubitType.getSize();
            }
            Value result = destination->getResult(0);
            ref = {result, multiQubitIndex};
        }

        // measured, reset, deallocated, returned
        else if (isa<MeasureOp, ResetOp, DeallocateOp, func::ReturnOp>(destination)) {
            return {std::nullopt, std::move(passthrough)};
        }

        // unrecognized operation
        else {
            return {std::nullopt, std::move(passthrough)};
        }
    }
}

// SCHEDULING
/////////////

// The ASAP, ALAP, and force-directed schedulers each calculate a layer for every gate,
// which we use in buildCircuitGraph as the per subgate schedule of the graph

/// Order the gates with the Kahn algorithm
static std::vector<GateIndex> topologicalOrder(CircuitGraph &graph)
{
    uint64_t gateCount = graph.gateSuccessors.size();
    auto order = std::vector<GateIndex>();
    auto remainingPredecessors = std::vector<uint64_t>(gateCount);
    auto ready = std::queue<GateIndex>();
    for (GateIndex gateIndex = 0; gateIndex < gateCount; ++gateIndex) {
        remainingPredecessors[gateIndex] = graph.gatePredecessors[gateIndex].size();
        if (remainingPredecessors[gateIndex] == 0) {
            ready.push(gateIndex);
        }
    }
    while (!ready.empty()) {
        GateIndex gateIndex = ready.front();
        ready.pop();
        order.push_back(gateIndex);
        for (GateIndex successor : graph.gateSuccessors[gateIndex]) {
            if (--remainingPredecessors[successor] == 0) {
                ready.push(successor);
            }
        }
    }
    return order;
}

/// Raise each ASAP layer in topological order, so every gate sits after its predecessors
static void raiseAsap(CircuitGraph &graph, ArrayRef<GateIndex> order, std::vector<LayerIndex> &asap)
{
    for (GateIndex gateIndex : order) {
        for (GateIndex successor : graph.gateSuccessors[gateIndex]) {
            asap[successor] = std::max(asap[successor], asap[gateIndex] + 1);
        }
    }
}

/// Lower each ALAP layer in reverse topological order, so every gate sits before its successors
static void lowerAlap(CircuitGraph &graph, ArrayRef<GateIndex> order, std::vector<LayerIndex> &alap)
{
    for (GateIndex gateIndex : llvm::reverse(order)) {
        for (GateIndex predecessor : graph.gatePredecessors[gateIndex]) {
            alap[predecessor] = std::min(alap[predecessor], alap[gateIndex] - 1);
        }
    }
}

/// Schedule each gate ASAP, each layer after its latest predecessor
static std::vector<LayerIndex> scheduleGatesAsap(CircuitGraph &graph)
{
    uint64_t gateCount = graph.gateSuccessors.size();
    auto gateLayer = std::vector<LayerIndex>(gateCount, 0);
    raiseAsap(graph, topologicalOrder(graph), gateLayer);
    return gateLayer;
}

/// Schedule each gate ALAP, each layer before its earliest successor
static std::vector<LayerIndex> scheduleGatesAlap(CircuitGraph &graph, LayerIndex makespan)
{
    uint64_t gateCount = graph.gateSuccessors.size();
    auto gateLayer = std::vector<LayerIndex>(gateCount, makespan);
    lowerAlap(graph, topologicalOrder(graph), gateLayer);
    return gateLayer;
}

/// Look-ahead correction weight from Paulin and Knight
static constexpr double lookahead = 1.0 / 3.0;

/// Weight of the storage term, small so it only breaks ties in gate concurrency
static constexpr double storageWeight = 0.01;

/// Expected number of gates per layer, each gate spread evenly over its window
static std::vector<double> buildGateDistribution(
    ArrayRef<LayerIndex> asap,
    ArrayRef<LayerIndex> alap,
    LayerIndex makespan)
{
    auto gateDistribution = std::vector<double>(makespan + 1, 0.0);
    for (GateIndex gateIndex = 0; gateIndex < asap.size(); ++gateIndex) {
        double probability = 1.0 / (alap[gateIndex] - asap[gateIndex] + 1);
        for (LayerIndex layer = asap[gateIndex]; layer <= alap[gateIndex]; ++layer) {
            gateDistribution[layer] += probability;
        }
    }
    return gateDistribution;
}

/// Expected share of a wire idling at a layer, averaged over its ASAP, ALAP and widest span
static double wireIdleContribution(
    LayerIndex asapSource,
    LayerIndex alapSource,
    LayerIndex asapDestination,
    LayerIndex alapDestination,
    LayerIndex layer)
{
    double contribution = 0.0;
    if (layer > asapSource && layer < asapDestination) {
        contribution += 1.0;
    }
    if (layer > alapSource && layer < alapDestination) {
        contribution += 1.0;
    }
    if (layer > asapSource && layer < alapDestination) {
        contribution += 1.0;
    }
    return contribution / 3.0;
}

/// Expected number of idle qubits per layer
static std::vector<double> buildStorageDistribution(
    CircuitGraph &graph,
    ArrayRef<LayerIndex> asap,
    ArrayRef<LayerIndex> alap,
    LayerIndex makespan)
{
    auto storageDistribution = std::vector<double>(makespan + 1, 0.0);
    for (Wire &wire : graph.wires) {
        GateIndex source = graph.subGateToGate[wire.source];
        GateIndex destination = graph.subGateToGate[wire.destination];
        for (LayerIndex layer = asap[source] + 1; layer < alap[destination]; ++layer) {
            storageDistribution[layer] +=
                wireIdleContribution(asap[source], alap[source], asap[destination], alap[destination], layer);
        }
    }
    return storageDistribution;
}

/// Compute the force of shrinking the window of a gate under the frozen distribution,
/// applying the look-ahead correction per layer. Negative force moves probability toward
/// less crowded layers, which is desirable.
static double gateRestrictionForce(
    ArrayRef<double> gateDistribution,
    LayerIndex oldFrom,
    LayerIndex oldTo,
    LayerIndex newFrom,
    LayerIndex newTo)
{
    double oldProbability = 1.0 / (oldTo - oldFrom + 1);
    double newProbability = 1.0 / (newTo - newFrom + 1);
    double force = 0.0;
    for (LayerIndex layer = oldFrom; layer <= oldTo; ++layer) {
        double delta = (layer >= newFrom && layer <= newTo ? newProbability : 0.0) - oldProbability;
        force += delta * (gateDistribution[layer] + lookahead * delta);
    }
    return force;
}

/// Compute the force of provisionally fixing a gate at a layer. Shrink the windows of all
/// dependent gates in a copy, then sum the gate and storage forces of every changed window.
static double provisionalForce(
    CircuitGraph &graph,
    ArrayRef<GateIndex> order,
    ArrayRef<LayerIndex> asap,
    ArrayRef<LayerIndex> alap,
    ArrayRef<double> gateDistribution,
    ArrayRef<double> storageDistribution,
    GateIndex gate,
    LayerIndex layer)
{
    // The windows once the gate is fixed at layer and every dependent gate shrinks to match
    auto fixedAsap = std::vector<LayerIndex>(asap.begin(), asap.end());
    auto fixedAlap = std::vector<LayerIndex>(alap.begin(), alap.end());
    fixedAsap[gate] = layer;
    fixedAlap[gate] = layer;
    raiseAsap(graph, order, fixedAsap);
    lowerAlap(graph, order, fixedAlap);

    // Gate forces of every window that shrank, including the one of the fixed gate
    double force = 0.0;
    for (GateIndex gateIndex = 0; gateIndex < asap.size(); ++gateIndex) {
        if (fixedAsap[gateIndex] != asap[gateIndex] || fixedAlap[gateIndex] != alap[gateIndex]) {
            force += gateRestrictionForce(
                gateDistribution,
                asap[gateIndex],
                alap[gateIndex],
                fixedAsap[gateIndex],
                fixedAlap[gateIndex]);
        }
    }

    // Storage forces of every wire whose source or destination window shrank
    auto shrank = [&](GateIndex gateIndex) {
        return fixedAsap[gateIndex] != asap[gateIndex] || fixedAlap[gateIndex] != alap[gateIndex];
    };
    for (Wire &wire : graph.wires) {
        GateIndex source = graph.subGateToGate[wire.source];
        GateIndex destination = graph.subGateToGate[wire.destination];
        if (!shrank(source) && !shrank(destination)) {
            continue;
        }
        // The new span lies within the old widest span
        for (LayerIndex wireLayer = asap[source] + 1; wireLayer < alap[destination]; ++wireLayer) {
            double delta =
                wireIdleContribution(
                    fixedAsap[source],
                    fixedAlap[source],
                    fixedAsap[destination],
                    fixedAlap[destination],
                    wireLayer)
                - wireIdleContribution(asap[source], alap[source], asap[destination], alap[destination], wireLayer);
            force += storageWeight * delta * (storageDistribution[wireLayer] + lookahead * delta);
        }
    }
    return force;
}

/// Schedule with the force directed scheduling of Paulin and Knight. Fix the makespan to the
/// critical path, then repeatedly commit the lowest force gate to its layer until every gate is
/// fixed, and return the layer of each gate, indexed by GateIndex.
static std::vector<LayerIndex> scheduleGatesForceDirected(CircuitGraph &graph)
{
    uint64_t gateCount = graph.gateSuccessors.size();
    if (gateCount == 0) {
        return {};
    }

    // Each gate may still go anywhere from its ASAP to its ALAP layer, its window. It is fixed
    // once both are equal. The makespan is the last layer of the critical path.
    auto asap = scheduleGatesAsap(graph);
    LayerIndex makespan = *llvm::max_element(asap);
    auto alap = scheduleGatesAlap(graph, makespan);

    // Topological order, which we reuse to shrink the windows after each fixed gate
    auto order = topologicalOrder(graph);

    while (true) {
        auto gateDistribution = buildGateDistribution(asap, alap, makespan);
        auto storageDistribution = buildStorageDistribution(graph, asap, alap, makespan);

        // Find the feasible assignment with the lowest total force
        double bestForce = std::numeric_limits<double>::infinity();
        GateIndex bestGate = 0;
        LayerIndex bestLayer = 0;
        bool anyOpen = false;
        for (GateIndex gateIndex = 0; gateIndex < gateCount; ++gateIndex) {
            if (asap[gateIndex] == alap[gateIndex]) {
                continue;
            }
            anyOpen = true;
            for (LayerIndex layer = asap[gateIndex]; layer <= alap[gateIndex]; ++layer) {
                double force =
                    provisionalForce(graph, order, asap, alap, gateDistribution, storageDistribution, gateIndex, layer);
                if (force < bestForce) {
                    bestForce = force;
                    bestGate = gateIndex;
                    bestLayer = layer;
                }
            }
        }

        if (!anyOpen) {
            break;
        }

        // Fix the best gate at its layer and shrink the other windows to match
        asap[bestGate] = bestLayer;
        alap[bestGate] = bestLayer;
        raiseAsap(graph, order, asap);
        lowerAlap(graph, order, alap);
    }

    return asap;
}

// CIRCUIT GRAPH CONSTRUCTION
/////////////////////////////

/// Build the circuit graph of the FuncOp, with its subgates, gates, wires, qubit lines, gate
/// dependencies, and schedule
static CircuitGraph buildCircuitGraph(func::FuncOp root, CircuitCutOptions &options)
{
    CircuitGraph graph;
    std::unordered_map<Operation*, MultiGateIndex> lookup;

    // multiGates resolves a gate operation and qubit index to a GateIndex while we trace wires
    std::vector<MultiGate> multiGates;

    // Gather subgates, eliding single qubit gates. Each operation yields one Gate per qubit index
    // of its operands, and each Gate one SubGate per qubit operand.
    for (Operation &op : root.getBody().front()) {
        if (!isMultiQubitGate(&op)) {
            continue;
        }

        MultiGateIndex newMultiGateIndex = multiGates.size();
        lookup[&op] = newMultiGateIndex;
        MultiGate &multiGate = multiGates.emplace_back();

        QubitType qubitType = cast<QubitType>(op.getResult(0).getType());
        uint64_t multiGateWidth = qubitType.getSize();
        for (uint64_t multiQubitIndex = 0; multiQubitIndex < multiGateWidth; ++multiQubitIndex) {
            GateIndex gateIndex = graph.gates.size();
            multiGate.gates.push_back(gateIndex);

            Gate &gate = graph.gates.emplace_back();
            gate.operation = &op;

            for (Value operand : op.getOperands()) {
                if (!isa<QubitType>(operand.getType())) {
                    continue;
                }
                gate.subGates.push_back(graph.subGateCount);
                ++graph.subGateCount;
            }
        }
    }

    uint64_t subGateCount = graph.subGateCount;

    // Trace the result of each gate to its destination to build the Wire edges. A result
    // reaching a measurement or leaving the function untracked gets no wire.
    for (Operation &sourceOp : root.getBody().front()) {
        if (!isMultiQubitGate(&sourceOp)) {
            continue;
        }
        MultiGateIndex multiGateIndex = lookup.at(&sourceOp);
        MultiGate &sourceMultiGate = multiGates[multiGateIndex];
        for (auto [resultIndex, result] : llvm::enumerate(sourceOp.getResults())) {
            QubitType qubitType = cast<QubitType>(result.getType());
            uint64_t multiGateWidth = qubitType.getSize();
            for (uint64_t multiQubitIndex = 0; multiQubitIndex < multiGateWidth; ++multiQubitIndex) {
                GateIndex sourceGateIndex = sourceMultiGate.gates[multiQubitIndex];
                Gate &sourceGate = graph.gates[sourceGateIndex];
                SubGateIndex sourceSubGateIndex = sourceGate.subGates[resultIndex];

                auto [reachedGate, passthrough] = traceThroughOps({result, multiQubitIndex});
                if (!reachedGate || !lookup.contains(reachedGate->gate)) {
                    continue;
                }

                MultiGateIndex destinationMultiGateIndex = lookup.at(reachedGate->gate);
                MultiGate &destinationMultiGate = multiGates[destinationMultiGateIndex];

                GateIndex destinationGateIndex = destinationMultiGate.gates[reachedGate->multiQubitIndex];
                Gate &destinationGate = graph.gates[destinationGateIndex];
                SubGateIndex destinationSubGateIndex = destinationGate.subGates[reachedGate->operandIndex];

                graph.wires.push_back({sourceSubGateIndex, destinationSubGateIndex, std::move(passthrough)});
            }
        }
    }

    // Derive the topology from the gates and wires, the reverse lookups, wire connectivity,
    // qubit lines, and gate dependency graph
    uint64_t gateCount = graph.gates.size();
    uint64_t wireCount = graph.wires.size();

    // Invert gates into subGateToGate
    graph.subGateToGate.assign(subGateCount, 0);
    for (GateIndex gateIndex = 0; gateIndex < gateCount; ++gateIndex) {
        for (SubGateIndex subGateIndex : graph.gates[gateIndex].subGates) {
            graph.subGateToGate[subGateIndex] = gateIndex;
        }
    }

    // Build incoming and outgoing wire connectivity from the wire list
    graph.incomingWire.assign(subGateCount, std::nullopt);
    graph.outgoingWire.assign(subGateCount, std::nullopt);
    for (WireIndex wireIndex = 0; wireIndex < wireCount; ++wireIndex) {
        graph.outgoingWire[graph.wires[wireIndex].source] = wireIndex;
        graph.incomingWire[graph.wires[wireIndex].destination] = wireIndex;
    }

    // A subgate without an incoming wire starts a qubit line
    std::vector<SubGateIndex> lineStarts;
    for (SubGateIndex subGateIndex = 0; subGateIndex < subGateCount; ++subGateIndex) {
        if (!graph.incomingWire[subGateIndex]) {
            lineStarts.push_back(subGateIndex);
        }
    }

    // Build qubit lines by walking from each line start along outgoing wires
    graph.lines.clear();
    for (SubGateIndex startSubGateIndex : lineStarts) {
        std::vector<SubGateIndex> &line = graph.lines.emplace_back();
        SubGateIndex current = startSubGateIndex;
        while (true) {
            line.push_back(current);
            auto nextWire = graph.outgoingWire[current];
            if (!nextWire) {
                break;
            }
            current = graph.wires[*nextWire].destination;
        }
    }

    // Build the gate dependency edges
    graph.gateSuccessors.assign(gateCount, {});
    graph.gatePredecessors.assign(gateCount, {});
    for (Wire &wire : graph.wires) {
        GateIndex sourceGate = graph.subGateToGate[wire.source];
        GateIndex destinationGate = graph.subGateToGate[wire.destination];
        graph.gateSuccessors[sourceGate].push_back(destinationGate);
        graph.gatePredecessors[destinationGate].push_back(sourceGate);
    }

    // Schedule the gates per the schedule policy
    std::vector<LayerIndex> gateLayer;
    if (options.schedulePolicy == SchedulePolicy::LayeredAsap) {
        gateLayer = scheduleGatesAsap(graph);
    } else if (options.schedulePolicy == SchedulePolicy::LayeredAlap) {
        std::vector<LayerIndex> asap = scheduleGatesAsap(graph);
        LayerIndex makespan = asap.empty() ? 0 : *llvm::max_element(asap);
        gateLayer = scheduleGatesAlap(graph, makespan);
    } else {
        gateLayer = scheduleGatesForceDirected(graph);
    }

    graph.schedule.assign(subGateCount, 0);
    for (SubGateIndex subGateIndex = 0; subGateIndex < subGateCount; ++subGateIndex) {
        graph.schedule[subGateIndex] = gateLayer[graph.subGateToGate[subGateIndex]];
    }

    return graph;
}

// CIRCUIT CUT SOLVER
/////////////////////

struct CutSolution {
    CutStatus status = CutStatus::Infeasible;
    std::vector<SubCircuitIndex> subGateColor; // [SubGateIndex] real subgates only, canonicalized
    int64_t subcircuitCount = 0;               // distinct colors over all slots, including idle slots
    double solveSeconds = 0.0;
    std::vector<std::vector<SubCircuitIndex>>
        wireChainColors; // [WireIndex], a vector of the chain colors, one per layer
};

/// The CP-SAT model of the cut while we build it
struct CutModel {
    operations_research::sat::CpModelBuilder builder;
    SubCircuitIndex cap;                                                   // number of colors
    std::vector<std::vector<operations_research::sat::BoolVar>> slotColor; // [SlotIndex][SubCircuitIndex]
    std::vector<std::vector<SlotIndex>> layerOccupants;                    // [LayerIndex]
    std::vector<operations_research::sat::BoolVar> wireCut;                // [wire edge], true iff cut
};

/// Create a slot that occupies every layer in [firstLayer, lastLayer]. This lets one slot
/// replace a long run of idle layers past idleSlotCap without changing the width
static SlotIndex addSlot(CutModel &model, LayerIndex firstLayer, LayerIndex lastLayer)
{
    SlotIndex slot = model.slotColor.size();
    auto &row = model.slotColor.emplace_back(model.cap);
    for (SubCircuitIndex color = 0; color < model.cap; ++color) {
        row[color] = model.builder.NewBoolVar();
    }
    model.builder.AddExactlyOne(row);
    for (LayerIndex layer = firstLayer; layer <= lastLayer; ++layer) {
        model.layerOccupants[layer].push_back(slot);
    }
    return slot;
}

/// Add a wire edge between the consecutive slots previous and next, appending to wireCut so
/// we cost it like any other wire edge
static void addWireEdge(CutModel &model, CircuitCutOptions &options, SlotIndex previous, SlotIndex next)
{
    size_t wireIndex = model.wireCut.size();
    model.wireCut.push_back(model.builder.NewBoolVar());
    for (SubCircuitIndex color = 0; color < model.cap; ++color) {
        model.builder.AddEquality(model.slotColor[previous][color], model.slotColor[next][color])
            .OnlyEnforceIf(model.wireCut[wireIndex].Not());
        if (options.allowWireCuts) {
            model.builder.AddBoolOr({model.slotColor[previous][color].Not(), model.slotColor[next][color].Not()})
                .OnlyEnforceIf(model.wireCut[wireIndex]);
        }
    }
    if (!options.allowWireCuts) {
        model.builder.FixVariable(model.wireCut[wireIndex], false);
    }
}

/// Pad the idle layers between previous and destination with a chain of idle slots, one
/// chainSlots entry per layer. Past 2 * idleSlotCap idle layers, one slot spans the idle layers in the middle.
static void padIdleLayers(
    CutModel &model,
    CircuitCutOptions &options,
    SlotIndex previous,
    LayerIndex sourceLayer,
    SlotIndex destination,
    LayerIndex closingLayer,
    std::vector<SlotIndex> &chainSlots)
{
    uint64_t idleLayerCount = closingLayer - sourceLayer - 1;
    bool spanMiddle = options.idleSlotCap > 0 && idleLayerCount > 2 * options.idleSlotCap;
    LayerIndex middleFirst = sourceLayer + options.idleSlotCap + 1;
    LayerIndex middleLast = closingLayer - options.idleSlotCap - 1;
    for (LayerIndex layer = sourceLayer + 1; layer <= closingLayer; ++layer) {
        // The middle slot spans its layers, every other slot spans one
        LayerIndex lastLayer = spanMiddle && layer == middleFirst ? middleLast : layer;
        SlotIndex next = layer == closingLayer ? destination : addSlot(model, layer, lastLayer);
        addWireEdge(model, options, previous, next);
        chainSlots.insert(chainSlots.end(), lastLayer - layer + 1, next);
        previous = next;
        layer = lastLayer;
    }
}

/// Canonical color of a raw solver color, numbered in order of first appearance
static SubCircuitIndex canonicalColor(
    std::unordered_map<SubCircuitIndex, SubCircuitIndex> &canonical,
    SubCircuitIndex rawColor)
{
    return canonical.try_emplace(rawColor, canonical.size()).first->second;
}

/// Color the subgates of the graph with the OR-Tools CP-SAT solver, minimizing 4^wireCuts *
/// 6^gateCuts. Each wire pads its idle layers with a chain of idle slots from padIdleLayers.
static CutSolution solveCut(CircuitGraph &graph, CircuitCutOptions &options)
{
    using namespace operations_research::sat;

    uint64_t realSubGateCount = graph.subGateCount;
    uint64_t rawCap = graph.lines.size();

    // TODO rawCap follows the qubit line count, one subcircuit color per line, which is the
    // cap of a coloring that never reuses a color across lines. The width constraint below
    // already forces at least ceil(p / maxWidth) subcircuits, where p is the peak layer
    // occupancy over layerOccupants, so we could instead cap at that bound plus a margin.
    SubCircuitIndex cap =
        (options.maxSubcircuits > 0 && options.maxSubcircuits < rawCap) ? options.maxSubcircuits : rawCap;

    // layerOccupants lists for every layer the slots occupying it, both real subgates and idle
    // slots. The width constraint limits how many of them may share a color.
    LayerIndex layerCount = realSubGateCount == 0 ? 0 : *llvm::max_element(graph.schedule) + 1;
    auto model = CutModel{};
    model.cap = cap;
    model.layerOccupants.resize(layerCount);
    for (SubGateIndex subGate = 0; subGate < realSubGateCount; ++subGate) {
        addSlot(model, graph.schedule[subGate], graph.schedule[subGate]);
    }

    // [WireIndex], a vector of the chain slots of that wire, one per layer, both ends included.
    // We read their colors into CutSolution::wireChainColors once we solve the model
    auto wireChainSlots = std::vector<std::vector<SlotIndex>>();
    for (Wire &wire : graph.wires) {
        // The destination sits at least one layer behind the source, so the range covers the
        // idle layers and then the layer of the destination itself, which closes the chain
        LayerIndex sourceLayer = graph.schedule[wire.source];
        LayerIndex closingLayer = graph.schedule[wire.destination];
        auto &chainSlots = wireChainSlots.emplace_back();
        chainSlots.push_back(wire.source);
        padIdleLayers(model, options, wire.source, sourceLayer, wire.destination, closingLayer, chainSlots);
    }

    uint64_t slotCount = model.slotColor.size();

    // gateCut[g] is true iff the subgates of gate g land in more than one subcircuit. We fix a
    // gate uncut when gate cuts are disabled. Otherwise the differs variables record where a
    // cuttable gate differs from its first subgate.
    uint64_t gateCount = graph.gates.size();
    auto gateCut = std::vector<BoolVar>(gateCount);
    for (GateIndex gateIndex = 0; gateIndex < gateCount; ++gateIndex) {
        gateCut[gateIndex] = model.builder.NewBoolVar();
        std::vector<SubGateIndex> &subGates = graph.gates[gateIndex].subGates;
        auto differsFromFirst = std::vector<BoolVar>();
        ArrayRef<BoolVar> first = model.slotColor[subGates[0]];
        for (size_t position = 1; position < subGates.size(); ++position) {
            ArrayRef<BoolVar> other = model.slotColor[subGates[position]];
            std::optional<BoolVar> differs;
            if (options.allowGateCuts) {
                differs = model.builder.NewBoolVar();
                differsFromFirst.push_back(*differs);
            }
            for (SubCircuitIndex color = 0; color < cap; ++color) {
                model.builder.AddEquality(first[color], other[color]).OnlyEnforceIf(gateCut[gateIndex].Not());
                if (differs) {
                    model.builder.AddBoolOr({first[color].Not(), other[color].Not()}).OnlyEnforceIf(*differs);
                }
            }
        }
        if (options.allowGateCuts) {
            model.builder.AddBoolOr(differsFromFirst).OnlyEnforceIf(gateCut[gateIndex]);
        } else {
            model.builder.FixVariable(gateCut[gateIndex], false);
        }
    }

    // Width at most maxWidth for every layer and color, the peak qubit occupancy
    for (std::vector<SlotIndex> &layer : model.layerOccupants) {
        for (SubCircuitIndex color = 0; color < cap; ++color) {
            LinearExpr width;
            for (SlotIndex slot : layer) {
                width += model.slotColor[slot][color];
            }
            model.builder.AddLessOrEqual(width, options.maxWidth);
        }
    }

    // Minimize wireCutWeight * wireCuts + gateCutWeight * gateCuts. The coefficients are the
    // log(4) and log(6) classical reconstruction overhead weights scaled by about 259 and rounded.
    constexpr int64_t wireCutWeight = 359;
    constexpr int64_t gateCutWeight = 464;
    model.builder.Minimize(wireCutWeight * LinearExpr::Sum(model.wireCut) + gateCutWeight * LinearExpr::Sum(gateCut));

    // Fixed seed so repeated runs on the same circuit pick the same cut
    constexpr int32_t solverSeed = 42;

    SatParameters parameters;
    // A worker count of 0 means we use the number of hardware CPU cores. hardware_concurrency
    // itself may report 0 when it cannot tell, in which case we fall back to a single worker.
    int64_t requestedWorkers = options.solverWorkers > 0 ? options.solverWorkers : std::thread::hardware_concurrency();
    parameters.set_num_workers(std::max<int64_t>(requestedWorkers, 1));
    parameters.set_random_seed(solverSeed);
    if (options.solverTimeLimitSeconds > 0) {
        parameters.set_max_time_in_seconds(options.solverTimeLimitSeconds);
    }
    auto solveStart = std::chrono::steady_clock::now();
    CpSolverResponse response = SolveWithParameters(model.builder.Build(), parameters);
    double solveSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - solveStart).count();

    CutSolution solution;
    solution.solveSeconds = solveSeconds;

    if (response.status() != CpSolverStatus::OPTIMAL && response.status() != CpSolverStatus::FEASIBLE) {
        solution.status = CutStatus::Infeasible;
        return solution;
    }
    solution.status = response.status() == CpSolverStatus::OPTIMAL ? CutStatus::Optimal : CutStatus::Feasible;

    // Raw solver color of every slot, real and idle, which we count and canonicalize below
    auto rawColor = std::vector<SubCircuitIndex>(slotCount, 0);
    for (SlotIndex slot = 0; slot < slotCount; ++slot) {
        for (SubCircuitIndex color = 0; color < cap; ++color) {
            if (SolutionBooleanValue(response, model.slotColor[slot][color])) {
                rawColor[slot] = color;
                break;
            }
        }
    }

    // Count distinct subcircuits over all slots
    solution.subcircuitCount = std::unordered_set<SubCircuitIndex>(rawColor.begin(), rawColor.end()).size();

    // Real subgate colors, relabelled in order of first appearance for determinism
    solution.subGateColor.assign(realSubGateCount, 0);
    auto canonical = std::unordered_map<SubCircuitIndex, SubCircuitIndex>();
    for (SubGateIndex subGate = 0; subGate < realSubGateCount; ++subGate) {
        solution.subGateColor[subGate] = canonicalColor(canonical, rawColor[subGate]);
    }

    // Extend the same canonicalization to the colors of the chain slots
    solution.wireChainColors.reserve(wireChainSlots.size());
    for (std::vector<SlotIndex> &chainSlots : wireChainSlots) {
        std::vector<SubCircuitIndex> &chainColors = solution.wireChainColors.emplace_back();
        chainColors.reserve(chainSlots.size());
        for (SlotIndex slot : chainSlots) {
            chainColors.push_back(canonicalColor(canonical, rawColor[slot]));
        }
    }

    return solution;
}

// CUT COLLECTION
/////////////////

/// A gate is cut when its subgates do not all share one subcircuit color
static bool gateIsCut(ArrayRef<SubGateIndex> subGates, ArrayRef<SubCircuitIndex> subGateColor)
{
    SubCircuitIndex firstColor = subGateColor[subGates.front()];
    return llvm::any_of(subGates, [&](SubGateIndex subGate) { return subGateColor[subGate] != firstColor; });
}

/// Translate a cut solution into the MLIR gates and wires to cut, one entry per cut, in
/// program order
static CutResult collectCuts(CircuitGraph &graph, CutSolution &solution)
{
    CutResult result;
    result.status = solution.status;
    result.solveSeconds = solution.solveSeconds;
    if (solution.status == CutStatus::Infeasible) {
        return result;
    }
    result.subcircuitCount = solution.subcircuitCount;

    // A gate is cut when its subgates land in more than one subcircuit
    for (GateIndex gate = 0; gate < graph.gates.size(); ++gate) {
        if (gateIsCut(graph.gates[gate].subGates, solution.subGateColor)) {
            result.gateCuts.push_back(graph.gates[gate].operation);
        }
    }

    // A wire edge is cut when its two slots land in different subcircuits, and we report the
    // values of the wire once per cut edge
    for (WireIndex wireIndex = 0; wireIndex < graph.wires.size(); ++wireIndex) {
        std::vector<SubCircuitIndex> &chainColors = solution.wireChainColors[wireIndex];
        for (size_t k = 1; k < chainColors.size(); ++k) {
            if (chainColors[k] != chainColors[k - 1]) {
                result.wireCuts.push_back(graph.wires[wireIndex].passthrough);
            }
        }
    }

    return result;
}

// PRETTY PRINTING
//////////////////

/// Narrowest grid column, which both grids widen to fit their longest cell
static constexpr size_t minimumColumnWidth = 3;

/// Single letter that wraps, 0->A, 25->Z, 26->A again, so labels repeat past 26 gates
static std::string letterLabel(uint64_t index) { return std::string(1, 'A' + index % 26); }

/// row label, padded with spaces to the length of the longest label, so the grid starts
/// at the same column in every row even once q10 and above appear
static std::string lineLabel(LineIndex line, uint64_t lineCount)
{
    size_t digitWidth = std::to_string(lineCount - 1).size();
    std::string label = "q" + std::to_string(line);
    label.append(digitWidth + 1 - label.size(), ' ');
    return label;
}

/// Name of the gate operation without its dialect prefix, e.g. "CNOT" for "quantum.CNOT"
static std::string gateName(Operation* operation)
{
    std::string full = operation->getName().getStringRef().str();
    size_t dot = full.rfind('.');
    return dot == std::string::npos ? full : full.substr(dot + 1);
}

/// Compact debug grid, one row per qubit line and one column per scheduled layer. Each cell
/// shows the gate name and its letter, plus the color when a solution is given.
static void printScheduledGrid(CircuitGraph &graph, CutSolution* solution, llvm::StringRef title, llvm::raw_ostream &os)
{
    uint64_t lineCount = graph.lines.size();
    uint64_t layerCount = graph.subGateCount == 0 ? 0 : *llvm::max_element(graph.schedule) + 1;
    os << title << "  (" << layerCount << " layers, " << lineCount << " lines)\n";
    if (lineCount == 0 || layerCount == 0) {
        os << "  (empty)\n\n";
        return;
    }

    // Fill the cells of each line at their scheduled layer. Consecutive line subgates sit at
    // strictly increasing layers, so we never overwrite a cell.
    auto grid = std::vector<std::vector<std::string>>(lineCount, std::vector<std::string>(layerCount));
    size_t columnWidth = minimumColumnWidth;
    for (LineIndex line = 0; line < lineCount; ++line) {
        for (SubGateIndex subGate : graph.lines[line]) {
            GateIndex gateIndex = graph.subGateToGate[subGate];
            std::string cell = gateName(graph.gates[gateIndex].operation);
            // Every gate gets a letter, so cells with the same letter belong to the same gate
            cell += ":" + letterLabel(gateIndex);
            if (solution && solution->status != CutStatus::Infeasible) {
                cell += "#" + std::to_string(solution->subGateColor[subGate]);
            }
            columnWidth = std::max(columnWidth, cell.size());
            grid[line][graph.schedule[subGate]] = std::move(cell);
        }
    }

    for (LineIndex line = 0; line < lineCount; ++line) {
        os << "  " << lineLabel(line, lineCount);
        for (LayerIndex layer = 0; layer < layerCount; ++layer) {
            std::string &cell = grid[line][layer];
            os << "|" << cell << std::string(columnWidth - cell.size(), ' ');
        }
        os << "|\n";
    }
    os << "\n";
}

/// One palette entry, a background color paired with a foreground that stays readable on it
struct AnsiSwatch {
    int background;
    int foreground;
};

/// 256-color terminal palette, one entry per subcircuit. Colors repeat past 7 subcircuits,
/// which is rare.
static constexpr std::array<AnsiSwatch, 7> kAnsiPalette = {
    AnsiSwatch{196, 16},
    AnsiSwatch{21, 231},
    AnsiSwatch{46, 16},
    AnsiSwatch{51, 16},
    AnsiSwatch{226, 16},
    AnsiSwatch{208, 16},
    AnsiSwatch{129, 231},
};

static constexpr char kAnsiReset[] = "\033[0m";

/// SGR escape sequence for the palette entry of color. Cut gates are also bold and underlined,
/// so their cells read as one gate even though their colors differ.
static std::string ansiStyle(SubCircuitIndex color, bool cut)
{
    AnsiSwatch swatch = kAnsiPalette[color % kAnsiPalette.size()];
    return "\033[" + std::string(cut ? "1;4;" : "") + "38;5;" + std::to_string(swatch.foreground) + ";48;5;"
           + std::to_string(swatch.background) + "m";
}

/// Terminal-colored rendering of a solved cut in the grid of printScheduledGrid. Each cell takes
/// the background color of its subcircuit, and cut gates are bold, underlined and marked with a
/// trailing asterisk. A wire cut shows as a color change along a line, so it needs no marker of
/// its own.
static void printColoredGrid(CircuitGraph &graph, CutSolution &solution, int64_t maxWidth, llvm::raw_ostream &os)
{
    uint64_t lineCount = graph.lines.size();
    if (solution.status == CutStatus::Infeasible) {
        os << "CIRCUIT CUT: infeasible\n\n";
        return;
    }
    uint64_t layerCount = graph.subGateCount == 0 ? 0 : *llvm::max_element(graph.schedule) + 1;
    os << "CIRCUIT CUT:  (" << layerCount << " layers, " << lineCount << " lines, " << solution.subcircuitCount
       << " subcircuits, max-width " << maxWidth << ")\n"
       << "  legend: color = subcircuit (a color change along one line is a wire cut), "
          "trailing * = gate cut\n";
    if (lineCount == 0 || layerCount == 0) {
        os << "  (empty)\n\n";
        return;
    }

    struct Cell {
        std::string text;
        SubCircuitIndex color = 0;
        bool cut = false;
        bool present = false; // holds a gate
        bool filled = false;  // idle slot between two gates of this line, colored like its slot
    };
    auto grid = std::vector<std::vector<Cell>>(lineCount, std::vector<Cell>(layerCount));
    size_t columnWidth = minimumColumnWidth;
    for (LineIndex line = 0; line < lineCount; ++line) {
        auto &subGates = graph.lines[line];
        for (size_t i = 0; i < subGates.size(); ++i) {
            SubGateIndex subGate = subGates[i];
            GateIndex gateIndex = graph.subGateToGate[subGate];
            bool cut = gateIsCut(graph.gates[gateIndex].subGates, solution.subGateColor);
            std::string text = gateName(graph.gates[gateIndex].operation);
            text += ":" + letterLabel(gateIndex);
            if (cut) {
                text += "*";
            }
            columnWidth = std::max(columnWidth, text.size());

            Cell &cell = grid[line][graph.schedule[subGate]];
            cell.text = std::move(text);
            cell.color = solution.subGateColor[subGate];
            cell.cut = cut;
            cell.present = true;

            // Color each idle layer between the two subgates with the color of its chain slot, so
            // every wire cut across several idle layers shows as a color change
            if (i + 1 < subGates.size()) {
                WireIndex wireIndex = *graph.outgoingWire[subGate];
                std::vector<SubCircuitIndex> &chainColors = solution.wireChainColors[wireIndex];
                SubGateIndex next = subGates[i + 1];
                for (LayerIndex layer = graph.schedule[subGate] + 1; layer < graph.schedule[next]; ++layer) {
                    grid[line][layer].color = chainColors[layer - graph.schedule[subGate]];
                    grid[line][layer].filled = true;
                }
            }
        }
    }

    for (LineIndex line = 0; line < lineCount; ++line) {
        os << "  " << lineLabel(line, lineCount);
        for (LayerIndex layer = 0; layer < layerCount; ++layer) {
            os << "|";
            Cell &cell = grid[line][layer];
            if (cell.present) {
                os << ansiStyle(cell.color, cell.cut) << cell.text << kAnsiReset
                   << std::string(columnWidth - cell.text.size(), ' ');
            } else if (cell.filled) {
                os << ansiStyle(cell.color, false) << std::string(columnWidth, ' ') << kAnsiReset;
            } else {
                os << std::string(columnWidth, ' ');
            }
        }
        os << "|\n";
    }
    os << "\n";
}

/// One machine-readable line of cut-quality metrics for benchmarks
static void printStats(CutResult &result, llvm::raw_ostream &os)
{
    if (result.status == CutStatus::Infeasible) {
        os << "circuit-cut-stats: feasible=0" << llvm::format(" solve-seconds=%.3f", result.solveSeconds) << "\n";
        return;
    }
    int64_t wireCuts = result.wireCuts.size();
    int64_t gateCuts = result.gateCuts.size();
    double normalizedCutCount = wireCuts + gateCuts * std::log(6.0) / std::log(4.0);
    double overheadMagnitude = wireCuts * std::log10(4.0) + gateCuts * std::log10(6.0);
    os << "circuit-cut-stats: feasible=1"
       << " wire-cuts=" << wireCuts << " gate-cuts=" << gateCuts
       << llvm::format(" normalized-cut-count=%.2f", normalizedCutCount)
       << llvm::format(" overhead-magnitude=%.2f", overheadMagnitude) << " subcircuits=" << result.subcircuitCount
       << llvm::format(" solve-seconds=%.3f", result.solveSeconds) << "\n";
}

/// Report the gate and wire cuts as remarks, which the tests check with -verify-diagnostics
static void emitCutRemarks(func::FuncOp root, CutResult &result)
{
    if (result.status == CutStatus::Infeasible) {
        root->emitRemark("circuit cut infeasible");
        return;
    }
    for (Operation* gate : result.gateCuts) {
        gate->emitRemark("gate cut");
    }

    // We emit one remark per wire cut, at the source of its wire, the front of its values
    for (WireValues &wire : result.wireCuts) {
        SingleQubit &source = wire.front();
        auto sourceResult = cast<OpResult>(source.multiQubit);
        std::string message = "wire cut: output " + std::to_string(sourceResult.getResultNumber());
        if (cast<QubitType>(sourceResult.getType()).getSize() > 1) {
            message += " qubit " + std::to_string(source.multiQubitIndex);
        }
        emitRemark(source.multiQubit.getLoc(), message);
    }
}

// PASS CONSTRUCTION
////////////////////

struct CircuitCutAnalysisPass : mlir::quantum::impl::CircuitCutAnalysisBase<CircuitCutAnalysisPass> {
    using CircuitCutAnalysisBase::CircuitCutAnalysisBase;
    void runOnOperation() override
    {
        auto root = getOperation();
        auto options = CircuitCutOptions{
            maxWidth,
            allowGateCuts,
            allowWireCuts,
            schedulePolicy,
            maxSubcircuits,
            solverTimeLimitSeconds,
            solverWorkers,
            idleSlotCap};
        auto graph = buildCircuitGraph(root, options);
        auto solution = solveCut(graph, options);
        auto result = collectCuts(graph, solution);

        // debug option, to see what schedules we get
        if (emitSchedules) {
            printScheduledGrid(graph, nullptr, "SCHEDULE:", llvm::outs());
        }

        // debug option, to see the scheduled graph
        if (emitGraph) {
            printScheduledGrid(graph, &solution, "SCHEDULED GRAPH:", llvm::outs());
        }

        // debug option to see the colored scheduled grid
        if (emitColor) {
            printColoredGrid(graph, solution, options.maxWidth, llvm::outs());
        }

        // debug option, print stats for benchmarks
        if (emitStats) {
            printStats(result, llvm::outs());
        }

        llvm::outs().flush();

        // debug option, print the results out for tests
        if (emitRemarks) {
            emitCutRemarks(root, result);
        }
    }
};

std::unique_ptr<Pass> mlir::quantum::createCircuitCutAnalysisPass()
{
    return std::make_unique<CircuitCutAnalysisPass>();
}

CircuitCutAnalysis::CircuitCutAnalysis(Operation* op, CircuitCutOptions &options)
{
    auto root = cast<func::FuncOp>(op);
    auto graph = buildCircuitGraph(root, options);
    auto solution = solveCut(graph, options);
    result = collectCuts(graph, solution);
}
