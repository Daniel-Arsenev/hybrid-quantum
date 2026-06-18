//===- Passes.td - Quantum dialect passes ---------------------*- tablegen -*-===//
//
// This is the definitions file for the Quantum dialect transform passes.
//
//===----------------------------------------------------------------------===//

#ifndef QUANTUM_PASSES
#define QUANTUM_PASSES

include "mlir/Pass/PassBase.td"

def QuantumOptimise : Pass<"quantum-optimise", "ModuleOp"> {
  let summary = "Optimise the `quantum` dialect";

  let description = [{
  }];

  let constructor = "mlir::quantum::createQuantumOptimisePass()";
}

def HermitianCancel : Pass<"hermitian-cancel", "ModuleOp"> {
  let summary = "Hermitian Cancellation on the `quantum` dialect";

  let description = [{
  }];

  let constructor = "mlir::quantum::createHermitianCancelPass()";
}

def CircuitCutAnalysis : Pass<"circuit-cut-analysis", "func::FuncOp"> {
  let summary = "Circuit cut analysis on the `quantum` dialect";

  let description = [{
    Computes a partition of a quantum circuit into subcircuits whose width
    (number of qubits) does not exceed `target-width`, by selecting a set of
    gate cuts and/or wire cuts. The set of cuts is chosen to minimize cutting
    cost subject to the configured `reuse-policy`, which controls how freed
    qubits may be reused when computing a subcircuit's width.

    The pass only computes the cut; it does not rewrite the IR. With
    `emit-remarks`, the chosen gate and wire cuts are reported as diagnostic
    remarks, which is intended for testing with `-verify-diagnostics`.
  }];

  let options = [
    Option<"targetWidth", "target-width", "int64_t",
           /*default=*/"std::numeric_limits<int64_t>::max()",
           "Maximum number of qubits allowed per subcircuit">,
    Option<"allowGateCuts", "allow-gate-cuts", "bool", /*default=*/"true",
           "Allow gates to be cut across subcircuits">,
    Option<"allowWireCuts", "allow-wire-cuts", "bool", /*default=*/"true",
           "Allow wires to be cut across subcircuits">,
    Option<"reusePolicy", "reuse-policy", "mlir::quantum::QubitReusePolicy",
           /*default=*/"mlir::quantum::QubitLineReuse",
           "Qubit reuse policy used when computing subcircuit width",
           [{::llvm::cl::values(
              clEnumValN(mlir::quantum::NoReuse, "none", "No qubit reuse"),
              clEnumValN(mlir::quantum::QubitLineReuse, "line", "Reuse along whole qubit lines"),
              clEnumValN(mlir::quantum::ComplexReuse, "complex", "Reachability-based qubit reuse"))}]>,
    Option<"cutWeight", "cut-weight", "int64_t", /*default=*/"1",
           "Weight on the post-processing (cut count) objective. Scales the cost "
           "of each wire and gate cut, trading off against the fidelity weight">,
    Option<"fidelityWeight", "fidelity-weight", "int64_t", /*default=*/"0",
           "Weight on the fidelity objective. Penalises the largest number of "
           "two-qubit gates in any subcircuit, encouraging balancing them across "
           "subcircuits. Zero is cuts-only">,
    Option<"emitRemarks", "emit-remarks", "bool", /*default=*/"false",
           "Emit the computed gate/wire cut set as diagnostic remarks "
           "(intended for testing with -verify-diagnostics)">,
  ];

  let constructor = "mlir::quantum::createCircuitCutAnalysisPass()";
}

#endif // QUANTUM_PASSES
