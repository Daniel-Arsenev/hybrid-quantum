// RUN: quantum-opt -circuit-cut-analysis='max-width=2 schedule-policy=asap emit-schedules=true' %s | FileCheck %s

// single-qubit gates elide into wire plumbing, so only the three CNOTs appear in the schedule

// CHECK:      (3 layers, 3 lines)
// CHECK-NEXT: q0|CNOT:A|CNOT:B|      |
// CHECK-NEXT: q1|CNOT:A|      |CNOT:C|
// CHECK-NEXT: q2|      |CNOT:B|CNOT:C|
func.func @elision() -> (!quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>) {
  %theta = arith.constant 0.5 : f64
  %a0 = "quantum.alloc"() : () -> !quantum.qubit<1>
  %b0 = "quantum.alloc"() : () -> !quantum.qubit<1>
  %c0 = "quantum.alloc"() : () -> !quantum.qubit<1>
  %a1 = "quantum.H"(%a0) : (!quantum.qubit<1>) -> !quantum.qubit<1>
  %a2, %b1 = "quantum.CNOT"(%a1, %b0) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  %b2 = "quantum.Rz"(%b1, %theta) : (!quantum.qubit<1>, f64) -> !quantum.qubit<1>
  %c1 = "quantum.X"(%c0) : (!quantum.qubit<1>) -> !quantum.qubit<1>
  %a3, %c2 = "quantum.CNOT"(%a2, %c1) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  %b3, %c3 = "quantum.CNOT"(%b2, %c2) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  return %a3, %b3, %c3 : !quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>
}
