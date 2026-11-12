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
    Computes a cut of a quantum circuit into subcircuits whose width does not exceed 
    `max-width`, by selecting a set of gate cuts and/or wire cuts. The set of cuts is 
    chosen to minimize the classical reconstruction overhead
  }];

  let options = [
    Option<"maxWidth", "max-width", "int64_t",
           /*default=*/"std::numeric_limits<int64_t>::max()",
           "Maximum number of qubits allowed per subcircuit">,
    Option<"allowGateCuts", "allow-gate-cuts", "bool", /*default=*/"true",
           "Allow gates to be cut across subcircuits">,
    Option<"allowWireCuts", "allow-wire-cuts", "bool", /*default=*/"true",
           "Allow wires to be cut across subcircuits">,
    Option<"schedulePolicy", "schedule-policy", "mlir::quantum::SchedulePolicy",
           /*default=*/"mlir::quantum::SchedulePolicy::LayeredForceDirected",
           "Schedule policy used when computing subcircuit width",
           [{::llvm::cl::values(
              clEnumValN(mlir::quantum::SchedulePolicy::LayeredAsap, "asap", "Layered reuse with an ASAP schedule"),
              clEnumValN(mlir::quantum::SchedulePolicy::LayeredAlap, "alap", "Layered reuse with an ALAP schedule"),
              clEnumValN(mlir::quantum::SchedulePolicy::LayeredForceDirected, "force-directed", "Layered reuse with a Paulin-Knight force-directed schedule"))}]>,
    Option<"maxSubcircuits", "max-subcircuits", "uint64_t", /*default=*/"0",
           "Cap on the number of subcircuits the solver may use (0 = use the "
           "qubit line count). Reducing this shrinks the CP-SAT model "
           "proportionally and speeds up the solve.">,
    Option<"solverTimeLimitSeconds", "solve-timeout-seconds", "int64_t", /*default=*/"0",
           "Time limit, in seconds, given to the CP-SAT solver (0 = "
           "unlimited). When the time limit is hit, the best cut found so far is "
           "reported (feasible but not proven optimal).">,
    Option<"solverWorkers", "solver-workers", "int64_t", /*default=*/"0",
           "Number of parallel search workers given to the CP-SAT solver "
           "(0 = the number of hardware CPU cores).">,
    Option<"emitRemarks", "emit-remarks", "bool", /*default=*/"false",
           "Emit the computed gate/wire cut set as diagnostic remarks "
           "(intended for testing with -verify-diagnostics)">,
    Option<"emitStats", "emit-stats", "bool", /*default=*/"false",
           "Emit a single machine-readable line of cut-quality metrics "
           "(feasibility, wire/gate cuts, normalized cut count, classical reconstruction overhead, "
           "subcircuits) for benchmarking">,
    Option<"emitSchedules", "emit-schedules", "bool", /*default=*/"false",
           "Pretty-print the gate schedule of the chosen schedule-policy as an "
           "ASCII circuit grid for debugging">,
    Option<"emitGraph", "emit-graph", "bool", /*default=*/"false",
           "Pretty-print the circuit-cut graph (subgates, wires, coloring) for "
           "debugging">,
    Option<"emitColor", "emit-color", "bool", /*default=*/"false",
           "Pretty-print the solved cut as an ANSI-colored terminal circuit "
           "grid for debugging">,
    Option<"idleSlotCap", "idle-slot-cap", "uint64_t", /*default=*/"2",
           "Cap idle-slot padding to this many slots at each end of a wire's idle "
           "layers once they exceed 2x this value (0 = no cap, pad every idle "
           "layer). The idle layers in the middle get spanned by one wide slot.">,
  ];

  let constructor = "mlir::quantum::createCircuitCutAnalysisPass()";
}

#endif // QUANTUM_PASSES
