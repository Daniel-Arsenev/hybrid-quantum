// RUN: quantum-opt -split-input-file -circuit-cut-analysis='max-width=1 allow-gate-cuts=true allow-wire-cuts=false schedule-policy=force-directed emit-remarks=true' -verify-diagnostics %s

// wire cutting disabled, the CX gate is gate-cut to fit width 1

func.func @cx_gate_cut() {
  %a = "quantum.alloc"() : () -> !quantum.qubit<1>
  %b = "quantum.alloc"() : () -> !quantum.qubit<1>
  // expected-remark@+1 {{gate cut}}
  %a1, %b1 = "quantum.CNOT"(%a, %b) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  "quantum.deallocate"(%a1) : (!quantum.qubit<1>) -> ()
  "quantum.deallocate"(%b1) : (!quantum.qubit<1>) -> ()
  return
}
