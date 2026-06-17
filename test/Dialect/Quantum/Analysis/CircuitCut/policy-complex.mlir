// RUN: quantum-opt -circuit-cut-analysis='target-width=2 allow-gate-cuts=false allow-wire-cuts=true reuse-policy=complex emit-remarks=true' -verify-diagnostics %s

// Complex reuse reclaims freed qubits within a subcircuit, so this circuit fits
// width 2 with the fewest wire cuts (two). Contrast with policy-line.mlir
// (three) and policy-none.mlir (four) on the same circuit.
func.func @policy_complex() -> (!quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>) {
  %q = "quantum.alloc"() : () -> !quantum.qubit<4>
  %a0, %b0, %c0, %d0 = "quantum.split"(%q) : (!quantum.qubit<4>) -> (!quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>)
  %a1, %b1 = "quantum.CNOT"(%a0, %b0) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  // expected-remark@+1 {{wire cut: output 1}}
  %c1, %d1 = "quantum.CNOT"(%c0, %d0) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  // expected-remark@+1 {{wire cut: output 1}}
  %b2, %c2 = "quantum.CNOT"(%b1, %c1) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  %c3, %d2 = "quantum.CNOT"(%c2, %d1) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  %m, %a2   = "quantum.measure"(%a1) : (!quantum.qubit<1>) -> (!quantum.measurement<1>, !quantum.qubit<1>)
  "quantum.deallocate"(%a2) : (!quantum.qubit<1>) -> ()
  return %b2, %c3, %d2 : !quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>
}
