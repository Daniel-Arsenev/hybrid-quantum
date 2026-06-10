add_mlir_library(QuantumAnalysis
    CircuitCutAnalysis.cpp
    RegisterRangesAnalysis.cpp

    ENABLE_AGGREGATION
    DEPENDS
        QuantumIncGen
        QuantumPassesIncGen
    LINK_LIBS PUBLIC
        MLIRFuncDialect
        MLIRPass
        ortools::ortools
)

# OR-Tools protobuf headers require OR_PROTO_DLL to be defined (empty on non-Windows).
# The ortools::ortools cmake target carries this as an INTERFACE_COMPILE_DEFINITION but
# add_mlir_library does not propagate it automatically, so we set it explicitly here.
target_compile_definitions(obj.QuantumAnalysis PRIVATE OR_PROTO_DLL=)
