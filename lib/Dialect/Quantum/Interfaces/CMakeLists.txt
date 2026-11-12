add_mlir_library(QuantumInterfaces
    GateOpInterface.cpp
    InferRegisterRangesInterface.cpp
    InferPhasePolynomialInterface.cpp

    ENABLE_AGGREGATION
    DEPENDS
        QuantumIncGen

    LINK_LIBS PUBLIC
        QuantumAnalysis
)
