// RUN: quantum-opt -circuit-cut-analysis='target-width=1 allow-gate-cuts=false allow-wire-cuts=true reuse-policy=line emit-remarks=true' -verify-diagnostics %s

// No two-qubit gates means no graph nodes to cut: feasible with no cuts at any
// width. The absence of any remark is the assertion.
func.func @edge_single_qubit() -> (!quantum.qubit<1>, !quantum.qubit<1>) {
  %a = "quantum.alloc"() : () -> !quantum.qubit<1>
  %b = "quantum.alloc"() : () -> !quantum.qubit<1>
  %a1 = "quantum.H"(%a) : (!quantum.qubit<1>) -> !quantum.qubit<1>
  %b1 = "quantum.X"(%b) : (!quantum.qubit<1>) -> !quantum.qubit<1>
  return %a1, %b1 : !quantum.qubit<1>, !quantum.qubit<1>
}
