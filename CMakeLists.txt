cmake_minimum_required(VERSION 3.30)

project(ithax VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
find_package(Threads REQUIRED)

find_package(carbon-core CONFIG REQUIRED)
find_package(carbon-math CONFIG REQUIRED)
set(Python3_EXECUTABLE
    "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/python3/python.exe")
find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
find_package(carbon-scheduler CONFIG REQUIRED)
find_package(carbon-io CONFIG REQUIRED)
find_package(carbon-blue CONFIG REQUIRED)
find_package(CarbonMesh CONFIG REQUIRED)
find_package(carbon-imageio CONFIG REQUIRED)
find_package(bsdiff CONFIG REQUIRED)
find_package(cryptopp CONFIG REQUIRED)
find_package(CURL CONFIG REQUIRED)
find_package(unofficial-inih CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(carbon-resources CONFIG REQUIRED)
find_package(carbon-trinity CONFIG REQUIRED)
find_package(carbon-destiny CONFIG REQUIRED)
find_package(carbon-audio CONFIG REQUIRED)
find_package(carbon-pathfinder CONFIG REQUIRED)
find_package(carbon-localization CONFIG REQUIRED)
find_package(carbon-db CONFIG REQUIRED)
find_package(carbon-parser CONFIG REQUIRED)
find_package(carbon-geo2 CONFIG REQUIRED)
find_package(carbon-d3dinfo CONFIG REQUIRED)
find_package(crashpad CONFIG REQUIRED)
find_package(Taskflow CONFIG REQUIRED)
find_package(EnTT CONFIG REQUIRED)
find_package(TBB CONFIG REQUIRED)
find_package(Tracy CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

option(
    ITHAX_ENABLE_CARBON_GRPC
    "Build the experimental Carbon gRPC smoke target"
    OFF)

if(ITHAX_ENABLE_CARBON_GRPC)
    find_package(carbon-grpc CONFIG REQUIRED)
endif()

option(
    ITHAX_ENABLE_TRINITY_SHADER_COMPILER
    "Require the separately installed Carbon Trinity shader compiler"
    OFF)
option(
    ITHAX_ENABLE_WWISE
    "Require the separately licensed Wwise SDK"
    OFF)
option(
    ITHAX_ENABLE_TRINITY_DX11
    "Require the separately installed Carbon TrinityAL DX11 provider"
    OFF)
option(
    ITHAX_ENABLE_TRINITY_VULKAN
    "Require the Ithax TrinityAL Vulkan backend"
    OFF)

if(ITHAX_ENABLE_TRINITY_SHADER_COMPILER)
    find_package(carbon-shadercompiler CONFIG REQUIRED)
endif()

if(ITHAX_ENABLE_TRINITY_DX11)
    find_package(carbon-trinity-dx11 CONFIG REQUIRED)
endif()

if(ITHAX_ENABLE_TRINITY_VULKAN)
    find_package(carbon-trinity-vulkan CONFIG REQUIRED)
endif()

if(ITHAX_ENABLE_WWISE)
    find_package(wwise CONFIG REQUIRED)
endif()

add_executable(ithax-foundation-smoke
    src/foundation_smoke.cpp
)

target_compile_features(ithax-foundation-smoke PRIVATE cxx_std_17)
target_link_libraries(ithax-foundation-smoke PRIVATE CcpCore CcpMath)

add_executable(ithax-blue-native-smoke
    src/blue_native_smoke.cpp
)

target_compile_features(ithax-blue-native-smoke PRIVATE cxx_std_17)
target_link_libraries(ithax-blue-native-smoke PRIVATE Blue)

add_executable(ithax-carbon-blue-owner-measurement
    src/carbon_blue_owner_measurement.cpp
    src/thread_budget.cpp
)

target_compile_features(
    ithax-carbon-blue-owner-measurement PRIVATE cxx_std_20)
target_include_directories(
    ithax-carbon-blue-owner-measurement PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(
    ithax-carbon-blue-owner-measurement PRIVATE
    Blue Python3::Python Threads::Threads)

add_executable(ithax-crashpad-owner-measurement
    src/crashpad_owner_measurement.cpp
    src/thread_budget.cpp
)

target_compile_features(
    ithax-crashpad-owner-measurement PRIVATE cxx_std_20)
target_include_directories(
    ithax-crashpad-owner-measurement PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(
    ithax-crashpad-owner-measurement PRIVATE
    crashpad::crashpad Threads::Threads)

add_executable(ithax-mesh-native-smoke
    src/mesh_native_smoke.cpp
)

target_compile_features(ithax-mesh-native-smoke PRIVATE cxx_std_17)
target_link_libraries(ithax-mesh-native-smoke PRIVATE CarbonMesh)

add_executable(ithax-imageio-native-smoke
    src/imageio_native_smoke.cpp
)

target_compile_features(ithax-imageio-native-smoke PRIVATE cxx_std_17)
target_link_libraries(ithax-imageio-native-smoke PRIVATE CcpImageIO)

add_executable(ithax-resources-native-smoke
    src/resources_native_smoke.cpp
)

target_compile_features(ithax-resources-native-smoke PRIVATE cxx_std_17)
target_link_libraries(
    ithax-resources-native-smoke PRIVATE resources resources-tools)

add_executable(ithax-trinity-native-smoke
    src/trinity_native_smoke.cpp
)

target_compile_features(ithax-trinity-native-smoke PRIVATE cxx_std_17)
target_link_libraries(ithax-trinity-native-smoke PRIVATE trinity_stub)

add_executable(ithax-audio-native-smoke
    src/audio_native_smoke.cpp
)

target_compile_features(ithax-audio-native-smoke PRIVATE cxx_std_17)
target_link_libraries(ithax-audio-native-smoke PRIVATE CarbonAudio)

add_executable(ithax-pathfinder-native-smoke
    src/pathfinder_native_smoke.cpp
)

target_compile_features(ithax-pathfinder-native-smoke PRIVATE cxx_std_17)
target_link_libraries(
    ithax-pathfinder-native-smoke PRIVATE EvePathfinder)

add_executable(ithax-localization-native-smoke
    src/localization_native_smoke.cpp
)

target_compile_features(ithax-localization-native-smoke PRIVATE cxx_std_17)
target_link_libraries(
    ithax-localization-native-smoke PRIVATE EveLocalization)

add_executable(ithax-db-native-smoke
    src/db_native_smoke.cpp
)

target_compile_features(ithax-db-native-smoke PRIVATE cxx_std_17)
target_link_libraries(ithax-db-native-smoke PRIVATE db)

add_executable(ithax-parser-native-smoke
    src/parser_native_smoke.cpp
)

target_compile_features(ithax-parser-native-smoke PRIVATE cxx_std_17)
target_link_libraries(ithax-parser-native-smoke PRIVATE CcpParser)

add_executable(ithax-geo2-native-smoke
    src/geo2_native_smoke.cpp
)

target_compile_features(ithax-geo2-native-smoke PRIVATE cxx_std_17)
target_link_libraries(ithax-geo2-native-smoke PRIVATE geo2)

add_executable(ithax-d3dinfo-native-smoke
    src/d3dinfo_native_smoke.cpp
)

target_compile_features(ithax-d3dinfo-native-smoke PRIVATE cxx_std_17)
target_link_libraries(ithax-d3dinfo-native-smoke PRIVATE d3dinfo)

add_executable(ithax-stage2-benchmark
    src/stage2_benchmark.cpp
)

target_compile_features(ithax-stage2-benchmark PRIVATE cxx_std_20)
target_link_libraries(
    ithax-stage2-benchmark PRIVATE Taskflow::Taskflow EnTT::EnTT)

add_executable(ithax-thread-budget-measurement
    src/thread_budget.cpp
    src/thread_budget_measurement.cpp
)

target_compile_features(ithax-thread-budget-measurement PRIVATE cxx_std_20)
target_link_libraries(
    ithax-thread-budget-measurement PRIVATE Taskflow::Taskflow)

add_executable(ithax-stage3-multicore-smoke
    src/ecs/ecs_world.cpp
    src/jobs/job_runtime.cpp
    src/runtime/runtime_supervisor.cpp
    src/stage3_multicore_smoke.cpp
    src/thread_budget.cpp
    src/threading/frame_slot.cpp
    src/threading/frame_packet_channel.cpp
    src/threading/scratch_allocator.cpp
)

target_compile_features(ithax-stage3-multicore-smoke PRIVATE cxx_std_20)
target_include_directories(
    ithax-stage3-multicore-smoke PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(
    ithax-stage3-multicore-smoke PRIVATE
    Taskflow::Taskflow EnTT::EnTT Threads::Threads)

add_executable(ithax-stage3-multicore-integration
    src/ecs/ecs_world.cpp
    src/jobs/job_runtime.cpp
    src/runtime/runtime_supervisor.cpp
    src/stage3_multicore_integration.cpp
    src/thread_budget.cpp
    src/threading/frame_slot.cpp
    src/threading/frame_packet_channel.cpp
    src/threading/scratch_allocator.cpp
)

target_compile_features(ithax-stage3-multicore-integration PRIVATE cxx_std_20)
target_include_directories(
    ithax-stage3-multicore-integration PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(
    ithax-stage3-multicore-integration PRIVATE
    Taskflow::Taskflow EnTT::EnTT Threads::Threads)

add_executable(ithax-stage3-carbon-host-integration
    src/ecs/ecs_world.cpp
    src/jobs/job_runtime.cpp
    src/runtime/runtime_supervisor.cpp
    src/stage3_carbon_host_integration.cpp
    src/thread_budget.cpp
    src/threading/frame_packet_channel.cpp
)

target_compile_features(ithax-stage3-carbon-host-integration PRIVATE cxx_std_20)
target_include_directories(
    ithax-stage3-carbon-host-integration PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(
    ithax-stage3-carbon-host-integration PRIVATE
    Blue Python3::Python Taskflow::Taskflow EnTT::EnTT
    Tracy::TracyClient Threads::Threads)

add_executable(ithax-stage3-owner-measurement
    src/stage3_owner_measurement.cpp
    src/thread_budget.cpp
)

target_compile_features(ithax-stage3-owner-measurement PRIVATE cxx_std_20)
target_include_directories(
    ithax-stage3-owner-measurement PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(
    ithax-stage3-owner-measurement PRIVATE
    TBB::tbb Tracy::TracyClient trinity_stub Threads::Threads
    d3d11 dxgi)

if(ITHAX_ENABLE_WWISE)
    target_link_libraries(ithax-stage3-owner-measurement PRIVATE Wwise::Wwise)
    target_compile_definitions(
        ithax-stage3-owner-measurement PRIVATE ITHAX_WWISE_ENABLED=1)
endif()

if(ITHAX_ENABLE_TRINITY_SHADER_COMPILER AND NOT TARGET ShaderCompiler)
    message(FATAL_ERROR
        "Carbon shader compiler feature did not export ShaderCompiler")
endif()

if(ITHAX_ENABLE_TRINITY_SHADER_COMPILER AND BUILD_TESTING)
    set(SHADER_OWNER_PATH
        "$<TARGET_FILE_DIR:ShaderCompiler>"
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
    )
    string(REPLACE ";" "\\;" _shader_owner_path
        "${SHADER_OWNER_PATH}")
    add_test(
        NAME stage3_shader_compiler_owner_measurement
        COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/stage3-shader-compiler-owner.ps1"
                -ShaderCompilerPath
                "$<TARGET_FILE:ShaderCompiler>"
                -FixturePath
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/stage3_owner.fx"
                -OutputPath
                "artifacts/benchmarks/shader-compiler-owner.jsonl"
                -Workers
                "1,2,4"
    )
    set_tests_properties(
        stage3_shader_compiler_owner_measurement
        PROPERTIES
        ENVIRONMENT "PATH=${_shader_owner_path}"
        LABELS "stage3;provider-owner"
        RUN_SERIAL TRUE
        TIMEOUT 120
    )
endif()

if(ITHAX_ENABLE_TRINITY_DX11)
    add_executable(ithax-stage3-renderer-owner-measurement
        src/stage3_renderer_owner_measurement.cpp
        src/thread_budget.cpp
    )
    target_compile_features(
        ithax-stage3-renderer-owner-measurement PRIVATE cxx_std_20)
    target_include_directories(
        ithax-stage3-renderer-owner-measurement PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(
        ithax-stage3-renderer-owner-measurement PRIVATE
        TrinityAL_dx11 Threads::Threads)

    if(BUILD_TESTING)
        set(_renderer_owner_path
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
        string(REPLACE ";" "\\;" _renderer_owner_escaped
            "${_renderer_owner_path}")
        add_test(
            NAME stage3_renderer_owner_measurement
            COMMAND ithax-stage3-renderer-owner-measurement
        )
        set_tests_properties(
            stage3_renderer_owner_measurement
            PROPERTIES
            ENVIRONMENT "PATH=${_renderer_owner_escaped}"
            LABELS "stage3;provider-owner"
            RUN_SERIAL TRUE
            TIMEOUT 120
        )
    endif()
endif()

add_executable(ithax-stage3-external-owner-measurement
    src/stage3_external_owner_measurement.cpp
    src/thread_budget.cpp
)

target_compile_features(
    ithax-stage3-external-owner-measurement PRIVATE cxx_std_20)
target_include_directories(
    ithax-stage3-external-owner-measurement PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(
    ithax-stage3-external-owner-measurement PRIVATE Threads::Threads)

add_executable(ithax-stage4-marshal-test
    src/network/marshal/marshal.cpp
    src/network/marshal/string_table.cpp
    src/stage4_marshal_test.cpp
)

target_compile_features(ithax-stage4-marshal-test PRIVATE cxx_std_20)
target_include_directories(
    ithax-stage4-marshal-test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

add_executable(ithax-stage4-crypto-test
    src/network/session_crypto.cpp
    src/stage4_crypto_test.cpp
)

target_compile_features(ithax-stage4-crypto-test PRIVATE cxx_std_20)
target_include_directories(
    ithax-stage4-crypto-test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(
    ithax-stage4-crypto-test PRIVATE OpenSSL::Crypto)

add_executable(ithax-stage4-framing-test
    src/network/framing.cpp
    src/stage4_framing_test.cpp
)

target_compile_features(ithax-stage4-framing-test PRIVATE cxx_std_20)
target_include_directories(
    ithax-stage4-framing-test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

add_executable(ithax-stage4-handshake-test
    src/network/handshake.cpp
    src/network/marshal/marshal.cpp
    src/network/marshal/string_table.cpp
    src/stage4_handshake_test.cpp
)

target_compile_features(ithax-stage4-handshake-test PRIVATE cxx_std_20)
target_include_directories(
    ithax-stage4-handshake-test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

add_executable(ithax-stage4-network-integration
    src/network/connection.cpp
    src/network/framing.cpp
    src/network/handshake.cpp
    src/network/marshal/marshal.cpp
    src/network/marshal/string_table.cpp
    src/network/session_crypto.cpp
    src/stage4_network_integration.cpp
)

target_compile_features(ithax-stage4-network-integration PRIVATE cxx_std_20)
target_include_directories(
    ithax-stage4-network-integration PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(
    ithax-stage4-network-integration PRIVATE
    OpenSSL::Crypto ws2_32)

add_executable(ithax-stage4-packet-test
    src/network/dispatcher.cpp
    src/network/marshal/marshal.cpp
    src/network/marshal/string_table.cpp
    src/network/packet.cpp
    src/stage4_packet_test.cpp
)

target_compile_features(ithax-stage4-packet-test PRIVATE cxx_std_20)
target_include_directories(
    ithax-stage4-packet-test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

add_executable(ithax-stage4-reconnect-test
    src/network/connection.cpp
    src/network/framing.cpp
    src/network/handshake.cpp
    src/network/marshal/marshal.cpp
    src/network/marshal/string_table.cpp
    src/network/packet.cpp
    src/network/session_crypto.cpp
    src/stage4_reconnect_test.cpp
)

target_compile_features(ithax-stage4-reconnect-test PRIVATE cxx_std_20)
target_include_directories(
    ithax-stage4-reconnect-test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(
    ithax-stage4-reconnect-test PRIVATE
    OpenSSL::Crypto ws2_32)

add_executable(ithax-stage4-real-server-conformance
    src/network/client.cpp
    src/network/connection.cpp
    src/network/dispatcher.cpp
    src/network/framing.cpp
    src/network/handshake.cpp
    src/network/marshal/marshal.cpp
    src/network/marshal/string_table.cpp
    src/network/packet.cpp
    src/network/session_crypto.cpp
    src/stage4_real_server_conformance.cpp
)

target_compile_features(
    ithax-stage4-real-server-conformance PRIVATE cxx_std_20)
target_include_directories(
    ithax-stage4-real-server-conformance PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(
    ithax-stage4-real-server-conformance PRIVATE
    OpenSSL::Crypto ws2_32)

if(ITHAX_ENABLE_CARBON_GRPC)
    add_executable(ithax-grpc-native-smoke
        src/grpc_native_smoke.cpp
    )

    target_compile_features(ithax-grpc-native-smoke PRIVATE cxx_std_17)
    target_link_libraries(
        ithax-grpc-native-smoke PRIVATE carbon-grpc::module)
endif()

if(MSVC)
    set_property(
        TARGET ithax-foundation-smoke
        PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
    target_compile_definitions(
        ithax-foundation-smoke PRIVATE _ITERATOR_DEBUG_LEVEL=0)
    set_property(
        TARGET ithax-blue-native-smoke
        PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
    target_compile_definitions(
        ithax-blue-native-smoke PRIVATE _ITERATOR_DEBUG_LEVEL=0)
    set_property(
        TARGET ithax-carbon-blue-owner-measurement
        PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
    target_compile_definitions(
        ithax-carbon-blue-owner-measurement PRIVATE _ITERATOR_DEBUG_LEVEL=0)
    set_property(
        TARGET ithax-crashpad-owner-measurement
        PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
    target_compile_definitions(
        ithax-crashpad-owner-measurement PRIVATE _ITERATOR_DEBUG_LEVEL=0)
    set_property(
        TARGET ithax-mesh-native-smoke
        PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
    target_compile_definitions(
        ithax-mesh-native-smoke PRIVATE _ITERATOR_DEBUG_LEVEL=0)
    set_property(
        TARGET ithax-imageio-native-smoke
        PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
    target_compile_definitions(
        ithax-imageio-native-smoke PRIVATE _ITERATOR_DEBUG_LEVEL=0)
    set_property(
        TARGET ithax-resources-native-smoke
        PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
    target_compile_definitions(
        ithax-resources-native-smoke PRIVATE _ITERATOR_DEBUG_LEVEL=0)
    set_property(
        TARGET ithax-trinity-native-smoke
        PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
    target_compile_definitions(
        ithax-trinity-native-smoke PRIVATE _ITERATOR_DEBUG_LEVEL=0)
    set_property(
        TARGET ithax-audio-native-smoke
        PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
    target_compile_definitions(
        ithax-audio-native-smoke PRIVATE _ITERATOR_DEBUG_LEVEL=0)
    set_property(
        TARGET ithax-pathfinder-native-smoke
        PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
    target_compile_definitions(
        ithax-pathfinder-native-smoke PRIVATE _ITERATOR_DEBUG_LEVEL=0)
    set_property(
        TARGET ithax-localization-native-smoke
        PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
    target_compile_definitions(
        ithax-localization-native-smoke PRIVATE _ITERATOR_DEBUG_LEVEL=0)
    set_property(
        TARGET ithax-db-native-smoke
        PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
    target_compile_definitions(
        ithax-db-native-smoke PRIVATE _ITERATOR_DEBUG_LEVEL=0)
    set_property(
        TARGET ithax-parser-native-smoke
        PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
    target_compile_definitions(
        ithax-parser-native-smoke PRIVATE _ITERATOR_DEBUG_LEVEL=0)
    set_property(
        TARGET ithax-geo2-native-smoke
        PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
    target_compile_definitions(
        ithax-geo2-native-smoke PRIVATE _ITERATOR_DEBUG_LEVEL=0)
    set_property(
        TARGET ithax-d3dinfo-native-smoke
        PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
    target_compile_definitions(
        ithax-d3dinfo-native-smoke PRIVATE _ITERATOR_DEBUG_LEVEL=0)
    set_property(
        TARGET ithax-stage2-benchmark
        PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
    target_compile_definitions(
        ithax-stage2-benchmark PRIVATE _ITERATOR_DEBUG_LEVEL=0)
    set_property(
        TARGET ithax-thread-budget-measurement
        PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
    target_compile_definitions(
        ithax-thread-budget-measurement PRIVATE _ITERATOR_DEBUG_LEVEL=0)
    set_property(
        TARGET ithax-stage3-multicore-smoke
        PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
    target_compile_definitions(
        ithax-stage3-multicore-smoke PRIVATE _ITERATOR_DEBUG_LEVEL=0)
    set_property(
        TARGET ithax-stage3-multicore-integration
        PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
    target_compile_definitions(
        ithax-stage3-multicore-integration PRIVATE _ITERATOR_DEBUG_LEVEL=0)
    set_property(
        TARGET ithax-stage3-carbon-host-integration
        PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
    target_compile_definitions(
        ithax-stage3-carbon-host-integration PRIVATE _ITERATOR_DEBUG_LEVEL=0)
    set_property(
        TARGET ithax-stage3-owner-measurement
        PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
    target_compile_definitions(
        ithax-stage3-owner-measurement PRIVATE _ITERATOR_DEBUG_LEVEL=0)
    if(ITHAX_ENABLE_TRINITY_DX11)
        set_property(
            TARGET ithax-stage3-renderer-owner-measurement
            PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
        target_compile_definitions(
            ithax-stage3-renderer-owner-measurement PRIVATE
            _ITERATOR_DEBUG_LEVEL=0)
    endif()
    set_property(
        TARGET ithax-stage3-external-owner-measurement
        PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
    target_compile_definitions(
        ithax-stage3-external-owner-measurement PRIVATE _ITERATOR_DEBUG_LEVEL=0)
    set_property(
        TARGET ithax-stage4-marshal-test
        PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
    target_compile_definitions(
        ithax-stage4-marshal-test PRIVATE _ITERATOR_DEBUG_LEVEL=0)
    set_property(
        TARGET ithax-stage4-crypto-test
        PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
    target_compile_definitions(
        ithax-stage4-crypto-test PRIVATE _ITERATOR_DEBUG_LEVEL=0)
    set_property(
        TARGET ithax-stage4-framing-test
        PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
    target_compile_definitions(
        ithax-stage4-framing-test PRIVATE _ITERATOR_DEBUG_LEVEL=0)
    set_property(
        TARGET ithax-stage4-handshake-test
        PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
    target_compile_definitions(
        ithax-stage4-handshake-test PRIVATE _ITERATOR_DEBUG_LEVEL=0)
    set_property(
        TARGET ithax-stage4-network-integration
        PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
    target_compile_definitions(
        ithax-stage4-network-integration PRIVATE _ITERATOR_DEBUG_LEVEL=0)
    set_property(
        TARGET ithax-stage4-packet-test
        PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
    target_compile_definitions(
        ithax-stage4-packet-test PRIVATE _ITERATOR_DEBUG_LEVEL=0)
    set_property(
        TARGET ithax-stage4-reconnect-test
        PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
    target_compile_definitions(
        ithax-stage4-reconnect-test PRIVATE _ITERATOR_DEBUG_LEVEL=0)
    set_property(
        TARGET ithax-stage4-real-server-conformance
        PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
    target_compile_definitions(
        ithax-stage4-real-server-conformance PRIVATE _ITERATOR_DEBUG_LEVEL=0)
    if(ITHAX_ENABLE_CARBON_GRPC)
        set_property(
            TARGET ithax-grpc-native-smoke
            PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
        target_compile_definitions(
            ithax-grpc-native-smoke PRIVATE _ITERATOR_DEBUG_LEVEL=0)
    endif()
endif()

if(BUILD_TESTING)
    add_test(NAME foundation_smoke COMMAND ithax-foundation-smoke)
    set_tests_properties(
        foundation_smoke
        PROPERTIES ENVIRONMENT "PATH=$<TARGET_FILE_DIR:CcpCore>")

    add_test(
        NAME scheduler_smoke
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/scheduler_smoke.py")
    set(SCHEDULER_TEST_ENVIRONMENT
        "SCHEDULER_PREFIX=${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}"
        "BUILDFLAVOR=debug"
        "PATH=$<TARGET_FILE_DIR:Scheduler>"
    )
    set_tests_properties(
        scheduler_smoke
        PROPERTIES
        ENVIRONMENT "${SCHEDULER_TEST_ENVIRONMENT}"
    )

    add_test(
        NAME io_smoke
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/io_smoke.py"
    )
    set(IO_TEST_ENVIRONMENT
        "CARBON_IO_PREFIX=${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}"
        "BUILDFLAVOR=debug"
        "PATH=${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
    )
    set_tests_properties(
        io_smoke
        PROPERTIES
        ENVIRONMENT "${IO_TEST_ENVIRONMENT}"
    )

    add_test(
        NAME carbon_io_owner_measurement
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/io_owner_measurement.py"
    )
    set_tests_properties(
        carbon_io_owner_measurement
        PROPERTIES
        ENVIRONMENT "${IO_TEST_ENVIRONMENT}"
        LABELS "carbon-owner"
        RUN_SERIAL TRUE
        TIMEOUT 30
    )

    add_test(
        NAME blue_native_smoke
        COMMAND ithax-blue-native-smoke
    )
    set_tests_properties(
        blue_native_smoke
        PROPERTIES ENVIRONMENT "PATH=$<TARGET_FILE_DIR:Blue>"
    )

    add_test(
        NAME carbon_blue_owner_measurement
        COMMAND ithax-carbon-blue-owner-measurement
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/blue"
    )
    set(BLUE_OWNER_TEST_ENVIRONMENT
        "PATH=$<TARGET_FILE_DIR:Blue>"
        "CARBON_BLUE_PREFIX=${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}"
        "PYTHON_STDLIB=${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/python3/Lib"
    )
    set_tests_properties(
        carbon_blue_owner_measurement
        PROPERTIES
        ENVIRONMENT "${BLUE_OWNER_TEST_ENVIRONMENT}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        LABELS "carbon-owner"
        RUN_SERIAL TRUE
        TIMEOUT 30
    )

    set(CRASHPAD_HANDLER_ROOT
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools")
    set(CRASHPAD_HANDLER "${CRASHPAD_HANDLER_ROOT}/crashpad_handler.exe")
    if(NOT EXISTS "${CRASHPAD_HANDLER}")
        message(FATAL_ERROR "Crashpad handler was not installed")
    endif()
    add_test(
        NAME crashpad_owner_measurement
        COMMAND ithax-crashpad-owner-measurement "${CRASHPAD_HANDLER}"
    )
    set_tests_properties(
        crashpad_owner_measurement
        PROPERTIES
        ENVIRONMENT
        "PATH=${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
        LABELS "carbon-owner"
        RUN_SERIAL TRUE
        TIMEOUT 30
    )

    add_test(
        NAME blue_smoke
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/blue_smoke.py"
    )
    set(BLUE_TEST_ENVIRONMENT
        "CARBON_BLUE_PREFIX=${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}"
        "BUILDFLAVOR=debug"
        "PATH=$<TARGET_FILE_DIR:Blue>"
    )
    set_tests_properties(
        blue_smoke
        PROPERTIES
        ENVIRONMENT "${BLUE_TEST_ENVIRONMENT}"
    )

    add_test(
        NAME conformance_stub_smoke
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/conformance_stub.py"
    )

    add_test(
        NAME mesh_native_smoke
        COMMAND ithax-mesh-native-smoke
    )
    set_tests_properties(
        mesh_native_smoke
        PROPERTIES ENVIRONMENT "PATH=$<TARGET_FILE_DIR:CarbonMesh>"
    )

    add_test(
        NAME imageio_native_smoke
        COMMAND ithax-imageio-native-smoke
    )
    set_tests_properties(
        imageio_native_smoke
        PROPERTIES ENVIRONMENT "PATH=$<TARGET_FILE_DIR:CcpCore>"
    )

    add_test(
        NAME resources_native_smoke
        COMMAND ithax-resources-native-smoke
    )
    set_tests_properties(
        resources_native_smoke
        PROPERTIES ENVIRONMENT "PATH=$<TARGET_FILE_DIR:CcpCore>"
    )

    add_test(
        NAME trinity_native_smoke
        COMMAND ithax-trinity-native-smoke
    )
    set_tests_properties(
        trinity_native_smoke
        PROPERTIES ENVIRONMENT "PATH=$<TARGET_FILE_DIR:trinity_stub>"
    )

    set(DESTINY_BIN_DIR
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
    set(DESTINY_LIB_DIR
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib")
    set(_destiny_path
        "${DESTINY_BIN_DIR};${DESTINY_LIB_DIR}")
    string(REPLACE ";" "\\;" _destiny_escaped "${_destiny_path}")
    add_test(
        NAME destiny_smoke
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/destiny_smoke.py"
    )
    set_tests_properties(
        destiny_smoke
        PROPERTIES
        ENVIRONMENT
        "DESTINY_PREFIX=${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET};PATH=${_destiny_escaped}"
    )

    add_test(
        NAME audio_native_smoke
        COMMAND ithax-audio-native-smoke
    )
    set_tests_properties(
        audio_native_smoke
        PROPERTIES ENVIRONMENT "PATH=$<TARGET_FILE_DIR:CarbonAudio>"
    )

    add_test(
        NAME pathfinder_native_smoke
        COMMAND ithax-pathfinder-native-smoke
    )
    set_tests_properties(
        pathfinder_native_smoke
        PROPERTIES ENVIRONMENT "PATH=$<TARGET_FILE_DIR:EvePathfinder>"
    )

    add_test(
        NAME localization_native_smoke
        COMMAND ithax-localization-native-smoke
    )
    set_tests_properties(
        localization_native_smoke
        PROPERTIES ENVIRONMENT "PATH=$<TARGET_FILE_DIR:EveLocalization>"
    )

    add_test(
        NAME db_native_smoke
        COMMAND ithax-db-native-smoke
    )
    set_tests_properties(
        db_native_smoke
        PROPERTIES ENVIRONMENT "PATH=$<TARGET_FILE_DIR:db>"
    )

    add_test(
        NAME parser_native_smoke
        COMMAND ithax-parser-native-smoke
    )

    add_test(
        NAME geo2_native_smoke
        COMMAND ithax-geo2-native-smoke
    )
    set_tests_properties(
        geo2_native_smoke
        PROPERTIES ENVIRONMENT "PATH=$<TARGET_FILE_DIR:geo2>"
    )

    add_test(
        NAME d3dinfo_native_smoke
        COMMAND ithax-d3dinfo-native-smoke
    )
    set_tests_properties(
        d3dinfo_native_smoke
        PROPERTIES ENVIRONMENT "PATH=$<TARGET_FILE_DIR:d3dinfo>"
    )

    add_test(
        NAME stage2_benchmark_smoke
        COMMAND ithax-stage2-benchmark
                --workers 2
                --tasks 1000
                --entities 1000
                --repetitions 3
    )

    add_test(
        NAME stage3_multicore_smoke
        COMMAND ithax-stage3-multicore-smoke
    )
    set_tests_properties(
        stage3_multicore_smoke
        PROPERTIES
        LABELS "stage3"
        RUN_SERIAL TRUE
        TIMEOUT 30
    )

    add_test(
        NAME stage3_multicore_integration
        COMMAND ithax-stage3-multicore-integration
    )
    set_tests_properties(
        stage3_multicore_integration
        PROPERTIES
        LABELS "stage3"
        RUN_SERIAL TRUE
        TIMEOUT 120
    )

    add_test(
        NAME stage3_carbon_host_integration
        COMMAND ithax-stage3-carbon-host-integration
                --fail-on-deadline
    )
    set(BLUE_HOST_TEST_PATH
        "$<TARGET_FILE_DIR:Blue>"
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/python"
    )
    string(REPLACE ";" "\\;" _blue_host_escaped_path
        "${BLUE_HOST_TEST_PATH}")
    set_tests_properties(
        stage3_carbon_host_integration
        PROPERTIES
        ENVIRONMENT
        "CARBON_HOST_PREFIX=${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET};PYTHON_STDLIB=${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/python3/Lib;PATH=${_blue_host_escaped_path}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        LABELS "stage3;carbon-owner"
        RUN_SERIAL TRUE
        TIMEOUT 180
    )

    add_test(
        NAME stage3_owner_measurement
        COMMAND ithax-stage3-owner-measurement
    )
    set(OWNER_TEST_PATH
        "$<TARGET_FILE_DIR:trinity_stub>"
        "$<TARGET_FILE_DIR:Tracy::TracyClient>"
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
    )
    string(REPLACE ";" "\\;" _owner_escaped_path
        "${OWNER_TEST_PATH}")
    set_tests_properties(
        stage3_owner_measurement
        PROPERTIES
        ENVIRONMENT "PATH=${_owner_escaped_path}"
        LABELS "stage3;carbon-owner"
        RUN_SERIAL TRUE
        TIMEOUT 60
    )

    add_test(
        NAME stage3_external_owner_measurement
        COMMAND ithax-stage3-external-owner-measurement
                --repetitions 1
                --owners 2
    )
    set_tests_properties(
        stage3_external_owner_measurement
        PROPERTIES
        LABELS "stage3"
        RUN_SERIAL TRUE
        TIMEOUT 30
    )

    add_test(
        NAME stage4_marshal_test
        COMMAND ithax-stage4-marshal-test
    )
    set_tests_properties(
        stage4_marshal_test
        PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        LABELS "stage4"
        TIMEOUT 60
    )

    add_test(
        NAME stage4_crypto_test
        COMMAND ithax-stage4-crypto-test
    )
    set_tests_properties(
        stage4_crypto_test
        PROPERTIES
        LABELS "stage4"
        TIMEOUT 30
    )

    add_test(
        NAME stage4_framing_test
        COMMAND ithax-stage4-framing-test
    )
    set_tests_properties(
        stage4_framing_test
        PROPERTIES
        LABELS "stage4"
        TIMEOUT 30
    )

    add_test(
        NAME stage4_handshake_test
        COMMAND ithax-stage4-handshake-test
    )
    set_tests_properties(
        stage4_handshake_test
        PROPERTIES
        LABELS "stage4"
        TIMEOUT 30
    )

    add_test(
        NAME stage4_network_integration
        COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/stage4-network-integration.ps1"
                -ClientPath
                "$<TARGET_FILE:ithax-stage4-network-integration>"
                -PythonPath
                "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/python3/python.exe"
                -MockServerPath
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/stage4_mock_server.py"
                -Port
                "26001"
    )
    set_tests_properties(
        stage4_network_integration
        PROPERTIES
        LABELS "stage4"
        RUN_SERIAL TRUE
        TIMEOUT 60
    )

    add_test(
        NAME stage4_packet_test
        COMMAND ithax-stage4-packet-test
    )
    set_tests_properties(
        stage4_packet_test
        PROPERTIES
        LABELS "stage4"
        TIMEOUT 30
    )

    add_test(
        NAME stage4_reconnect_test
        COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/stage4-network-integration.ps1"
                -ClientPath
                "$<TARGET_FILE:ithax-stage4-reconnect-test>"
                -PythonPath
                "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/python3/python.exe"
                -MockServerPath
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/stage4_mock_server.py"
                -Port
                "26001"
    )
    set_tests_properties(
        stage4_reconnect_test
        PROPERTIES
        LABELS "stage4"
        RUN_SERIAL TRUE
        TIMEOUT 60
    )

    add_test(
        NAME thread_budget_measurement_smoke
        COMMAND ithax-thread-budget-measurement
                --workers 2
                --repetitions 1
    )

    add_test(
        NAME thread_budget_rejects_no_headroom
        COMMAND ithax-thread-budget-measurement
                --workers 1
                --hard-reserved 64
    )
    set_tests_properties(
        thread_budget_rejects_no_headroom
        PROPERTIES WILL_FAIL TRUE
    )

    if(ITHAX_ENABLE_CARBON_GRPC)
        add_test(
            NAME grpc_native_smoke
            COMMAND ithax-grpc-native-smoke
        )
    endif()

    set(EXEFILE_ROOT
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
    set(EXEFILE_TOOL_DIR "${EXEFILE_ROOT}/tools/carbon-exefile")
    set(EXEFILE_TOOL "${EXEFILE_TOOL_DIR}/exefile_Debug.exe")
    if(NOT EXISTS "${EXEFILE_TOOL}")
        message(FATAL_ERROR "Carbon Exefile tool was not installed")
    endif()
    add_test(
        NAME exefile_native_smoke
        COMMAND "${EXEFILE_TOOL}" /?
    )
    set(EXEFILE_TEST_PATH "${EXEFILE_TOOL_DIR};${EXEFILE_ROOT}/bin")
    string(REPLACE ";" "\\;" _exefile_escaped_path
        "${EXEFILE_TEST_PATH}")
    set_tests_properties(
        exefile_native_smoke
        PROPERTIES ENVIRONMENT "PATH=${_exefile_escaped_path}"
    )

    add_test(
        NAME exefile_py_smoke
        COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/exefile-py-smoke.ps1"
                -ExefilePath
                "${EXEFILE_TOOL}"
                -Prefix
                "${EXEFILE_ROOT}"
    )
    set_tests_properties(
        exefile_py_smoke
        PROPERTIES ENVIRONMENT "PATH=${_exefile_escaped_path}"
    )
endif()
