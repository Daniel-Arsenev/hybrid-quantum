// RUN: quantum-opt -circuit-cut-analysis='target-width=2 allow-gate-cuts=false allow-wire-cuts=true reuse-policy=line emit-remarks=true' -verify-diagnostics %s

// Qubit-line reuse is weaker than complex reuse, so fitting the same circuit in
// width 2 needs more wire cuts (three). Contrast with policy-complex.mlir (two)
// and policy-none.mlir (four).
func.func @policy_line() -> (!quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>) {
  %q = "quantum.alloc"() : () -> !quantum.qubit<4>
  %a0, %b0, %c0, %d0 = "quantum.split"(%q) : (!quantum.qubit<4>) -> (!quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>)
  // expected-remark@+1 {{wire cut: output 1}}
  %a1, %b1 = "quantum.CNOT"(%a0, %b0) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  // expected-remark@+1 {{wire cut: output 0}}
  %c1, %d1 = "quantum.CNOT"(%c0, %d0) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  // expected-remark@+1 {{wire cut: output 1}}
  %b2, %c2 = "quantum.CNOT"(%b1, %c1) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  %c3, %d2 = "quantum.CNOT"(%c2, %d1) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  "quantum.deallocate"(%a1) : (!quantum.qubit<1>) -> ()
  return %b2, %c3, %d2 : !quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>
}
