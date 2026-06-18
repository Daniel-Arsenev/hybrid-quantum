//===--- CircuitCutAnalysis.cpp - Wire / Gate Cut Analysis--------*- C++-*-===//
//
// @author  Daniel Arsenev
//===----------------------------------------------------------------------===//

#include "quantum-mlir/Dialect/Quantum/Analysis/CircuitCutAnalysis.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/AsmState.h"
#include "mlir/IR/Operation.h"
#include "mlir/IR/Value.h"
#include "ortools/sat/cp_model.h"
#include "quantum-mlir/Dialect/Quantum/IR/QuantumOps.h"
#include "quantum-mlir/Dialect/Quantum/IR/QuantumTypes.h"
#include "quantum-mlir/Dialect/Quantum/Interfaces/GateOpInterface.h"
#include "quantum-mlir/Dialect/Quantum/Transforms/Passes.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <queue>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace mlir::quantum {

#define GEN_PASS_DEF_CIRCUITCUTANALYSIS
#include "quantum-mlir/Dialect/Quantum/Transforms/Passes.h.inc"

} // namespace mlir::quantum

using namespace mlir;
using namespace mlir::quantum;

/// DATATYPE DEFINITION
///////////////////////

using OperandIndex = uint64_t; // indexes into a gate
using QubitIndex = uint64_t;   // indexes into a multiqubit or multigate

using SubGateIndex = uint64_t;   // indexes into the graph subgate vector
using GateIndex = uint64_t;      // indexes into the graph gate vector
using MultiGateIndex = uint64_t; // indexes into the graph multigate vector
using WireIndex = uint64_t;      // indexes into the graph wire vector
using LineIndex = uint64_t;      // indexes into the graph qubit line vector

using SubCircuitIndex = uint64_t;

struct Allocated {}; // incoming: qubit freshly allocated, no prior gate
struct Destroyed {}; // outgoing: qubit measured, no subsequent gate
struct Returned {};  // outgoing: qubit returned from the function

using IncomingWireConnection = std::variant<WireIndex, Allocated>;
using OutgoingWireConnection = std::variant<WireIndex, Destroyed, Returned>;

/// for the gate graph representation we consider every gate to be made up of one
/// qubit sub gates working on one wide qubit values
struct SubGate {
    OperandIndex operandIndex;
    QubitIndex qubitIndex;
};

/// every n qubit gate is made up of n subgates
struct Gate {
    std::vector<SubGateIndex> subGates; // [OperandIndex] - could be a llvm small vector
    QubitIndex qubitIndex;
};

/// every multigate on an n wide qubit value is made up of n gates
struct MultiGate {
    std::vector<GateIndex> gates; // [QubitIndex] within this multigate
};

struct Wire {
    SubGateIndex from;
    SubGateIndex to;
};

struct CircuitGraph {

    // basic graph nodes and edges
    std::vector<SubGate> subGates;     // [SubGateIndex] - nodes in the circuit graph
    std::vector<Wire> wires;           // [WireIndex] - directed wire edges between nodes
    std::vector<Gate> gates;           // [GateIndex] - undirected gate edges between nodes
    std::vector<MultiGate> multiGates; // [MultiGateIndex]

    // reverse node lookups
    std::vector<GateIndex> subGateToGate;        // [SubGateIndex] -> GateIndex
    std::vector<MultiGateIndex> gateToMultiGate; // [GateIndex] -> MultiGateIndex

    // wire connectivity lookups
    std::vector<IncomingWireConnection> incomingWire; // [SubGateIndex] -> WireIndex or Allocated Unit
    std::vector<OutgoingWireConnection> outgoingWire; // [SubGateIndex] -> WireIndex or Destroyed or Returned Unit

    // subgate lifecycle lookup
    std::vector<SubGateIndex> allocatedSubGates; // [SubGateIndex] - nodes with no input wire
    std::vector<SubGateIndex> destroyedSubGates; // [SubGateIndex] - nodes with no output wire (measured)
    std::vector<SubGateIndex> returnedSubGates;  // [SubGateIndex] - nodes with no output wire (returned)

    // qubit lines
    std::vector<std::vector<SubGateIndex>> qubitLines; // [LineIndex] - ordered subgate chains

    // qubit line reverse lookup
    std::vector<LineIndex> subGateQubitLine;     // [SubGateIndex] -> LineIndex
    std::vector<uint64_t> subGatePositionOnLine; // [SubGateIndex] -> position within that line
};

/// MLIR-specific data parallel to CircuitGraph, kept separate to keep the
/// graph pure. Indexed by the same positions as the graph.
struct CircuitGraphAnnotations {
    Operation* asmContextOp;
    std::vector<Operation*> gateOps;                    // [GateIndex] -> Operation*
    std::vector<std::vector<SingleQubit>> passthroughs; // [WireIndex] -> qubit chain
};

/// ReachedGate carries the MLIR Operation* for the traced destination gate.
/// It is a transient result of traceThroughOps and is never stored in the
/// graph.
struct ReachedGate {
    Operation* gate;
    OperandIndex operandIndex;
    QubitIndex qubitIndex;
};
struct ReachedEnd {};
struct ReachedMeasurement {};
struct ReachedUnknown {};

using TraceResult = std::variant<ReachedGate, ReachedEnd, ReachedMeasurement, ReachedUnknown>;

/// CIRCUIT CUT ANALYSIS
////////////////////////

/// Iteratively trace one qubit through non-gate plumbing (split/merge/measurements)
/// and single-qubit gates, respecting qubit ordering, until an n-qubit gate is reached.
std::pair<TraceResult, std::vector<SingleQubit>> traceThroughOps(SingleQubit ref)
{
    std::vector<SingleQubit> passthrough;
    while (true) {
        passthrough.push_back(ref);

        auto uses = ref.multiQubit.getUses();
        if (uses.empty()) { return {ReachedEnd{}, std::move(passthrough)}; }

        OpOperand &use = *uses.begin();
        Operation* destination = use.getOwner();
        uint64_t landedOperandNumber = use.getOperandNumber();

        // for n-qubit gate we return here
        // for single-qubit gate we record it and continue through its qubit result.
        // gates may take non-qubit operands, we have to consider that.
        if (isa<GateOpInterface>(destination)) {
            uint64_t qubitOperandCount = 0;
            uint64_t qubitOperandIndex = 0;
            for (auto [operandIndex, operand] : llvm::enumerate(destination->getOperands())) {
                if (!isa<QubitType>(operand.getType())) { continue; }
                if (operandIndex < landedOperandNumber) { ++qubitOperandIndex; }
                ++qubitOperandCount;
            }
            if (qubitOperandCount > 1) {
                return {
                    ReachedGate{destination, qubitOperandIndex, ref.multiQubitIndex},
                    std::move(passthrough)
                };
            } else {
                // a single-qubit gate has exactly one qubit result (possibly
                // parametrised by non-qubit operands); follow that qubit result.
                Value qubitResult;
                for (Value result : destination->getResults()) {
                    if (isa<QubitType>(result.getType())) {
                        qubitResult = result;
                        break;
                    }
                }
                if (!qubitResult) { return {ReachedUnknown{}, std::move(passthrough)}; }
                ref = {qubitResult, ref.multiQubitIndex};
            }
        }

        // we are splitting the n qubit value. find the new m qubit value we are in
        // and adjust the index accordingly
        else if (isa<SplitOp>(destination)) {
            uint64_t multiQubitIndex = ref.multiQubitIndex;
            for (Value result : destination->getResults()) {
                auto quantumType = dyn_cast<QubitType>(result.getType());
                uint64_t multiQubitWidth = static_cast<uint64_t>(quantumType.getSize());
                if (multiQubitIndex >= multiQubitWidth) {
                    multiQubitIndex -= multiQubitWidth;
                } else {
                    ref = {result, multiQubitIndex};
                    break;
                }
            }
        }

        // we are merging the n qubit value with some others. adjust the index
        // accordingly
        else if (isa<MergeOp>(destination)) {
            uint64_t multiQubitIndex = ref.multiQubitIndex;
            for (uint64_t i = 0; i < landedOperandNumber; ++i) {
                auto quantumType = dyn_cast<QubitType>(destination->getOperand(i).getType());
                multiQubitIndex += static_cast<uint64_t>(quantumType.getSize());
            }
            Value result = destination->getResult(0);
            ref = {result, multiQubitIndex};
        }

        else if (isa<MeasureOp>(destination)) {
            return {ReachedMeasurement{}, std::move(passthrough)};
        }

        else if (isa<ResetOp>(destination)) {
            return {ReachedEnd{}, std::move(passthrough)};
        }

        else if (isa<DeallocateOp>(destination)) {
            return {ReachedEnd{}, std::move(passthrough)};
        }

        else {
            return {ReachedUnknown{}, std::move(passthrough)};
        }
    }
}

std::pair<CircuitGraph, CircuitGraphAnnotations> buildCircuitGraph(func::FuncOp root)
{

    CircuitGraph graph;
    CircuitGraphAnnotations annotations;
    annotations.asmContextOp = root;
    std::unordered_map<Operation*, MultiGateIndex> lookup;

    // Gather gate nodes. Single-qubit gates are not represented as graph nodes.
    // Each op decomposes into one Gate per qubit in its multi-qubit operand,
    // and each Gate gets one SubGate per operand slot.
    for (Operation &op : root.getBody().front()) {
        if (!isa<GateOpInterface>(op) || op.getNumResults() == 1) { continue; }

        MultiGateIndex newMultiGateIndex = graph.multiGates.size();
        lookup.insert_or_assign(&op, newMultiGateIndex);
        MultiGate &multiGate = graph.multiGates.emplace_back();

        QubitType quantumType = dyn_cast<QubitType>(op.getResult(0).getType());
        uint64_t multiGateWidth = quantumType.getSize();
        for (uint64_t multiQubitIndex = 0; multiQubitIndex < multiGateWidth; ++multiQubitIndex) {
            GateIndex gateIndex = graph.gates.size();
            multiGate.gates.push_back(gateIndex);

            Gate &gate = graph.gates.emplace_back();
            gate.qubitIndex = multiQubitIndex;
            graph.gateToMultiGate.push_back(newMultiGateIndex);
            annotations.gateOps.push_back(&op);

            uint64_t qubitIndex = 0;
            for (Value operand : op.getOperands()) {
                if (!isa<QubitType>(operand.getType())) { continue; }
                SubGateIndex subGateIndex = graph.subGates.size();
                gate.subGates.push_back(subGateIndex);
                graph.subGateToGate.push_back(gateIndex);
                graph.subGates.emplace_back(qubitIndex, multiQubitIndex);
                ++qubitIndex;
            }
        }
    }

    const uint64_t subGateCount = graph.subGates.size();

    // Gather Wire edges between SubGates by tracing each n-qubit gate result through any
    // intervening single-qubit ops to its consumer. Results that reach a measurement are
    // marked Destroyed, results that leave the function without hitting a tracked gate are
    // marked Returned, all others become a Wire connecting source and destination SubGates.
    graph.incomingWire.assign(subGateCount, IncomingWireConnection(Allocated{}));
    graph.outgoingWire.assign(subGateCount, OutgoingWireConnection(Returned{}));
    for (Operation &sourceOp : root.getBody().front()) {
        if (!isa<GateOpInterface>(sourceOp) || sourceOp.getNumResults() == 1) { continue; }
        for (auto [resultIndex, result] : llvm::enumerate(sourceOp.getResults())) {
            QubitType quantumType = dyn_cast<QubitType>(result.getType());
            uint64_t multiGateWidth = quantumType.getSize();
            for (uint64_t multiQubitIndex = 0; multiQubitIndex < multiGateWidth; ++multiQubitIndex) {

                MultiGateIndex multiGateIndex = lookup.at(&sourceOp);
                MultiGate &sourceMultigate = graph.multiGates[multiGateIndex];

                GateIndex sourceGateIndex = sourceMultigate.gates[multiQubitIndex];
                Gate &sourceGate = graph.gates[sourceGateIndex];
                SubGateIndex sourceSubGateIndex = sourceGate.subGates[resultIndex];

                auto [outcome, passthrough] = traceThroughOps({result, multiQubitIndex});
                if (std::holds_alternative<ReachedMeasurement>(outcome)) {
                    graph.destroyedSubGates.push_back(sourceSubGateIndex);
                    graph.outgoingWire[sourceSubGateIndex] = Destroyed{};
                    continue;
                }
                auto* traceGate = std::get_if<ReachedGate>(&outcome);
                if (!traceGate || !lookup.contains(traceGate->gate)) {
                    graph.returnedSubGates.push_back(sourceSubGateIndex);
                    graph.outgoingWire[sourceSubGateIndex] = Returned{};
                    continue;
                }

                MultiGateIndex destMultiGateIndex = lookup.at(traceGate->gate);
                MultiGate &destMultigate = graph.multiGates[destMultiGateIndex];

                GateIndex destGateIndex = destMultigate.gates[traceGate->qubitIndex];
                Gate &destGate = graph.gates[destGateIndex];
                SubGateIndex destSubGateIndex = destGate.subGates[traceGate->operandIndex];

                WireIndex newWireIndex = graph.wires.size();
                graph.wires.push_back({sourceSubGateIndex, destSubGateIndex});
                annotations.passthroughs.push_back(std::move(passthrough));

                graph.outgoingWire[sourceSubGateIndex] = newWireIndex;
                graph.incomingWire[destSubGateIndex] = newWireIndex;
            }
        }
    }
    for (SubGateIndex subGateIndex = 0; subGateIndex < subGateCount; ++subGateIndex) {
        if (std::holds_alternative<Allocated>(graph.incomingWire[subGateIndex])) {
            graph.allocatedSubGates.push_back(subGateIndex);
        }
    }

    // Gather qubit lines
    for (SubGateIndex startSubGateIndex : graph.allocatedSubGates) {
        std::vector<SubGateIndex> &line = graph.qubitLines.emplace_back();
        SubGateIndex current = startSubGateIndex;
        while (true) {
            line.push_back(current);
            auto* nextWireIndex = std::get_if<WireIndex>(&graph.outgoingWire[current]);
            if (!nextWireIndex) { break; }
            current = graph.wires[*nextWireIndex].to;
        }
    }
    graph.subGateQubitLine.resize(subGateCount);
    graph.subGatePositionOnLine.resize(subGateCount);
    for (LineIndex lineIndex = 0; lineIndex < graph.qubitLines.size(); ++lineIndex) {
        const auto &qubitLine = graph.qubitLines[lineIndex];
        for (uint64_t position = 0; position < qubitLine.size(); ++position) {
            graph.subGateQubitLine[qubitLine[position]] = lineIndex;
            graph.subGatePositionOnLine[qubitLine[position]] = position;
        }
    }

    return {std::move(graph), std::move(annotations)};
}

struct CutSolution {
    bool feasible;
    std::vector<int32_t> subGateSubcircuit; // [SubGateIndex] -> SubCircuitIndex
};

/// Solve the circuit-cut using OR-Tools CP-SAT.
CutSolution solveCircuitCut(const CircuitGraph &graph, CircuitCutOptions options)
{
    using namespace operations_research::sat;

    const uint64_t subGateCount = graph.subGates.size();
    const uint64_t wireCount = graph.wires.size();
    const uint64_t gateCount = graph.gates.size();

    // todo: find a lower estimate for the maximimum number of sub circuits
    // maye even qubit reuse dependent
    const uint64_t maxSubcircuitCount = subGateCount;

    CpModelBuilder cp;

    // subgateColor[subGate][subCircuit] is true iff that subgate is in the
    // subCircuit
    auto subGateSubcircuits = std::vector<std::vector<BoolVar>>(subGateCount, std::vector<BoolVar>(maxSubcircuitCount));
    for (SubGateIndex subGateIndex = 0; subGateIndex < subGateCount; ++subGateIndex) {
        for (SubCircuitIndex subCircuitIndex = 0; subCircuitIndex < maxSubcircuitCount; ++subCircuitIndex) {
            subGateSubcircuits[subGateIndex][subCircuitIndex] = cp.NewBoolVar();
        }
        cp.AddExactlyOne(subGateSubcircuits[subGateIndex]);
    }

    // wireCut[wireIndex] is true iff the wire crosses a subcircuit boundary.
    auto wireCut = std::vector<BoolVar>(wireCount);
    for (WireIndex wireIndex = 0; wireIndex < wireCount; ++wireIndex) {
        wireCut[wireIndex] = cp.NewBoolVar();
        const SubGateIndex sourceSubGateIndex = graph.wires[wireIndex].from;
        const SubGateIndex destSubGateIndex = graph.wires[wireIndex].to;
        for (SubCircuitIndex subCircuitIndex = 0; subCircuitIndex < maxSubcircuitCount; ++subCircuitIndex) {
            cp.AddImplication(
                  subGateSubcircuits[sourceSubGateIndex][subCircuitIndex],
                  subGateSubcircuits[destSubGateIndex][subCircuitIndex])
                .OnlyEnforceIf(wireCut[wireIndex].Not());
            cp.AddImplication(
                  subGateSubcircuits[destSubGateIndex][subCircuitIndex],
                  subGateSubcircuits[sourceSubGateIndex][subCircuitIndex])
                .OnlyEnforceIf(wireCut[wireIndex].Not());
            cp.AddBoolOr({subGateSubcircuits[sourceSubGateIndex][subCircuitIndex].Not(),
                          subGateSubcircuits[destSubGateIndex][subCircuitIndex].Not()})
                .OnlyEnforceIf(wireCut[wireIndex]);
        }
    }

    // wire cutting disabled: no wire may cross a subcircuit boundary
    if (!options.allowWireCuts) {
        for (WireIndex wireIndex = 0; wireIndex < wireCount; ++wireIndex) { cp.AddBoolAnd({wireCut[wireIndex].Not()}); }
    }

    // gateCut[gateIndex] is true iff the SubGates of the Gate span more than one
    // subcircuit.
    auto gateCut = std::vector<BoolVar>(gateCount);
    for (GateIndex gateIndex = 0; gateIndex < gateCount; ++gateIndex) {
        gateCut[gateIndex] = cp.NewBoolVar();
        const Gate &gate = graph.gates[gateIndex];
        if (gate.subGates.size() <= 1) {
            cp.AddBoolAnd({gateCut[gateIndex].Not()});
            continue;
        }
        const SubGateIndex firstSubGateIndex = gate.subGates[0];
        auto differsFromFirst = std::vector<BoolVar>();
        for (size_t i = 1; i < gate.subGates.size(); ++i) {
            const SubGateIndex otherSubGateIndex = gate.subGates[i];
            auto differs = cp.NewBoolVar();
            differsFromFirst.push_back(differs);
            for (SubCircuitIndex subCircuitIndex = 0; subCircuitIndex < maxSubcircuitCount; ++subCircuitIndex) {
                cp.AddImplication(
                      subGateSubcircuits[firstSubGateIndex][subCircuitIndex],
                      subGateSubcircuits[otherSubGateIndex][subCircuitIndex])
                    .OnlyEnforceIf(gateCut[gateIndex].Not());
                cp.AddImplication(
                      subGateSubcircuits[otherSubGateIndex][subCircuitIndex],
                      subGateSubcircuits[firstSubGateIndex][subCircuitIndex])
                    .OnlyEnforceIf(gateCut[gateIndex].Not());
                cp.AddBoolOr({subGateSubcircuits[firstSubGateIndex][subCircuitIndex].Not(),
                              subGateSubcircuits[otherSubGateIndex][subCircuitIndex].Not()})
                    .OnlyEnforceIf(differs);
            }
        }
        cp.AddBoolOr(differsFromFirst).OnlyEnforceIf(gateCut[gateIndex]);
    }

    // even if cutting is allowed, gates feeding into measurements cannot be gate-cut
    if (options.allowGateCuts) {
        auto incomingSourceGate = std::vector<GateIndex>(subGateCount, UINT64_MAX);
        for (const Wire &wire : graph.wires) { incomingSourceGate[wire.to] = graph.subGateToGate[wire.from]; }
        std::unordered_set<GateIndex> gateCutBlocked;
        std::queue<GateIndex> worklist;
        for (SubGateIndex subGateIndex : graph.destroyedSubGates) {
            GateIndex gateIndex = graph.subGateToGate[subGateIndex];
            if (gateCutBlocked.insert(gateIndex).second) { worklist.push(gateIndex); }
        }
        while (!worklist.empty()) {
            GateIndex gateIndex = worklist.front();
            worklist.pop();
            const Gate &blockedGate = graph.gates[gateIndex];
            for (SubGateIndex subGateIndex : blockedGate.subGates) {
                GateIndex sourceGate = incomingSourceGate[subGateIndex];
                if (sourceGate != UINT64_MAX && gateCutBlocked.insert(sourceGate).second) { worklist.push(sourceGate); }
            }
        }
        for (GateIndex gateIndex : gateCutBlocked) {
            if (gateIndex < gateCount) { cp.AddBoolAnd({gateCut[gateIndex].Not()}); }
        }
    }

    // gate cutting disabled: no gate may span more than one subcircuit
    else {
        for (GateIndex gateIndex = 0; gateIndex < gateCount; ++gateIndex) { cp.AddBoolAnd({gateCut[gateIndex].Not()}); }
    }

    // width is the number of gates with no wires going into them in a subircuit
    if (options.reusePolicy == NoReuse) {
        for (SubCircuitIndex subCircuitIndex = 0; subCircuitIndex < maxSubcircuitCount; ++subCircuitIndex) {
            LinearExpr width;
            for (SubGateIndex allocatedSubGateIndex : graph.allocatedSubGates) {
                width += subGateSubcircuits[allocatedSubGateIndex][subCircuitIndex];
            }
            for (WireIndex wireIndex = 0; wireIndex < wireCount; ++wireIndex) {
                const SubGateIndex destSubGateIndex = graph.wires[wireIndex].to;
                // a cut wire leaves its destination with no incoming wire inside this
                // subcircuit, so the destination is allocated here.
                const auto allocatedByCut = cp.NewBoolVar();
                cp.AddBoolOr(
                    {wireCut[wireIndex].Not(),
                     subGateSubcircuits[destSubGateIndex][subCircuitIndex].Not(),
                     allocatedByCut});
                width += allocatedByCut;
            }
            cp.AddLessOrEqual(width, options.targetWidth);
        }
    }

    // width is the number of qubit lines in a subcircuit
    else if (options.reusePolicy == QubitLineReuse) {
        const uint64_t lineCount = graph.qubitLines.size();
        auto width = std::vector<LinearExpr>(maxSubcircuitCount);
        for (LineIndex lineIndex = 0; lineIndex < lineCount; ++lineIndex) {
            for (SubCircuitIndex subCircuitIndex = 0; subCircuitIndex < maxSubcircuitCount; ++subCircuitIndex) {
                // lineInSubcircuit is true if some subgate of the line is in this subcircuit.
                BoolVar lineInSubcircuit = cp.NewBoolVar();
                for (SubGateIndex subGateIndex : graph.qubitLines[lineIndex]) {
                    cp.AddImplication(subGateSubcircuits[subGateIndex][subCircuitIndex], lineInSubcircuit);
                }
                width[subCircuitIndex] += lineInSubcircuit;
            }
        }
        for (SubCircuitIndex subCircuitIndex = 0; subCircuitIndex < maxSubcircuitCount; ++subCircuitIndex) {
            cp.AddLessOrEqual(width[subCircuitIndex], options.targetWidth);
        }
    }

    // width is determined by qubit reuse
    else if (options.reusePolicy == ComplexReuse) {
        // step 1: determine the sources and sinks of all sub circuits
        // a subgate is allocated iff
        // 1) it is in the graph.allocatedSubGates vector, OR
        // 2) it has an edge into it where the other side of the edge is in a different subcircuit
        // a subgate is returned iff
        // 1) it is in the graph.returnedSubGates vector
        // a subgate is destroyed iff
        // 1) it is in the graph.destroyedSubGates vector, OR
        // 2) it has an edge out of it where the other side of the edge is in a different subcircuit
        auto isAllocated = std::vector<BoolVar>(subGateCount);
        auto isReturned = std::vector<BoolVar>(subGateCount);
        auto isDestroyed = std::vector<BoolVar>(subGateCount);
        for (SubGateIndex subGateIndex = 0; subGateIndex < subGateCount; ++subGateIndex) {
            isAllocated[subGateIndex] = cp.NewBoolVar();
            isReturned[subGateIndex] = cp.NewBoolVar();
            isDestroyed[subGateIndex] = cp.NewBoolVar();
        }
        for (SubGateIndex subGateIndex = 0; subGateIndex < subGateCount; ++subGateIndex) {
            if (auto* wire = std::get_if<WireIndex>(&graph.incomingWire[subGateIndex])) {
                cp.AddImplication(wireCut[*wire], isAllocated[subGateIndex]);
                cp.AddImplication(isAllocated[subGateIndex], wireCut[*wire]);
            } else {
                cp.AddBoolAnd({isAllocated[subGateIndex]});
            }

            if (auto* wire = std::get_if<WireIndex>(&graph.outgoingWire[subGateIndex])) {
                cp.AddBoolAnd({isReturned[subGateIndex].Not()});
                cp.AddImplication(wireCut[*wire], isDestroyed[subGateIndex]);
                cp.AddImplication(isDestroyed[subGateIndex], wireCut[*wire]);
            } else if (std::holds_alternative<Returned>(graph.outgoingWire[subGateIndex])) {
                cp.AddBoolAnd({isReturned[subGateIndex]});
                cp.AddBoolAnd({isDestroyed[subGateIndex].Not()});
            } else {
                cp.AddBoolAnd({isReturned[subGateIndex].Not()});
                cp.AddBoolAnd({isDestroyed[subGateIndex]});
            }
        }

        // step 2: subgate reachability within a subcircuit.
        // subgate b is reachable from subgate a iff there is a path from a to b
        // along wire edges and gate edges that are all uncut (and hence all in the
        // same subcircuit). this is the data dependence that rules out reuse.
        auto combinedReachMatrix = std::vector<std::vector<BoolVar>>(subGateCount, std::vector<BoolVar>(subGateCount));
        for (SubGateIndex startSubGateIndex = 0; startSubGateIndex < subGateCount; ++startSubGateIndex) {
            for (SubGateIndex endSubGateIndex = 0; endSubGateIndex < subGateCount; ++endSubGateIndex) {
                combinedReachMatrix[startSubGateIndex][endSubGateIndex] = cp.NewBoolVar();
            }
        }
        // base case: wire edges
        for (WireIndex wireIndex = 0; wireIndex < wireCount; ++wireIndex) {
            SubGateIndex sourceSubGateIndex = graph.wires[wireIndex].from;
            SubGateIndex destSubGateIndex = graph.wires[wireIndex].to;
            cp.AddImplication(combinedReachMatrix[sourceSubGateIndex][destSubGateIndex], wireCut[wireIndex].Not());
            cp.AddImplication(wireCut[wireIndex].Not(), combinedReachMatrix[sourceSubGateIndex][destSubGateIndex]);
        }
        // base case: gate edges
        for (GateIndex gateIndex = 0; gateIndex < graph.gates.size(); ++gateIndex) {
            const Gate &gate = graph.gates[gateIndex];
            for (size_t i = 0; i < gate.subGates.size(); ++i) {
                SubGateIndex subGateIndexI = gate.subGates[i];
                for (size_t j = 0; j < gate.subGates.size(); ++j) {
                    SubGateIndex subGateIndexJ = gate.subGates[j];
                    cp.AddImplication(combinedReachMatrix[subGateIndexI][subGateIndexJ], gateCut[gateIndex].Not());
                    cp.AddImplication(gateCut[gateIndex].Not(), combinedReachMatrix[subGateIndexI][subGateIndexJ]);
                }
            }
        }
        // inductive case
        for (SubGateIndex midSubGateIndex = 0; midSubGateIndex < subGateCount; ++midSubGateIndex) {
            for (SubGateIndex startSubGateIndex = 0; startSubGateIndex < subGateCount; ++startSubGateIndex) {
                for (SubGateIndex endSubGateIndex = 0; endSubGateIndex < subGateCount; ++endSubGateIndex) {
                    if (startSubGateIndex == endSubGateIndex || startSubGateIndex == midSubGateIndex
                        || midSubGateIndex == endSubGateIndex) {
                        continue;
                    }
                    cp.AddBoolAnd({combinedReachMatrix[startSubGateIndex][endSubGateIndex]})
                        .OnlyEnforceIf(
                            {combinedReachMatrix[startSubGateIndex][midSubGateIndex],
                             combinedReachMatrix[midSubGateIndex][endSubGateIndex]});
                }
            }
        }

        // step 3: qubit reuse.
        // reuse[d][a] is true iff the physical qubit freed when subgate d is
        // destroyed is reused by the partial qubit line that begins at allocated subgate a.
        // a reuse is legal only if d is destroyed, a is allocated, both are in the
        // same subcircuit, and there is no path from a to d.
        auto reuses = std::vector<std::vector<BoolVar>>(subGateCount, std::vector<BoolVar>(subGateCount));
        auto incomingReuse = std::vector<LinearExpr>(subGateCount);
        auto outgoingReuse = std::vector<LinearExpr>(subGateCount);
        for (SubGateIndex destroyedSubGateIndex = 0; destroyedSubGateIndex < subGateCount; ++destroyedSubGateIndex) {
            for (SubGateIndex allocatedSubGateIndex = 0; allocatedSubGateIndex < subGateCount;
                 ++allocatedSubGateIndex) {
                if (allocatedSubGateIndex == destroyedSubGateIndex) { continue; }
                reuses[destroyedSubGateIndex][allocatedSubGateIndex] = cp.NewBoolVar();
                cp.AddImplication(
                    reuses[destroyedSubGateIndex][allocatedSubGateIndex],
                    isDestroyed[destroyedSubGateIndex]);
                cp.AddImplication(
                    reuses[destroyedSubGateIndex][allocatedSubGateIndex],
                    isAllocated[allocatedSubGateIndex]);
                cp.AddImplication(
                    reuses[destroyedSubGateIndex][allocatedSubGateIndex],
                    combinedReachMatrix[allocatedSubGateIndex][destroyedSubGateIndex].Not());
                for (SubCircuitIndex subCircuitIndex = 0; subCircuitIndex < maxSubcircuitCount; ++subCircuitIndex) {
                    cp.AddImplication(
                          subGateSubcircuits[destroyedSubGateIndex][subCircuitIndex],
                          subGateSubcircuits[allocatedSubGateIndex][subCircuitIndex])
                        .OnlyEnforceIf(reuses[destroyedSubGateIndex][allocatedSubGateIndex]);
                    cp.AddImplication(
                          subGateSubcircuits[allocatedSubGateIndex][subCircuitIndex],
                          subGateSubcircuits[destroyedSubGateIndex][subCircuitIndex])
                        .OnlyEnforceIf(reuses[destroyedSubGateIndex][allocatedSubGateIndex]);
                }
                incomingReuse[allocatedSubGateIndex] += reuses[destroyedSubGateIndex][allocatedSubGateIndex];
                outgoingReuse[destroyedSubGateIndex] += reuses[destroyedSubGateIndex][allocatedSubGateIndex];
            }
        }
        // each freed qubit is reused at most once, and each partial qubit line
        // reuses at most one freed qubit. reusesQubit[a] is whether partial qubit
        // line a reuses an earlier freed qubit (and so does not need a fresh one).
        auto reusesQubit = std::vector<BoolVar>(subGateCount);
        for (SubGateIndex subGateIndex = 0; subGateIndex < subGateCount; ++subGateIndex) {
            reusesQubit[subGateIndex] = cp.NewBoolVar();
            cp.AddEquality(reusesQubit[subGateIndex], incomingReuse[subGateIndex]);
            cp.AddLessOrEqual(outgoingReuse[subGateIndex], 1);
        }
        // allocFresh[a] is true iff a subGate is allocated
        // and does not reuse an earlier freed qubit.
        auto allocFresh = std::vector<BoolVar>(subGateCount);
        for (SubGateIndex subGateIndex = 0; subGateIndex < subGateCount; ++subGateIndex) {
            allocFresh[subGateIndex] = cp.NewBoolVar();
            cp.AddBoolAnd({isAllocated[subGateIndex], reusesQubit[subGateIndex].Not()})
                .OnlyEnforceIf(allocFresh[subGateIndex]);
            cp.AddBoolOr({isAllocated[subGateIndex].Not(), reusesQubit[subGateIndex]})
                .OnlyEnforceIf(allocFresh[subGateIndex].Not());
        }
        // reachability alone fails at independent partial qubit lines. to prevent
        // something like "p reuses q" and "q reuses p" we introduce reuseOrder to
        // pin down a single execution order by asserting the rank of a subgate
        // strictly increases along every reuse edge
        auto reuseOrder = std::vector<IntVar>(subGateCount);
        for (SubGateIndex subGateIndex = 0; subGateIndex < subGateCount; ++subGateIndex) {
            reuseOrder[subGateIndex] = cp.NewIntVar(operations_research::Domain(0, static_cast<int64_t>(subGateCount)));
        }
        for (SubGateIndex destroyedSubGateIndex = 0; destroyedSubGateIndex < subGateCount; ++destroyedSubGateIndex) {
            for (SubGateIndex allocatedSubGateIndex = 0; allocatedSubGateIndex < subGateCount;
                 ++allocatedSubGateIndex) {
                if (allocatedSubGateIndex == destroyedSubGateIndex) { continue; }
                cp.AddGreaterThan(reuseOrder[allocatedSubGateIndex], reuseOrder[destroyedSubGateIndex])
                    .OnlyEnforceIf(reuses[destroyedSubGateIndex][allocatedSubGateIndex]);
            }
        }

        // step 4: width of a subcircuit is the number of physical qubits it needs
        for (SubCircuitIndex subCircuitIndex = 0; subCircuitIndex < maxSubcircuitCount; ++subCircuitIndex) {
            LinearExpr width;
            for (SubGateIndex subGateIndex = 0; subGateIndex < subGateCount; ++subGateIndex) {
                BoolVar freshHere = cp.NewBoolVar();
                cp.AddBoolAnd({allocFresh[subGateIndex], subGateSubcircuits[subGateIndex][subCircuitIndex]})
                    .OnlyEnforceIf(freshHere);
                cp.AddBoolOr({allocFresh[subGateIndex].Not(), subGateSubcircuits[subGateIndex][subCircuitIndex].Not()})
                    .OnlyEnforceIf(freshHere.Not());
                width += freshHere;
            }
            cp.AddLessOrEqual(width, options.targetWidth);
        }
    }

    // subcircuitUsed[subCircuitIndex] is true iff at least one subgate is
    // assigned to that subcircuit.
    auto subcircuitUsed = std::vector<BoolVar>(maxSubcircuitCount);
    for (SubCircuitIndex subCircuitIndex = 0; subCircuitIndex < maxSubcircuitCount; ++subCircuitIndex) {
        subcircuitUsed[subCircuitIndex] = cp.NewBoolVar();
        for (SubGateIndex subGateIndex = 0; subGateIndex < subGateCount; ++subGateIndex) {
            cp.AddImplication(subGateSubcircuits[subGateIndex][subCircuitIndex], subcircuitUsed[subCircuitIndex]);
        }
    }

    // maxTwoQubitGates tracks the largest number of two-qubit gates in any one
    // subcircuit.
    IntVar maxTwoQubitGates = cp.NewIntVar(operations_research::Domain(0, static_cast<int64_t>(gateCount)));
    if (options.fidelityWeight != 0) {
        for (SubCircuitIndex subCircuitIndex = 0; subCircuitIndex < maxSubcircuitCount; ++subCircuitIndex) {
            LinearExpr twoQubitGatesHere;
            for (GateIndex gateIndex = 0; gateIndex < gateCount; ++gateIndex) {
                const Gate &gate = graph.gates[gateIndex];
                if (gate.subGates.size() <= 1) { continue; }
                // the gate is intact and in this subcircuit iff it is not gate-cut
                // and its first subgate is coloured with this subcircuit.
                BoolVar gateHere = cp.NewBoolVar();
                cp.AddBoolAnd({gateCut[gateIndex].Not(), subGateSubcircuits[gate.subGates[0]][subCircuitIndex]})
                    .OnlyEnforceIf(gateHere);
                cp.AddBoolOr({gateCut[gateIndex], subGateSubcircuits[gate.subGates[0]][subCircuitIndex].Not()})
                    .OnlyEnforceIf(gateHere.Not());
                twoQubitGatesHere += gateHere;
            }
            cp.AddLessOrEqual(twoQubitGatesHere, maxTwoQubitGates);
        }
    }

    // objective is to minimise wireCuts*ln(4) + gateCuts*ln(6), scaled by 10
    // ln(4) is about 14 and ln(6) is about 18, plus a configurable cost per
    // subcircuit used to discourage fragmenting the circuit. The fidelity term
    // adds a configurable penalty on the worst subcircuit's two-qubit gate count.
    LinearExpr objective;
    for (const auto &wireCutVariable : wireCut) {
        objective += LinearExpr::Term(wireCutVariable, options.cutWeight * 14);
    }
    for (const auto &gateCutVariable : gateCut) {
        objective += LinearExpr::Term(gateCutVariable, options.cutWeight * 18);
    }
    for (const auto &subcircuitUsedVariable : subcircuitUsed) {
        objective += LinearExpr::Term(subcircuitUsedVariable, 1);
    }
    if (options.fidelityWeight != 0) { objective += LinearExpr::Term(maxTwoQubitGates, options.fidelityWeight); }
    cp.Minimize(objective);

    // solve. pin the solver to a single worker with a fixed seed so the
    // returned cut set is reproducible across runs.
    SatParameters parameters;
    parameters.set_num_workers(1);
    parameters.set_random_seed(42);
    const CpSolverResponse resp = SolveWithParameters(cp.Build(), parameters);

    if (resp.status() != CpSolverStatus::OPTIMAL && resp.status() != CpSolverStatus::FEASIBLE) { return {false, {}}; }

    // read the subcircuit assignment from the solver. he solver picks subcircuit
    // indices arbitrarily, so we relabel the subcircuits in order of first appearance
    // so the returned assignment uses a contiguous range.
    auto rawSubcircuit = std::vector<int32_t>(subGateCount, 0);
    for (uint64_t subGateIndex = 0; subGateIndex < subGateCount; ++subGateIndex) {
        for (uint64_t subCircuitIndex = 0; subCircuitIndex < maxSubcircuitCount; ++subCircuitIndex) {
            if (SolutionBooleanValue(resp, subGateSubcircuits[subGateIndex][subCircuitIndex])) {
                rawSubcircuit[subGateIndex] = static_cast<int32_t>(subCircuitIndex);
                break;
            }
        }
    }
    auto canonicalLabel = std::unordered_map<int32_t, int32_t>();
    auto subcircuits = std::vector<int32_t>(subGateCount, 0);
    for (uint64_t subGateIndex = 0; subGateIndex < subGateCount; ++subGateIndex) {
        int32_t raw = rawSubcircuit[subGateIndex];
        auto it = canonicalLabel.find(raw);
        if (it == canonicalLabel.end()) {
            int32_t nextLabel = static_cast<int32_t>(canonicalLabel.size());
            it = canonicalLabel.emplace(raw, nextLabel).first;
        }
        subcircuits[subGateIndex] = it->second;
    }

    return {true, std::move(subcircuits)};
}

/// PRETTY PRINTING
///////////////////

enum class Align { Left, Center };

struct Column {
    std::string header;
    Align align;
};

struct Table {
    std::string title;
    std::vector<Column> columns;
    std::vector<std::vector<std::string>> rows;

    void addRow(std::vector<std::string> row) { rows.push_back(std::move(row)); }

    std::string padRight(const std::string &text, size_t width) const
    {
        if (text.size() >= width) { return text; }
        return text + std::string(width - text.size(), ' ');
    }

    std::string padCenter(const std::string &text, size_t width) const
    {
        if (text.size() >= width) { return text; }
        size_t total = width - text.size();
        size_t left = total / 2;
        return std::string(left, ' ') + text + std::string(total - left, ' ');
    }

    void print() const
    {
        auto widths = std::vector<size_t>(columns.size());
        for (size_t columnIndex = 0; columnIndex < columns.size(); ++columnIndex) {
            widths[columnIndex] = columns[columnIndex].header.size();
        }
        for (const auto &row : rows) {
            for (size_t columnIndex = 0; columnIndex < row.size(); ++columnIndex) {
                widths[columnIndex] = std::max(widths[columnIndex], row[columnIndex].size());
            }
        }

        llvm::outs() << title << "\n";
        llvm::outs() << "  ";
        for (size_t columnIndex = 0; columnIndex < columns.size(); ++columnIndex) {
            if (columnIndex > 0) { llvm::outs() << "  "; }
            const Column &col = columns[columnIndex];
            llvm::outs()
                << (col.align == Align::Center ? padCenter(col.header, widths[columnIndex])
                                               : padRight(col.header, widths[columnIndex]));
        }
        llvm::outs() << "\n";
        for (const auto &row : rows) {
            llvm::outs() << "  ";
            for (size_t columnIndex = 0; columnIndex < row.size(); ++columnIndex) {
                if (columnIndex > 0) { llvm::outs() << "  "; }
                const Column &col = columns[columnIndex];
                llvm::outs()
                    << (col.align == Align::Center ? padCenter(row[columnIndex], widths[columnIndex])
                                                   : padRight(row[columnIndex], widths[columnIndex]));
            }
            llvm::outs() << "\n";
        }
        llvm::outs() << "\n";
    }
};

/// Print the circuit graph nodes, wires, and cut coloring for debugging.
void printCircuitGraph(
    const CircuitGraph &graph,
    const CircuitGraphAnnotations &annotations,
    const CutSolution &solution)
{
    std::unique_ptr<AsmState> asmState;
    if (annotations.asmContextOp) { asmState = std::make_unique<AsmState>(annotations.asmContextOp); }

    auto isGateCut = std::vector<bool>(graph.gates.size(), false);
    if (solution.feasible && !solution.subGateSubcircuit.empty()) {
        for (GateIndex gateIndex = 0; gateIndex < graph.gates.size(); ++gateIndex) {
            const Gate &gate = graph.gates[gateIndex];
            if (gate.subGates.empty()) { continue; }
            int32_t firstColor = solution.subGateSubcircuit[gate.subGates[0]];
            for (SubGateIndex subGateIndex : gate.subGates) {
                if (solution.subGateSubcircuit[subGateIndex] != firstColor) {
                    isGateCut[gateIndex] = true;
                    break;
                }
            }
        }
    }

    auto allocatedSet =
        std::unordered_set<SubGateIndex>(graph.allocatedSubGates.begin(), graph.allocatedSubGates.end());
    auto measuredSet = std::unordered_set<SubGateIndex>(graph.destroyedSubGates.begin(), graph.destroyedSubGates.end());
    auto releasedSet = std::unordered_set<SubGateIndex>(graph.returnedSubGates.begin(), graph.returnedSubGates.end());

    Table gateTable{
        "GATES:",
        {{"gate", Align::Center}, {"op", Align::Left}, {"cut", Align::Center}, {"nodes", Align::Left}},
        {},
    };
    for (GateIndex gateIndex = 0; gateIndex < graph.gates.size(); ++gateIndex) {
        const Gate &gate = graph.gates[gateIndex];
        std::string opName;
        if (gateIndex < annotations.gateOps.size() && annotations.gateOps[gateIndex]) {
            opName = annotations.gateOps[gateIndex]->getName().getStringRef().str();
        }
        std::string cut = "?";
        bool isBlocked = false;
        for (SubGateIndex subGateIndex : gate.subGates) {
            if (measuredSet.count(subGateIndex)) {
                isBlocked = true;
                break;
            }
        }
        if (isBlocked) {
            cut = "blk";
        } else if (solution.feasible) {
            cut = isGateCut[gateIndex] ? "yes" : "no";
        }
        std::string nodes;
        for (SubGateIndex subGateIndex : gate.subGates) {
            if (!nodes.empty()) { nodes += ","; }
            nodes += std::to_string(subGateIndex);
        }
        gateTable.addRow({std::to_string(gateIndex), opName, std::move(cut), std::move(nodes)});
    }
    gateTable.print();

    Table nodeTable{
        "NODES:",
        {{"node", Align::Center},
          {"gate", Align::Center},
          {"op", Align::Left},
          {"operand", Align::Center},
          {"multiindex", Align::Center},
          {"subcircuit", Align::Center}},
        {},
    };
    for (SubGateIndex subGateIndex = 0; subGateIndex < graph.subGates.size(); ++subGateIndex) {
        const SubGate &subGate = graph.subGates[subGateIndex];
        std::string subcircuit = "?";
        if (solution.feasible && subGateIndex < solution.subGateSubcircuit.size()) {
            subcircuit = std::to_string(solution.subGateSubcircuit[subGateIndex]);
        }
        GateIndex gateIndex = graph.subGateToGate[subGateIndex];
        std::string opName;
        if (gateIndex < annotations.gateOps.size() && annotations.gateOps[gateIndex]) {
            opName = annotations.gateOps[gateIndex]->getName().getStringRef().str();
        }
        nodeTable.addRow({
            std::to_string(subGateIndex),
            std::to_string(gateIndex),
            std::move(opName),
            std::to_string(subGate.operandIndex),
            std::to_string(subGate.qubitIndex),
            std::move(subcircuit),
        });
    }
    nodeTable.print();

    auto printQubit = [&](Value value, MultiGateIndex qubitIndex) -> std::string {
        std::string result;
        auto outputStream = llvm::raw_string_ostream(result);
        value.printAsOperand(outputStream, *asmState);
        if (qubitIndex != 0) {
            result += "#";
            result += std::to_string(qubitIndex);
        }
        return result;
    };

    Table wireTable{
        "WIRES:",
        {{"wire", Align::Center},
          {"from", Align::Center},
          {"to", Align::Center},
          {"cut", Align::Center},
          {"passthroughs", Align::Left}},
        {},
    };
    for (size_t wireIndex = 0; wireIndex < graph.wires.size(); ++wireIndex) {
        const Wire &wire = graph.wires[wireIndex];
        std::string cut = "?";
        if (solution.feasible && wire.from < solution.subGateSubcircuit.size()
            && wire.to < solution.subGateSubcircuit.size()) {
            cut = (solution.subGateSubcircuit[wire.from] != solution.subGateSubcircuit[wire.to]) ? "yes" : "no";
        }
        std::string passthroughs;
        for (const SingleQubit &passthrough : annotations.passthroughs[wireIndex]) {
            if (!passthroughs.empty()) { passthroughs += " -> "; }
            passthroughs += printQubit(passthrough.multiQubit, passthrough.multiQubitIndex);
        }
        wireTable.addRow({
            std::to_string(wireIndex),
            std::to_string(wire.from),
            std::to_string(wire.to),
            std::move(cut),
            std::move(passthroughs),
        });
    }
    wireTable.print();

    auto joinIndices = [](const std::vector<SubGateIndex> &indices) -> std::string {
        std::string result;
        for (SubGateIndex subGateIndex : indices) {
            if (!result.empty()) { result += ","; }
            result += std::to_string(subGateIndex);
        }
        return result;
    };

    Table lifecycleTable{
        "LIFECYCLE:",
        {{"lifecycle", Align::Left}, {"nodes", Align::Left}},
        {},
    };
    lifecycleTable.addRow({"allocated", joinIndices(graph.allocatedSubGates)});
    lifecycleTable.addRow({"measured", joinIndices(graph.destroyedSubGates)});
    lifecycleTable.addRow({"released", joinIndices(graph.returnedSubGates)});
    lifecycleTable.print();

    llvm::outs().flush();
}

/// PASS CONSTRUCTION
/////////////////////

struct CircuitCutAnalysisPass : mlir::quantum::impl::CircuitCutAnalysisBase<CircuitCutAnalysisPass> {
    using CircuitCutAnalysisBase::CircuitCutAnalysisBase;
    void runOnOperation() override
    {
        auto analysis = CircuitCutAnalysis(
            getOperation(),
            CircuitCutOptions{targetWidth, allowGateCuts, allowWireCuts, reusePolicy, cutWeight, fidelityWeight});

        // Surface the analysis result as diagnostics so it can be checked with
        // -verify-diagnostics.
        if (!emitRemarks) { return; }
        if (!analysis.isFeasible()) {
            getOperation()->emitRemark("circuit cut infeasible");
            return;
        }
        for (Operation* gate : analysis.gatesToCut()) { gate->emitRemark("gate cut"); }

        for (const WireValues &wire : analysis.wiresToCut()) {
            const SingleQubit &source = wire.front();
            auto sourceResult = cast<OpResult>(source.multiQubit);
            std::string message = "wire cut: output " + std::to_string(sourceResult.getResultNumber());
            if (source.multiQubitIndex != 0) { message += "#" + std::to_string(source.multiQubitIndex); }
            emitRemark(source.multiQubit.getLoc(), message);
        }
    }
};

std::unique_ptr<Pass> mlir::quantum::createCircuitCutAnalysisPass()
{
    return std::make_unique<CircuitCutAnalysisPass>();
}

/// Translate the solver's subcircuit assignment back into the MLIR gates and
/// wire chains that must be cut.
std::pair<std::set<Operation*>, std::set<WireValues, WireValuesLess>> collectCuts(
    const CircuitGraph &graph,
    const CircuitGraphAnnotations &annotations,
    const CutSolution &solution)
{
    auto gateCuts = std::set<Operation*>();
    auto wireCuts = std::set<WireValues, WireValuesLess>();
    for (GateIndex gateIndex = 0; gateIndex < graph.gates.size(); ++gateIndex) {
        const Gate &gate = graph.gates[gateIndex];
        if (gate.subGates.empty()) { continue; }
        int32_t firstSubcircuit = solution.subGateSubcircuit[gate.subGates[0]];
        bool isGateCut = false;
        for (SubGateIndex subGateIndex : gate.subGates) {
            if (solution.subGateSubcircuit[subGateIndex] != firstSubcircuit) {
                isGateCut = true;
                break;
            }
        }
        if (isGateCut && gateIndex < annotations.gateOps.size() && annotations.gateOps[gateIndex]) {
            gateCuts.insert(annotations.gateOps[gateIndex]);
        }
    }

    for (WireIndex wireIndex = 0; wireIndex < graph.wires.size(); ++wireIndex) {
        const Wire &wire = graph.wires[wireIndex];
        if (solution.subGateSubcircuit[wire.from] == solution.subGateSubcircuit[wire.to]) { continue; }
        auto wireValues = WireValues(annotations.passthroughs[wireIndex]);
        wireCuts.insert(std::move(wireValues));
    }
    return {std::move(gateCuts), std::move(wireCuts)};
}

CircuitCutAnalysis::CircuitCutAnalysis(Operation* op, const CircuitCutOptions &options)
{
    func::FuncOp root = dyn_cast<func::FuncOp>(op);
    assert(root && "CircuitCutAnalysis requires a FunctionOpInterface op");
    auto [circuitGraph, annotations] = buildCircuitGraph(root);
    auto cutSolution = solveCircuitCut(circuitGraph, options);
    if ((feasible = cutSolution.feasible)) {
        std::tie(gateCuts, wireCuts) = collectCuts(circuitGraph, annotations, cutSolution);
    }
    printCircuitGraph(circuitGraph, annotations, cutSolution);
}
