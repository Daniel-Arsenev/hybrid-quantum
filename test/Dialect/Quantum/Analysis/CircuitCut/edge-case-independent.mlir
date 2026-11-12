// RUN: quantum-opt -circuit-cut-analysis='max-width=2 allow-gate-cuts=false allow-wire-cuts=true schedule-policy=force-directed emit-remarks=true' -verify-diagnostics %s

// the two CNOTs share no qubit, so each fits its own subcircuit without a cut

func.func @edge_independent() -> (!quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>) {
  %a = "quantum.alloc"() : () -> !quantum.qubit<1>
  %b = "quantum.alloc"() : () -> !quantum.qubit<1>
  %c = "quantum.alloc"() : () -> !quantum.qubit<1>
  %d = "quantum.alloc"() : () -> !quantum.qubit<1>
  %a1, %b1 = "quantum.CNOT"(%a, %b) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  %c1, %d1 = "quantum.CNOT"(%c, %d) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  return %a1, %b1, %c1, %d1 : !quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>
}
