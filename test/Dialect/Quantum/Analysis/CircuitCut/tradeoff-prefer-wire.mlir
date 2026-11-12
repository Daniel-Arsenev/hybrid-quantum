// RUN: quantum-opt -circuit-cut-analysis='max-width=3 allow-gate-cuts=true allow-wire-cuts=true schedule-policy=force-directed emit-remarks=true' -verify-diagnostics %s

// both cut kinds allowed, a wire cut is cheaper here, so the solver picks it
func.func @tradeoff_prefer_wire()
    -> (!quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>) {
  %a = "quantum.alloc"() : () -> !quantum.qubit<1>
  %b = "quantum.alloc"() : () -> !quantum.qubit<1>
  %c = "quantum.alloc"() : () -> !quantum.qubit<1>
  %d = "quantum.alloc"() : () -> !quantum.qubit<1>
  %a1, %b1 = "quantum.CNOT"(%a, %b) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  // expected-remark@+1 {{wire cut: output 0}}
  %c1, %d1 = "quantum.CNOT"(%c, %d) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  %b2, %c2 = "quantum.CNOT"(%b1, %c1) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  return %a1, %b2, %c2, %d1 : !quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>
}
