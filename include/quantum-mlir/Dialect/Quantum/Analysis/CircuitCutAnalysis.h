//===--- CircuitCutAnalysis.h - Wire / Gate Cut Analysis----------*- C++-*-===//
//
// @author  Daniel Arsenev
//===----------------------------------------------------------------------===//

#ifndef QUANTUM_MLIR_QUANTUM_ANALYSIS_CIRCUIT_CUT_ANALYSIS_H
#define QUANTUM_MLIR_QUANTUM_ANALYSIS_CIRCUIT_CUT_ANALYSIS_H

#include "llvm/ADT/ArrayRef.h"

#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <mlir/IR/Operation.h>
#include <mlir/IR/Value.h>
#include <vector>

namespace mlir {
namespace quantum {

enum class SchedulePolicy {
    LayeredAsap,          // layered reuse with an ASAP schedule
    LayeredAlap,          // layered reuse with an ALAP schedule
    LayeredForceDirected, // layered reuse with a force-directed schedule
};

struct CircuitCutOptions {
    int64_t maxWidth;
    bool allowGateCuts;
    bool allowWireCuts;
    SchedulePolicy schedulePolicy;
    uint64_t maxSubcircuits = 0;        // 0 = use the qubit line count
    int64_t solverTimeLimitSeconds = 0; // 0 = unlimited
    int64_t solverWorkers = 0;          // 0 = number of hardware CPU cores
    uint64_t idleSlotCap = 2;           // 0 = no cap
};

/// A single qubit, represented as an m-wide qubit value together with the
/// index of the qubit within it
struct SingleQubit {
    Value multiQubit;
    uint64_t multiQubitIndex;
};

/// A single graph wire is an ordered list of single qubits, the qubit as it passes through
/// splits, merges and single qubit gates. Cutting the wire means cutting any one
/// qubit along the wire.
using WireValues = std::vector<SingleQubit>;

/// Outcome of a solver run. Optimal is a proven best cut, Feasible is a valid cut found before
/// the time limit but not proven best, and Infeasible means the solver found no cut.
enum class CutStatus { Infeasible, Feasible, Optimal };

/// The full result of a circuit-cut analysis, the chosen cuts plus the derived counts. Both
/// cut lists hold one entry per cut, in program order.
struct CutResult {
    CutStatus status = CutStatus::Infeasible;
    std::vector<Operation*> gateCuts;
    std::vector<WireValues> wireCuts;
    int64_t subcircuitCount = 0;
    double solveSeconds = 0.0;
};

/// Finds the gate and wire cuts that fit every subcircuit into the maximum width. For simplicity
/// we cut only primitive n qubit gates. We could handle non primitive gates by inlining, and we
/// do not consider control flow yet.
class CircuitCutAnalysis {
public:
    /// Compute the circuit cut for the given options
    CircuitCutAnalysis(Operation* op, CircuitCutOptions &options);

    /// Whether there exists a cut that fits every subcircuit into the maximum width
    bool isFeasible() { return result.status != CutStatus::Infeasible; }

    /// The gates that must be cut to fit every subcircuit into the maximum width
    ArrayRef<Operation*> gatesToCut() { return result.gateCuts; }

    /// The wires that must be cut to fit every subcircuit into the maximum width
    ArrayRef<WireValues> wiresToCut() { return result.wireCuts; }

private:
    CutResult result;
};

} // namespace quantum
} // namespace mlir

#endif
