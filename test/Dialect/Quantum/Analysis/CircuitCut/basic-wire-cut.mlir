// RUN: quantum-opt -circuit-cut-analysis='max-width=2 allow-gate-cuts=false allow-wire-cuts=true schedule-policy=force-directed emit-remarks=true' -verify-diagnostics %s

// gate cutting disabled, one wire cut splits the chain to fit width 2
func.func @basic_wire_cut() -> (!quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>) {
  %q = "quantum.alloc"() : () -> !quantum.qubit<3>
  %a0, %b0, %c0 = "quantum.split"(%q) : (!quantum.qubit<3>) -> (!quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>)
  // expected-remark@+1 {{wire cut: output 0}}
  %a1, %b1 = "quantum.CNOT"(%a0, %b0) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  %b2, %c1 = "quantum.CNOT"(%b1, %c0) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  %a2, %b3 = "quantum.CNOT"(%a1, %b2) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  return %a2, %b3, %c1 : !quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>
}
