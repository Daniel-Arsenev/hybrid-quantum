// RUN: quantum-opt -circuit-cut-analysis='target-width=4 allow-gate-cuts=false allow-wire-cuts=true reuse-policy=line fidelity-weight=5 emit-remarks=true' -verify-diagnostics %s


func.func @fidelity_balance() -> (!quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>) {
  %q0 = "quantum.alloc"() : () -> !quantum.qubit<1>
  %q1 = "quantum.alloc"() : () -> !quantum.qubit<1>
  %q2 = "quantum.alloc"() : () -> !quantum.qubit<1>
  %q3 = "quantum.alloc"() : () -> !quantum.qubit<1>
  %q4 = "quantum.alloc"() : () -> !quantum.qubit<1>
  %a0, %a1 = "quantum.CNOT"(%q0, %q1) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  // expected-remark@+1 {{wire cut: output 1}}
  %b1, %b2 = "quantum.CNOT"(%a1, %q2) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  %c2, %c3 = "quantum.CNOT"(%b2, %q3) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  %d3, %d4 = "quantum.CNOT"(%c3, %q4) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  return %a0, %b1, %c2, %d3, %d4 : !quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>
}
