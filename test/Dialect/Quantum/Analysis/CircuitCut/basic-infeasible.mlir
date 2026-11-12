// RUN: quantum-opt -split-input-file -circuit-cut-analysis='max-width=2 allow-gate-cuts=false allow-wire-cuts=false schedule-policy=force-directed emit-remarks=true' -verify-diagnostics %s

// both cut kinds disabled, no width 2 partition of the three-qubit chain exists

// expected-remark@+1 {{circuit cut infeasible}}
func.func @basic_infeasible() -> (!quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>) {
  %q = "quantum.alloc"() : () -> !quantum.qubit<3>
  %a0, %b0, %c0 = "quantum.split"(%q) : (!quantum.qubit<3>) -> (!quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>)
  %a1, %b1 = "quantum.CNOT"(%a0, %b0) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  %b2, %c1 = "quantum.CNOT"(%b1, %c0) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  %a2, %b3 = "quantum.CNOT"(%a1, %b2) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  return %a2, %b3, %c1 : !quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>
}
