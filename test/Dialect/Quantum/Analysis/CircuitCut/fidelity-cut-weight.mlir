// RUN: quantum-opt -circuit-cut-analysis='target-width=3 allow-gate-cuts=true allow-wire-cuts=true reuse-policy=line cut-weight=20 fidelity-weight=20 emit-remarks=true' -verify-diagnostics %s

func.func @fidelity_cut_weight() -> (!quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>) {
  %a = "quantum.alloc"() : () -> !quantum.qubit<1>
  %b = "quantum.alloc"() : () -> !quantum.qubit<1>
  %c = "quantum.alloc"() : () -> !quantum.qubit<1>
  %d = "quantum.alloc"() : () -> !quantum.qubit<1>
  %a1, %b1 = "quantum.CNOT"(%a, %b) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  // expected-remark@+1 {{wire cut: output 1}}
  %b2, %c1 = "quantum.CNOT"(%b1, %c) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  %c2, %d1 = "quantum.CNOT"(%c1, %d) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  %c3, %d2 = "quantum.CNOT"(%c2, %d1) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  %c4, %d3 = "quantum.CNOT"(%c3, %d2) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  return %a1, %b2, %c4, %d3 : !quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>
}
