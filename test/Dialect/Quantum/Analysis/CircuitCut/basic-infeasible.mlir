// RUN: quantum-opt -split-input-file -circuit-cut-analysis='target-width=2 allow-gate-cuts=false allow-wire-cuts=false reuse-policy=line emit-remarks=true' -verify-diagnostics %s

// Width 2 with neither wire nor gate cuts allowed cannot fit three qubit wide circuit
// expected-remark@+1 {{circuit cut infeasible}}
func.func @basic_infeasible() -> (!quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>) {
  %q = "quantum.alloc"() : () -> !quantum.qubit<3>
  %a0, %b0, %c0 = "quantum.split"(%q) : (!quantum.qubit<3>) -> (!quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>)
  %a1, %b1 = "quantum.CNOT"(%a0, %b0) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  %b2, %c1 = "quantum.CNOT"(%b1, %c0) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  %a2, %b3 = "quantum.CNOT"(%a1, %b2) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  return %a2, %b3, %c1 : !quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>
}

// -----

// Width 2 with neither wire nor gate cuts allowed cannot fit three qubit wide gate
// expected-remark@+1 {{circuit cut infeasible}}
func.func @ccx_infeasible() -> (!quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>) {
  %a = "quantum.alloc"() : () -> !quantum.qubit<1>
  %b = "quantum.alloc"() : () -> !quantum.qubit<1>
  %c = "quantum.alloc"() : () -> !quantum.qubit<1>
  %a1, %b1, %c1 = "quantum.CCX"(%a, %b, %c) : (!quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>)
  return %a1, %b1, %c1 : !quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>
}
