//===- GateOpInterface.td - Quantum gate marker interface ---*- tablegen -*-===//
//
//===----------------------------------------------------------------------===//
//
// Defines GateOpInterface: a zero-method marker that every PrimitiveGate_Op
// in the Quantum dialect implements, allowing callers to use
// isa<GateOpInterface>() to identify primitive unitary gate ops.
//
//===----------------------------------------------------------------------===//

#ifndef QUANTUM_INTERFACES_GATEOPINTERFACE
#define QUANTUM_INTERFACES_GATEOPINTERFACE

include "mlir/IR/OpBase.td"

def GateOpInterface : OpInterface<"GateOpInterface"> {
  let description = [{
    Marker interface implemented by primitive quantum gate operations
    (PrimitiveGate_Op subclasses: quantum.X, quantum.H, quantum.CNOT, etc.).
    Non-primitive gate ops (quantum.call) and memory ops (quantum.alloc,
    quantum.deallocate, quantum.measure, quantum.reset, quantum.split,
    quantum.merge) do not implement this interface.
  }];
  let cppNamespace = "::mlir::quantum";
  let methods = [];
}

#endif // QUANTUM_INTERFACES_GATEOPINTERFACE
