cmake_minimum_required(VERSION 3.16)
project(engine_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Paths
set(ENGINE_CPP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../cpp)

# ============================================================================
# ONNX Runtime Setup (download if needed)
# ============================================================================
set(ONNX_VERSION "1.21.0")
set(ONNX_DIR ${CMAKE_CURRENT_BINARY_DIR}/onnxruntime)

# Detect platform
if(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
        set(ONNX_PLATFORM "osx-arm64")
    else()
        set(ONNX_PLATFORM "osx-x86_64")
    endif()
elseif(UNIX)
    set(ONNX_PLATFORM "linux-x64")
else()
    message(WARNING "Unsupported platform for ONNX Runtime tests")
    set(ONNX_ENABLED FALSE)
endif()

if(NOT DEFINED ONNX_ENABLED OR ONNX_ENABLED)
    set(ONNX_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ONNX_VERSION}/onnxruntime-${ONNX_PLATFORM}-${ONNX_VERSION}.tgz")
    set(ONNX_ARCHIVE ${CMAKE_CURRENT_BINARY_DIR}/onnxruntime.tgz)
    set(ONNX_EXTRACT_DIR ${CMAKE_CURRENT_BINARY_DIR}/onnxruntime-${ONNX_PLATFORM}-${ONNX_VERSION})

    if(NOT EXISTS ${ONNX_EXTRACT_DIR})
        message(STATUS "Downloading ONNX Runtime ${ONNX_VERSION} for ${ONNX_PLATFORM}...")
        file(DOWNLOAD ${ONNX_URL} ${ONNX_ARCHIVE} SHOW_PROGRESS)
        message(STATUS "Extracting ONNX Runtime...")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar xzf ${ONNX_ARCHIVE}
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        )
    endif()

    set(ONNX_INCLUDE_DIR ${ONNX_EXTRACT_DIR}/include)
    set(ONNX_LIB_DIR ${ONNX_EXTRACT_DIR}/lib)

    # Find the library
    find_library(ONNX_LIBRARY
        NAMES onnxruntime
        PATHS ${ONNX_LIB_DIR}
        NO_DEFAULT_PATH
    )

    if(ONNX_LIBRARY)
        message(STATUS "Found ONNX Runtime: ${ONNX_LIBRARY}")
        set(ONNX_ENABLED TRUE)
    else()
        message(WARNING "ONNX Runtime library not found")
        set(ONNX_ENABLED FALSE)
    endif()
endif()

# ============================================================================
# Engine library for testing
# ============================================================================
add_library(engine_testable STATIC
    ${ENGINE_CPP_DIR}/Engine.cpp
    ${ENGINE_CPP_DIR}/MelExtractor.cpp
    ${ENGINE_CPP_DIR}/OnnxModel.cpp
    FFT.cpp  # Uses pocketfft, same algorithm as numpy
)

target_include_directories(engine_testable PUBLIC
    ${ENGINE_CPP_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# pocketfft is header-only
target_compile_definitions(engine_testable PRIVATE
    POCKETFFT_NO_MULTITHREADING
)

# Add ONNX support if available
if(ONNX_ENABLED)
    target_include_directories(engine_testable PUBLIC ${ONNX_INCLUDE_DIR})
    target_compile_definitions(engine_testable PUBLIC ONNX_ENABLED=1)
    target_link_libraries(engine_testable ${ONNX_LIBRARY})

    # Set rpath for finding the library at runtime
    if(APPLE)
        set_target_properties(engine_testable PROPERTIES
            BUILD_RPATH "${ONNX_LIB_DIR}"
            INSTALL_RPATH "${ONNX_LIB_DIR}"
        )
    endif()
endif()

# ============================================================================
# Catch2 (amalgamated)
# ============================================================================
add_library(catch2 STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/catch_amalgamated.cpp
)
target_include_directories(catch2 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# ============================================================================
# Test executable
# ============================================================================
set(TEST_SOURCES
    test_main.cpp
    test_fft.cpp
    test_mel_extractor.cpp
    test_integration.cpp
)

# Add ONNX tests if available
if(ONNX_ENABLED)
    list(APPEND TEST_SOURCES
        test_onnx_model.cpp
        test_e2e.cpp
    )
endif()

add_executable(engine_tests ${TEST_SOURCES})

target_include_directories(engine_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${ENGINE_CPP_DIR}
)

target_link_libraries(engine_tests
    engine_testable
    catch2
)

# Set rpath for test executable
if(ONNX_ENABLED AND APPLE)
    set_target_properties(engine_tests PROPERTIES
        BUILD_RPATH "${ONNX_LIB_DIR}"
        INSTALL_RPATH "${ONNX_LIB_DIR}"
    )
endif()

# ============================================================================
# Testing
# ============================================================================
enable_testing()
add_test(NAME engine_tests COMMAND engine_tests)

# Verbose test output
add_custom_target(check
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS engine_tests
)

# Print build info
message(STATUS "")
message(STATUS "Build Configuration:")
message(STATUS "  ONNX Runtime: ${ONNX_ENABLED}")
if(ONNX_ENABLED)
    message(STATUS "  ONNX Version: ${ONNX_VERSION}")
    message(STATUS "  ONNX Platform: ${ONNX_PLATFORM}")
endif()
message(STATUS "")
