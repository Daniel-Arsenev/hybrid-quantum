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
  }];

  let constructor = "mlir::quantum::createCircuitCutAnalysisPass()";
}

#endif // QUANTUM_PASSES
