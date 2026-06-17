// RUN: quantum-opt -circuit-cut-analysis='target-width=1 allow-gate-cuts=true allow-wire-cuts=false reuse-policy=line emit-remarks=true' -verify-diagnostics %s

// A lone two-qubit gate whose qubits are returned can be gate-cut to fit width 1.
// Contrast with blocked-measured.mlir, which differs only in lifecycle.
func.func @blocked_returned() -> (!quantum.qubit<1>, !quantum.qubit<1>) {
  %a = "quantum.alloc"() : () -> !quantum.qubit<1>
  %b = "quantum.alloc"() : () -> !quantum.qubit<1>
  // expected-remark@+1 {{gate cut}}
  %a1, %b1 = "quantum.CNOT"(%a, %b) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  return %a1, %b1 : !quantum.qubit<1>, !quantum.qubit<1>
}
