// RUN: quantum-opt -split-input-file -circuit-cut-analysis='max-width=2 allow-gate-cuts=true allow-wire-cuts=false schedule-policy=force-directed emit-remarks=true' -verify-diagnostics %s

func.func @param_two_qubit_gate_cut(%angle: f64) {
  %q = "quantum.alloc"() : () -> !quantum.qubit<3>
  %a0, %b0, %c0 = "quantum.split"(%q) : (!quantum.qubit<3>) -> (!quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>)
  %a1, %b1 = "quantum.CNOT"(%a0, %b0) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  // expected-remark@+1 {{gate cut}}
  %b2, %c1 = "quantum.CRz"(%b1, %c0, %angle) : (!quantum.qubit<1>, !quantum.qubit<1>, f64) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  %a2, %b3 = "quantum.CNOT"(%a1, %b2) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  "quantum.deallocate"(%a2) : (!quantum.qubit<1>) -> ()
  "quantum.deallocate"(%b3) : (!quantum.qubit<1>) -> ()
  "quantum.deallocate"(%c1) : (!quantum.qubit<1>) -> ()
  return
}
