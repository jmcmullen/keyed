cmake_minimum_required(VERSION 3.16)
project(beatnet_cli)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ONNX Runtime - download if not present
set(ONNX_VERSION "1.16.3")
set(ONNX_DIR "${CMAKE_BINARY_DIR}/onnxruntime")

if(NOT EXISTS "${ONNX_DIR}/include/onnxruntime_cxx_api.h")
    message(STATUS "Downloading ONNX Runtime ${ONNX_VERSION}...")

    if(APPLE)
        if(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
            set(ONNX_PLATFORM "osx-arm64")
        else()
            set(ONNX_PLATFORM "osx-x86_64")
        endif()
    else()
        message(FATAL_ERROR "Only macOS is supported for now")
    endif()

    set(ONNX_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ONNX_VERSION}/onnxruntime-${ONNX_PLATFORM}-${ONNX_VERSION}.tgz")
    set(ONNX_ARCHIVE "${CMAKE_BINARY_DIR}/onnxruntime.tgz")

    file(DOWNLOAD ${ONNX_URL} ${ONNX_ARCHIVE} SHOW_PROGRESS)
    file(ARCHIVE_EXTRACT INPUT ${ONNX_ARCHIVE} DESTINATION ${CMAKE_BINARY_DIR})
    file(RENAME "${CMAKE_BINARY_DIR}/onnxruntime-${ONNX_PLATFORM}-${ONNX_VERSION}" ${ONNX_DIR})
endif()

# miniaudio - download if not present
set(MINIAUDIO_DIR "${CMAKE_BINARY_DIR}/miniaudio")
if(NOT EXISTS "${MINIAUDIO_DIR}/miniaudio.h")
    message(STATUS "Downloading miniaudio...")
    file(DOWNLOAD
        "https://raw.githubusercontent.com/mackron/miniaudio/master/miniaudio.h"
        "${MINIAUDIO_DIR}/miniaudio.h"
        SHOW_PROGRESS
    )
endif()

# Engine source files
set(ENGINE_DIR "${CMAKE_SOURCE_DIR}/../cpp")
set(ENGINE_SOURCES
    ${ENGINE_DIR}/Engine.cpp
    ${ENGINE_DIR}/MelExtractor.cpp
    ${ENGINE_DIR}/OnnxModel.cpp
    ${ENGINE_DIR}/FFT.cpp
)

# Function to copy ONNX runtime dylib and create symlink for a target
function(copy_onnx_runtime_dylib target_name)
    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            "${ONNX_DIR}/lib/libonnxruntime.${ONNX_VERSION}.dylib"
            "$<TARGET_FILE_DIR:${target_name}>/libonnxruntime.${ONNX_VERSION}.dylib"
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            "libonnxruntime.${ONNX_VERSION}.dylib"
            "$<TARGET_FILE_DIR:${target_name}>/libonnxruntime.dylib"
    )
    set_target_properties(${target_name} PROPERTIES
        INSTALL_RPATH "@executable_path"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endfunction()

# CLI executable
add_executable(beatnet_cli
    main.cpp
    ${ENGINE_SOURCES}
)

target_compile_definitions(beatnet_cli PRIVATE ONNX_ENABLED)

target_include_directories(beatnet_cli PRIVATE
    ${ENGINE_DIR}
    ${ONNX_DIR}/include
    ${MINIAUDIO_DIR}
)

target_link_directories(beatnet_cli PRIVATE
    ${ONNX_DIR}/lib
)

target_link_libraries(beatnet_cli PRIVATE
    onnxruntime
    "-framework CoreAudio"
    "-framework AudioToolbox"
    "-framework CoreFoundation"
)

copy_onnx_runtime_dylib(beatnet_cli)

# Batch test executable
add_executable(batch_test
    batch_test.cpp
    ${ENGINE_SOURCES}
)

target_compile_definitions(batch_test PRIVATE ONNX_ENABLED)

target_include_directories(batch_test PRIVATE
    ${ENGINE_DIR}
    ${ONNX_DIR}/include
    ${MINIAUDIO_DIR}
)

target_link_directories(batch_test PRIVATE
    ${ONNX_DIR}/lib
)

target_link_libraries(batch_test PRIVATE
    onnxruntime
    "-framework CoreAudio"
    "-framework AudioToolbox"
    "-framework CoreFoundation"
)

copy_onnx_runtime_dylib(batch_test)

# Latency test executable
add_executable(latency_test
    latency_test.cpp
    ${ENGINE_SOURCES}
)

target_compile_definitions(latency_test PRIVATE ONNX_ENABLED)

target_include_directories(latency_test PRIVATE
    ${ENGINE_DIR}
    ${ONNX_DIR}/include
    ${MINIAUDIO_DIR}
)

target_link_directories(latency_test PRIVATE
    ${ONNX_DIR}/lib
)

target_link_libraries(latency_test PRIVATE
    onnxruntime
    "-framework CoreAudio"
    "-framework AudioToolbox"
    "-framework CoreFoundation"
)

copy_onnx_runtime_dylib(latency_test)
