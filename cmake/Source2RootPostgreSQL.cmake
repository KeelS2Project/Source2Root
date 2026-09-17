option(SR_POSTGRESQL_DRIVER "Build pinned PostgreSQL database driver" ON)
if(SR_DATABASE_EXTENSION AND SR_POSTGRESQL_DRIVER)
    include(FetchContent)
    include(ExternalProject)
    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
    find_package(OpenSSL REQUIRED)
    find_package(Threads REQUIRED)
    find_program(SR_NINJA NAMES ninja ninja-build REQUIRED)
    find_program(SR_PG_FLEX NAMES flex win_flex REQUIRED)
    find_program(SR_PG_BISON NAMES bison win_bison REQUIRED)
    file(READ "${CMAKE_SOURCE_DIR}/dependencies.lock.json" SR_PG_LOCK)
    foreach(dependency postgresql meson)
        string(JSON url GET "${SR_PG_LOCK}" "${dependency}" url)
        string(JSON hash GET "${SR_PG_LOCK}" "${dependency}" sha256)
        if(dependency STREQUAL "meson")
            FetchContent_Declare(sr_meson URL "${url}" URL_HASH "SHA256=${hash}" DOWNLOAD_NAME meson.zip DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        else()
            FetchContent_Declare(sr_postgresql URL "${url}" URL_HASH "SHA256=${hash}" DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        endif()
    endforeach()
    FetchContent_MakeAvailable(sr_meson sr_postgresql)
    set(SR_PG_ARGS --flex "${SR_PG_FLEX}" --bison "${SR_PG_BISON}")
    if(SR_SANITIZERS AND NOT MSVC)
        list(APPEND SR_PG_ARGS --sanitize)
    endif()
    if(WIN32)
        list(APPEND SR_PG_ARGS --windows)
    endif()
    set(SR_PQ_ARCHIVE "${sr_postgresql_BINARY_DIR}/src/interfaces/libpq/libpq.a")
    set(SR_PG_COMMON "${sr_postgresql_BINARY_DIR}/src/common/libpgcommon_shlib.a")
    set(SR_PG_PORT "${sr_postgresql_BINARY_DIR}/src/port/libpgport_shlib.a")
    ExternalProject_Add(sr_libpq_build
        SOURCE_DIR "${sr_postgresql_SOURCE_DIR}" BINARY_DIR "${sr_postgresql_BINARY_DIR}"
        DOWNLOAD_COMMAND "" UPDATE_COMMAND "" PATCH_COMMAND ""
        CONFIGURE_COMMAND "${CMAKE_COMMAND}" -E env "PYTHONPATH=${sr_meson_SOURCE_DIR}"
            "CC=${CMAKE_C_COMPILER}" "CXX=${CMAKE_CXX_COMPILER}"
            "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/cmake/configure_postgresql.py"
            "${sr_postgresql_SOURCE_DIR}" "${sr_postgresql_BINARY_DIR}" ${SR_PG_ARGS}
        BUILD_COMMAND "${CMAKE_COMMAND}" -E env "PYTHONPATH=${sr_meson_SOURCE_DIR}"
            "${SR_NINJA}" -C "${sr_postgresql_BINARY_DIR}" -j 4
            src/interfaces/libpq/libpq.a src/common/libpgcommon_shlib.a src/port/libpgport_shlib.a
        INSTALL_COMMAND ""
        BUILD_BYPRODUCTS "${SR_PQ_ARCHIVE}" "${SR_PG_COMMON}" "${SR_PG_PORT}")
    ExternalProject_Add_StepDependencies(sr_libpq_build configure "${CMAKE_SOURCE_DIR}/cmake/configure_postgresql.py")
    file(MAKE_DIRECTORY "${sr_postgresql_BINARY_DIR}/src/include")
    add_library(sr_libpq STATIC IMPORTED GLOBAL)
    set_target_properties(sr_libpq PROPERTIES IMPORTED_LOCATION "${SR_PQ_ARCHIVE}"
        INTERFACE_INCLUDE_DIRECTORIES "${sr_postgresql_SOURCE_DIR}/src/interfaces/libpq;${sr_postgresql_SOURCE_DIR}/src/include;${sr_postgresql_BINARY_DIR}/src/include"
        INTERFACE_LINK_LIBRARIES "${SR_PG_COMMON};${SR_PG_PORT};OpenSSL::SSL;OpenSSL::Crypto;Threads::Threads;${CMAKE_DL_LIBS}")
    if(WIN32)
        set_property(TARGET sr_libpq APPEND PROPERTY INTERFACE_LINK_LIBRARIES ws2_32 secur32 crypt32)
    endif()
    add_dependencies(sr_libpq sr_libpq_build)
    add_library(sr_postgresql STATIC extensions/postgresql/driver.cpp)
    target_include_directories(sr_postgresql PUBLIC extensions/postgresql)
    target_link_libraries(sr_postgresql PUBLIC sr_database PRIVATE sr_libpq Threads::Threads)
    target_link_libraries(source2root_database PRIVATE sr_postgresql)
    target_compile_definitions(source2root_database PRIVATE SR_POSTGRESQL_DRIVER=1)
endif()
