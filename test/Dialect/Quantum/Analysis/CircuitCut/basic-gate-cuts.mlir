// RUN: quantum-opt -split-input-file -circuit-cut-analysis='target-width=2 allow-gate-cuts=true allow-wire-cuts=false reuse-policy=line emit-remarks=true' -verify-diagnostics %s

// With only gate cuts allowed, the width-2 limit is met by cutting the middle
// two-qubit gate rather than a wire.
func.func @basic_gate_cut() -> (!quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>) {
  %q = "quantum.alloc"() : () -> !quantum.qubit<3>
  %a0, %b0, %c0 = "quantum.split"(%q) : (!quantum.qubit<3>) -> (!quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>)
  %a1, %b1 = "quantum.CNOT"(%a0, %b0) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  // expected-remark@+1 {{gate cut}}
  %b2, %c1 = "quantum.CNOT"(%b1, %c0) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  %a2, %b3 = "quantum.CNOT"(%a1, %b2) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  return %a2, %b3, %c1 : !quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>
}

// -----

// With only gate cuts allowed, the width-2 limit can only be met by cutting the
// three-qubit gate rather than a wire.
func.func @ccx_gate_cut() -> (!quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>) {
  %a = "quantum.alloc"() : () -> !quantum.qubit<1>
  %b = "quantum.alloc"() : () -> !quantum.qubit<1>
  %c = "quantum.alloc"() : () -> !quantum.qubit<1>
  // expected-remark@+1 {{gate cut}}
  %a1, %b1, %c1 = "quantum.CCX"(%a, %b, %c) : (!quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>)
  return %a1, %b1, %c1 : !quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>
}
