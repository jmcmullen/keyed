cmake_minimum_required(VERSION 3.22.1)
project(engine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find JNI (required for native method bindings)
find_package(JNI REQUIRED)

# Path to shared C++ sources
set(SHARED_CPP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../cpp)

# ONNX Runtime setup (passed from Gradle)
if(DEFINED ONNX_DIR)
    message(STATUS "ONNX Runtime directory: ${ONNX_DIR}")
    set(ONNX_INCLUDE_DIR "${ONNX_DIR}/headers")
    set(ONNX_LIB_DIR "${ONNX_DIR}/jni/${ANDROID_ABI}")

    # Create imported library for ONNX Runtime
    add_library(onnxruntime SHARED IMPORTED)
    set_target_properties(onnxruntime PROPERTIES
        IMPORTED_LOCATION "${ONNX_LIB_DIR}/libonnxruntime.so"
    )
    set(ONNX_FOUND TRUE)
else()
    message(WARNING "ONNX_DIR not set - ONNX inference disabled")
    set(ONNX_FOUND FALSE)
endif()

# Main engine library
add_library(engine SHARED
    # Shared C++ core (from packages/engine/cpp/)
    ${SHARED_CPP_DIR}/Engine.cpp
    ${SHARED_CPP_DIR}/FFT.cpp
    ${SHARED_CPP_DIR}/MelExtractor.cpp
    ${SHARED_CPP_DIR}/ParticleFilter.cpp
    ${SHARED_CPP_DIR}/OnnxModel.cpp
    # Android-specific JNI bindings
    EngineJNI.cpp
)

target_include_directories(engine PRIVATE
    ${SHARED_CPP_DIR}
    ${JNI_INCLUDE_DIRS}
)

# Add ONNX include directories if available
if(ONNX_FOUND)
    target_include_directories(engine PRIVATE ${ONNX_INCLUDE_DIR})
    target_compile_definitions(engine PRIVATE ONNX_ENABLED=1)
endif()

target_link_libraries(engine
    android
    log
)

# Link ONNX Runtime if available
if(ONNX_FOUND)
    target_link_libraries(engine onnxruntime)
endif()

# Optimization flags
target_compile_options(engine PRIVATE
    -O3
    -ffast-math
)
