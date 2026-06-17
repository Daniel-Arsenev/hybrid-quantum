// RUN: quantum-opt -circuit-cut-analysis='target-width=4 allow-gate-cuts=false allow-wire-cuts=true reuse-policy=line emit-remarks=true' -verify-diagnostics %s

// The whole circuit uses four qubits, so at target-width 4 it fits in a single
// subcircuit and no cut is needed. The absence of any remark is the assertion.
func.func @edge_fits() -> (!quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>) {
  %a = "quantum.alloc"() : () -> !quantum.qubit<1>
  %b = "quantum.alloc"() : () -> !quantum.qubit<1>
  %c = "quantum.alloc"() : () -> !quantum.qubit<1>
  %d = "quantum.alloc"() : () -> !quantum.qubit<1>
  %a1, %b1 = "quantum.CNOT"(%a, %b) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  %c1, %d1 = "quantum.CNOT"(%c, %d) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  %b2, %c2 = "quantum.CNOT"(%b1, %c1) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  return %a1, %b2, %c2, %d1 : !quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>
}
