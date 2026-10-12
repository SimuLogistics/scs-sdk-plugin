cmake_minimum_required(VERSION 3.16)

# Cross-platform build of the telemetry plugin.
# Windows: scs-telemetry.dll, Linux: libscs-telemetry.so, macOS: libscs-telemetry.dylib.
# The Visual Studio solution in scs-telemetry/vs2012 still works on Windows.
project(scs-telemetry LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(scs-telemetry SHARED
  scs-telemetry/src/scs_telemetry.cpp
  scs-telemetry/src/scs_config_handlers.cpp
  scs-telemetry/src/scs_gameplay_event_handlers.cpp
  scs-telemetry/src/sharedmemory.cpp
)

target_include_directories(scs-telemetry PRIVATE
  scs-telemetry/inc
  scs_sdk/include
)

if(WIN32)
  target_sources(scs-telemetry PRIVATE scs-telemetry/vs2012/scs-telemetry.def)
  target_compile_definitions(scs-telemetry PRIVATE _CRT_SECURE_NO_WARNINGS)
  # Ship a self-contained DLL (no Visual C++ redistributable needed).
  set_property(TARGET scs-telemetry PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
  # Only the two SDK entry points are exported.
  set_target_properties(scs-telemetry PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Static libstdc++ so the plugin does not depend on the player's distro.
    target_link_options(scs-telemetry PRIVATE -static-libstdc++ -static-libgcc)
    target_link_libraries(scs-telemetry PRIVATE rt)
  endif()
endif()

# Smoke test: loads the plugin with stub SDK callbacks and checks the shared memory.
include(CTest)
if(BUILD_TESTING)
  add_executable(smoke_test tests/smoke_test.cpp)
  target_include_directories(smoke_test PRIVATE scs-telemetry/inc scs_sdk/include)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(smoke_test PRIVATE dl rt)
  endif()
  add_dependencies(smoke_test scs-telemetry)
  add_test(NAME smoke_test COMMAND smoke_test $<TARGET_FILE:scs-telemetry>)
endif()
