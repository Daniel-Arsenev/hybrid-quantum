// RUN: quantum-opt -split-input-file -circuit-cut-analysis='target-width=2 allow-gate-cuts=true allow-wire-cuts=false reuse-policy=line emit-remarks=true' -verify-diagnostics %s

func.func @param_two_qubit_gate_cut(%angle: f64) -> (!quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>) {
  %q = "quantum.alloc"() : () -> !quantum.qubit<3>
  %a0, %b0, %c0 = "quantum.split"(%q) : (!quantum.qubit<3>) -> (!quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>)
  %a1, %b1 = "quantum.CNOT"(%a0, %b0) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  // expected-remark@+1 {{gate cut}}
  %b2, %c1 = "quantum.CRz"(%b1, %c0, %angle) : (!quantum.qubit<1>, !quantum.qubit<1>, f64) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  %a2, %b3 = "quantum.CNOT"(%a1, %b2) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  return %a2, %b3, %c1 : !quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>
}

// -----

func.func @param_two_qubit_chain(%angle: f64)
    -> (!quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>) {
  %q0 = "quantum.alloc"() : () -> !quantum.qubit<1>
  %q1 = "quantum.alloc"() : () -> !quantum.qubit<1>
  %q2 = "quantum.alloc"() : () -> !quantum.qubit<1>
  %a0, %a1 = "quantum.CRz"(%q0, %q1, %angle) : (!quantum.qubit<1>, !quantum.qubit<1>, f64) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  // expected-remark@+1 {{gate cut}}
  %b1, %b2 = "quantum.CRz"(%a1, %q2, %angle) : (!quantum.qubit<1>, !quantum.qubit<1>, f64) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  return %a0, %b1, %b2 : !quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>
}
