set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_ENV_PASSTHROUGH SR_WINDOWS_DEPENDENCY_ROOT)

if(NOT DEFINED ENV{SR_WINDOWS_DEPENDENCY_ROOT})
    message(FATAL_ERROR "Use tools/windows_dependencies.py to select the dependency build root")
endif()

file(TO_CMAKE_PATH "$ENV{SR_WINDOWS_DEPENDENCY_ROOT}" dependency_root)
set(VCPKG_C_FLAGS "/experimental:deterministic /pathmap:\"${dependency_root}\"=dependencies")
set(VCPKG_CXX_FLAGS "${VCPKG_C_FLAGS}")
