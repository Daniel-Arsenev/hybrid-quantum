// RUN: quantum-opt -circuit-cut-analysis='max-width=3 allow-gate-cuts=true allow-wire-cuts=true schedule-policy=asap emit-stats=true' %s | FileCheck %s
// RUN: quantum-opt -circuit-cut-analysis='max-width=3 allow-gate-cuts=true allow-wire-cuts=true schedule-policy=alap emit-stats=true' %s | FileCheck %s
// RUN: quantum-opt -circuit-cut-analysis='max-width=3 allow-gate-cuts=true allow-wire-cuts=true schedule-policy=force-directed emit-stats=true' %s | FileCheck %s

// a qubit line stops its contribution to the circuit width after its last gate, so the staircase fits width 3
// uncut

// CHECK: circuit-cut-stats: feasible=1 wire-cuts=0 gate-cuts=0 normalized-cut-count=0.00 overhead-magnitude=0.00 subcircuits=1
func.func @returned_tail() -> (!quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>) {
  %q = "quantum.alloc"() : () -> !quantum.qubit<4>
  %a0, %b0, %c0, %d0 = "quantum.split"(%q) : (!quantum.qubit<4>) -> (!quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>)
  %a1, %b1 = "quantum.CNOT"(%a0, %b0) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  %b2, %c1 = "quantum.CNOT"(%b1, %c0) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  %c2, %d1 = "quantum.CNOT"(%c1, %d0) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  return %a1, %b2, %c2, %d1 : !quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>
}
