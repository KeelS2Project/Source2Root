function(sr_mariadb_dependency)
    set(OPT SR_MARIADB_)
    set(SR_MARIADB_WITH_UNIT_TESTS OFF)
    set(SR_MARIADB_WITH_DYNCOL OFF)
    set(SR_MARIADB_WITH_CURL OFF)
    set(SR_MARIADB_WITH_EXTERNAL_ZLIB OFF)
    set(SR_MARIADB_WITH_SSL ON)
    set(SR_MARIADB_DEFAULT_SSL_VERIFY_SERVER_CERT ON)

    foreach(plugin DIALOG PARSEC AUTH_GSSAPI_CLIENT MYSQL_OLD_PASSWORD MYSQL_CLEAR_PASSWORD REMOTE_IO REPLICATION)
        set(CLIENT_PLUGIN_${plugin} OFF)
    endforeach()

    foreach(plugin MYSQL_NATIVE_PASSWORD CACHING_SHA2_PASSWORD SHA256_PASSWORD CLIENT_ED25519
                   PVIO_SOCKET PVIO_NPIPE PVIO_SHMEM)
        set(CLIENT_PLUGIN_${plugin} STATIC)
    endforeach()

    file(READ "${CMAKE_SOURCE_DIR}/dependencies.lock.json" lock)
    string(JSON url GET "${lock}" mariadb_connector_c url)
    string(JSON hash GET "${lock}" mariadb_connector_c sha256)
    # Populate the unmodified dependency, then build a private patched copy.
    FetchContent_Declare(sr_mariadb URL "${url}" URL_HASH "SHA256=${hash}"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR source2root-unused)
    FetchContent_MakeAvailable(sr_mariadb)
    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
    set(SR_MARIADB_EFFECTIVE_SOURCE "${CMAKE_BINARY_DIR}/_deps/sr_mariadb-strict-src")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/cmake/prepare_mariadb.py")
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/cmake/prepare_mariadb.py"
        "${sr_mariadb_SOURCE_DIR}" "${SR_MARIADB_EFFECTIVE_SOURCE}" COMMAND_ERROR_IS_FATAL ANY)
    add_subdirectory("${SR_MARIADB_EFFECTIVE_SOURCE}" "${sr_mariadb_BINARY_DIR}")

    if(WIN32)
        target_compile_definitions(mariadb_obj PRIVATE WIN32_LEAN_AND_MEAN NOGDI)
    endif()

    set_target_properties(libmariadb PROPERTIES OUTPUT_NAME "libsource2root_mariadb")
    target_include_directories(libmariadb INTERFACE "${SR_MARIADB_EFFECTIVE_SOURCE}/include" "${sr_mariadb_BINARY_DIR}/include")
    set(SR_MARIADB_EFFECTIVE_SOURCE "${SR_MARIADB_EFFECTIVE_SOURCE}" PARENT_SCOPE)
endfunction()
