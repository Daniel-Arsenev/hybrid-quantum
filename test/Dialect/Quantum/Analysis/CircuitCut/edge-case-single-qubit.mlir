// RUN: quantum-opt -circuit-cut-analysis='max-width=1 allow-gate-cuts=false allow-wire-cuts=true schedule-policy=force-directed emit-remarks=true' -verify-diagnostics %s

// no two-qubit gates means nothing to cut, so width 1 is feasible without cuts

func.func @edge_single_qubit() -> (!quantum.qubit<1>, !quantum.qubit<1>) {
  %a = "quantum.alloc"() : () -> !quantum.qubit<1>
  %b = "quantum.alloc"() : () -> !quantum.qubit<1>
  %a1 = "quantum.H"(%a) : (!quantum.qubit<1>) -> !quantum.qubit<1>
  %b1 = "quantum.X"(%b) : (!quantum.qubit<1>) -> !quantum.qubit<1>
  return %a1, %b1 : !quantum.qubit<1>, !quantum.qubit<1>
}
