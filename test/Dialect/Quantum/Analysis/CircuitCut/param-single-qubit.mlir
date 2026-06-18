// RUN: quantum-opt -circuit-cut-analysis='target-width=2 allow-gate-cuts=false allow-wire-cuts=true reuse-policy=line emit-remarks=true' -verify-diagnostics %s

func.func @param_single_qubit_tracing(%theta: f64)
    -> (!quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>) {
  %q0 = "quantum.alloc"() : () -> !quantum.qubit<1>
  %q1 = "quantum.alloc"() : () -> !quantum.qubit<1>
  %q2 = "quantum.alloc"() : () -> !quantum.qubit<1>
  // the wire out of this gate's output 1 is traced through Rz/Rx to the second
  // CNOT and cut there; the cut is reported at the source output.
  // expected-remark@+1 {{wire cut: output 1}}
  %a0, %a1 = "quantum.CNOT"(%q0, %q1) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  %r1 = "quantum.Rz"(%a1, %theta) : (!quantum.qubit<1>, f64) -> !quantum.qubit<1>
  %s1 = "quantum.Rx"(%r1, %theta) : (!quantum.qubit<1>, f64) -> !quantum.qubit<1>
  %b1, %b2 = "quantum.CNOT"(%s1, %q2) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  return %a0, %b1, %b2 : !quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>
}
