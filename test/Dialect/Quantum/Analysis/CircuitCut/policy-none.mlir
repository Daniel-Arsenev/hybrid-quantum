// RUN: quantum-opt -circuit-cut-analysis='target-width=2 allow-gate-cuts=false allow-wire-cuts=true reuse-policy=none emit-remarks=true' -verify-diagnostics %s

// Without qubit reuse, fitting the same circuit in width 2 needs the most wire
// cuts (four; two leave the same gate, named by output index). Contrast with
// policy-complex.mlir (two) and policy-line.mlir (three).
func.func @policy_none() -> (!quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>) {
  %q = "quantum.alloc"() : () -> !quantum.qubit<4>
  %a0, %b0, %c0, %d0 = "quantum.split"(%q) : (!quantum.qubit<4>) -> (!quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>)
  // expected-remark@+1 {{wire cut: output 1}}
  %a1, %b1 = "quantum.CNOT"(%a0, %b0) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  // expected-remark@+2 {{wire cut: output 0}}
  // expected-remark@+1 {{wire cut: output 1}}
  %c1, %d1 = "quantum.CNOT"(%c0, %d0) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  // expected-remark@+1 {{wire cut: output 1}}
  %b2, %c2 = "quantum.CNOT"(%b1, %c1) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  %c3, %d2 = "quantum.CNOT"(%c2, %d1) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  "quantum.deallocate"(%a1) : (!quantum.qubit<1>) -> ()
  return %b2, %c3, %d2 : !quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>
}
