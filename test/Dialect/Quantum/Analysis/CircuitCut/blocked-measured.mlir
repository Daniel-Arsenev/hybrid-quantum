// RUN: quantum-opt -circuit-cut-analysis='target-width=1 allow-gate-cuts=true allow-wire-cuts=false reuse-policy=line emit-remarks=true' -verify-diagnostics %s

// A gate feeding a measurement cannot be gate-cut. With wire cuts disabled the
// only gate is uncuttable, so no width-1 partition exists. Same options as
// blocked-returned.mlir; only the lifecycle (measured vs returned) differs.
// expected-remark@+1 {{circuit cut infeasible}}
func.func @blocked_measured() {
  %a = "quantum.alloc"() : () -> !quantum.qubit<1>
  %b = "quantum.alloc"() : () -> !quantum.qubit<1>
  %a1, %b1 = "quantum.CNOT"(%a, %b) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  %ma, %a2 = "quantum.measure"(%a1) : (!quantum.qubit<1>) -> (!quantum.measurement<1>, !quantum.qubit<1>)
  %mb, %b2 = "quantum.measure"(%b1) : (!quantum.qubit<1>) -> (!quantum.measurement<1>, !quantum.qubit<1>)
  "quantum.deallocate"(%a2) : (!quantum.qubit<1>) -> ()
  "quantum.deallocate"(%b2) : (!quantum.qubit<1>) -> ()
  return
}
