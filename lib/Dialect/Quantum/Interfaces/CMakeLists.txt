add_mlir_library(QuantumInterfaces
    GateOpInterface.cpp
    InferRegisterRangesInterface.cpp

    ENABLE_AGGREGATION
    DEPENDS
        QuantumIncGen

    LINK_LIBS PUBLIC
        QuantumAnalysis
)
