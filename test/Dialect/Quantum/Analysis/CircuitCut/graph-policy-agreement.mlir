// RUN: quantum-opt -circuit-cut-analysis='max-width=2 allow-gate-cuts=false allow-wire-cuts=true schedule-policy=asap emit-stats=true' %s | FileCheck %s
// RUN: quantum-opt -circuit-cut-analysis='max-width=2 allow-gate-cuts=false allow-wire-cuts=true schedule-policy=alap emit-stats=true' %s | FileCheck %s
// RUN: quantum-opt -circuit-cut-analysis='max-width=2 allow-gate-cuts=false allow-wire-cuts=true schedule-policy=force-directed emit-stats=true' %s | FileCheck %s

// all layered scheduling policies agree on width cost and cut set for this circuit

// CHECK: circuit-cut-stats: feasible=1 wire-cuts=2 gate-cuts=0 normalized-cut-count=2.00 overhead-magnitude=1.20 subcircuits=2
func.func @policy_layered() -> (!quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>) {
  %q = "quantum.alloc"() : () -> !quantum.qubit<4>
  %a0, %b0, %c0, %d0 = "quantum.split"(%q) : (!quantum.qubit<4>) -> (!quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>)
  %a1, %b1 = "quantum.CNOT"(%a0, %b0) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  %c1, %d1 = "quantum.CNOT"(%c0, %d0) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  %b2, %c2 = "quantum.CNOT"(%b1, %c1) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  %c3, %d2 = "quantum.CNOT"(%c2, %d1) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  "quantum.deallocate"(%a1) : (!quantum.qubit<1>) -> ()
  return %b2, %c3, %d2 : !quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>
}
