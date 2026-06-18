//===--- CircuitCutAnalysis.h - Wire / Gate Cut Analysis----------*- C++-*-===//
//
// @author  Daniel Arsenev
//===----------------------------------------------------------------------===//

#ifndef QUANTUM_MLIR_QUANTUM_ANALYSIS_CIRCUIT_CUT_ANALYSIS_H
#define QUANTUM_MLIR_QUANTUM_ANALYSIS_CIRCUIT_CUT_ANALYSIS_H

#include <algorithm>
#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <mlir/IR/Operation.h>
#include <mlir/IR/Value.h>
#include <set>
#include <vector>

namespace mlir {
namespace quantum {

// Gate cuts require that we calculate the expected value of the quantum circuit

enum QubitReusePolicy {
    NoReuse,        // no reuse at all
    QubitLineReuse, // fast and handles simple reuse cases. should be the default in most cases.
    ComplexReuse    // probably prohibitively expensive but produces the best results
};

struct CircuitCutOptions {
    int64_t targetWidth;
    bool allowGateCuts;
    bool allowWireCuts;
    QubitReusePolicy reusePolicy;
    int64_t cutWeight;
    int64_t fidelityWeight;
};

/// A single qubit, represented as a multi-qubit MLIR value together with the
/// position of the qubit within it.
struct SingleQubit {
    Value multiQubit;
    uint64_t multiQubitIndex;
};

/// A single graph wire materialises as an ordered chain of single qubits (the
/// qubit as it passes through splits, merges and single-qubit plumbing).
/// Cutting the wire means cutting any one qubit along that chain.
using WireValues = std::vector<SingleQubit>;

/// Orders wire chains so they can live in a std::set.
struct WireValuesLess {
    bool operator()(const WireValues &lhs, const WireValues &rhs) const
    {
        return std::lexicographical_compare(
            lhs.begin(),
            lhs.end(),
            rhs.begin(),
            rhs.end(),
            [](const SingleQubit &a, const SingleQubit &b) {
                if (a.multiQubit.getAsOpaquePointer() != b.multiQubit.getAsOpaquePointer()) {
                    return a.multiQubit.getAsOpaquePointer() < b.multiQubit.getAsOpaquePointer();
                }
                return a.multiQubitIndex < b.multiQubitIndex;
            });
    }
};

// For simplicity we consider only gate cutting of primitive n qubit gates
// The algorithm can be expanded to handle non primitive gates (for example by inlining).
// Also we do not consider control flow yet.

class CircuitCutAnalysis {
public:
    /// Compute the circuit cut for the given options.
    CircuitCutAnalysis(Operation* op, const CircuitCutOptions &options);

    bool isFeasible() const { return feasible; }

    /// The gates that must be cut to reach the target circuit width.
    const std::set<Operation*> &gatesToCut() const { return gateCuts; }

    /// The wires that must be cut to reach the target circuit width.
    const std::set<WireValues, WireValuesLess> &wiresToCut() const { return wireCuts; }

private:
    bool feasible;
    std::set<Operation*> gateCuts;
    std::set<WireValues, WireValuesLess> wireCuts;
};

} // namespace quantum
} // namespace mlir

#endif
