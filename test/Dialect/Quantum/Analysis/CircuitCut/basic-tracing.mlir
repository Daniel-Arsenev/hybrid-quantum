// RUN: quantum-opt -circuit-cut-analysis='target-width=2 allow-gate-cuts=false allow-wire-cuts=true reuse-policy=line emit-remarks=true' -verify-diagnostics %s

// A wire is traced through split/merge plumbing back to a wide (2-qubit) gate.
// The cut is reported at that gate's output, including the qubit index within
// the wide output ("output N#i"). Exercises the SplitOp/MergeOp tracing branches
// and the multi-qubit-index suffix in the remark.
func.func @trace_merge() -> !quantum.qubit<2> {
  %q = "quantum.alloc"() : () -> !quantum.qubit<2>
  %r = "quantum.alloc"() : () -> !quantum.qubit<2>
  %qa, %qb = "quantum.split"(%q) : (!quantum.qubit<2>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  %ra, %rb = "quantum.split"(%r) : (!quantum.qubit<2>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  %qa1, %ra1 = "quantum.CNOT"(%qa, %ra) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  %qm = "quantum.merge"(%qa1, %qb) : (!quantum.qubit<1>, !quantum.qubit<1>) -> !quantum.qubit<2>
  %rm = "quantum.merge"(%ra1, %rb) : (!quantum.qubit<1>, !quantum.qubit<1>) -> !quantum.qubit<2>
  // expected-remark@+2 {{wire cut: output 0}}
  // expected-remark@+1 {{wire cut: output 1#1}}
  %q1, %r1 = "quantum.CNOT"(%qm, %rm) : (!quantum.qubit<2>, !quantum.qubit<2>) -> (!quantum.qubit<2>, !quantum.qubit<2>)
  %m = "quantum.merge"(%q1, %r1) : (!quantum.qubit<2>, !quantum.qubit<2>) -> !quantum.qubit<4>
  %s0, %s1, %s2, %s3 = "quantum.split"(%m) : (!quantum.qubit<4>) -> (!quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>)
  %x0, %x1 = "quantum.CNOT"(%s0, %s3) : (!quantum.qubit<1>, !quantum.qubit<1>) -> (!quantum.qubit<1>, !quantum.qubit<1>)
  %back = "quantum.merge"(%x0, %s1, %s2, %x1) : (!quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>, !quantum.qubit<1>) -> !quantum.qubit<4>
  %d0, %d1 = "quantum.split"(%back) : (!quantum.qubit<4>) -> (!quantum.qubit<2>, !quantum.qubit<2>)
  "quantum.deallocate"(%d1) : (!quantum.qubit<2>) -> ()
  return %d0 : !quantum.qubit<2>
}
