// RUN: quantum-opt -circuit-cut-analysis='max-width=2 allow-gate-cuts=true allow-wire-cuts=true schedule-policy=force-directed emit-remarks=true' -verify-diagnostics %s

// both cut kinds allowed, a gate cut is cheaper here, so the solver picks it
func.func @tradeoff_prefer_gate() {
  %q = "quantum.alloc"() : () -> !quantum.qubit<3>
  %a0, %b0, %c0 = "quantum.split"(%q) : (!quantum.qubit<3>) -> (!quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>)
  %a1, %b1 = "quantum.CNOT"(%a0, %b0) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  // expected-remark@+1 {{gate cut}}
  %b2, %c1 = "quantum.CNOT"(%b1, %c0) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  %a2, %b3 = "quantum.CNOT"(%a1, %b2) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  "quantum.deallocate"(%a2) : (!quantum.qubit<1>) -> ()
  "quantum.deallocate"(%b3) : (!quantum.qubit<1>) -> ()
  "quantum.deallocate"(%c1) : (!quantum.qubit<1>) -> ()
  return
}
