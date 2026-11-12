// RUN: quantum-opt -circuit-cut-analysis='max-width=2 schedule-policy=asap emit-schedules=true' %s | FileCheck %s

// split/merge index tracing follows each physical qubit, so both of its CNOTs share one qubit line

// CHECK:      (2 layers, 6 lines)
// CHECK-NEXT: q0|CNOT:A|CNOT:D|
// CHECK-NEXT: q1|CNOT:A|CNOT:D|
// CHECK-NEXT: q2|CNOT:B|CNOT:E|
// CHECK-NEXT: q3|CNOT:B|CNOT:E|
// CHECK-NEXT: q4|CNOT:C|CNOT:F|
// CHECK-NEXT: q5|CNOT:C|CNOT:F|
func.func @split_merge_indices()
    -> (!quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>) {
  %q = "quantum.alloc"() : () -> !quantum.qubit<4>
  %aux0 = "quantum.alloc"() : () -> !quantum.qubit<1>
  %aux1 = "quantum.alloc"() : () -> !quantum.qubit<1>
  %head, %tail = "quantum.split"(%q) : (!quantum.qubit<4>) -> (!quantum.qubit<1>, !quantum.qubit<3>)
  %t0, %t1, %t2 = "quantum.split"(%tail) : (!quantum.qubit<3>) -> (!quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>)
  %h1, %t2a = "quantum.CNOT"(%head, %t2) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  %t0a, %aux0a = "quantum.CNOT"(%t0, %aux0) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  %t1a, %aux1a = "quantum.CNOT"(%t1, %aux1) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  %pair = "quantum.merge"(%t0a, %t1a) : (!quantum.qubit<1>, !quantum.qubit<1>) -> !quantum.qubit<2>
  %m3 = "quantum.merge"(%pair, %t2a) : (!quantum.qubit<2>, !quantum.qubit<1>) -> !quantum.qubit<3>
  %r0, %r1, %r2 = "quantum.split"(%m3) : (!quantum.qubit<3>) -> (!quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>)
  %h2, %r2a = "quantum.CNOT"(%h1, %r2) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  %r0a, %aux0b = "quantum.CNOT"(%r0, %aux0a) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  %r1a, %aux1b = "quantum.CNOT"(%r1, %aux1a) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  return %h2, %r2a, %r0a, %aux0b, %r1a, %aux1b :
      !quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>
}
