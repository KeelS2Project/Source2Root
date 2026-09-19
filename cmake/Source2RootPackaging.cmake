# The packager consumes this generated inventory rather than guessing binary
# names or treating a disabled extension as a missing build artifact.
if(WIN32)
    set(SR_PACKAGE_PLATFORM win64)
else()
    set(SR_PACKAGE_PLATFORM linuxsteamrt64)
endif()

set(SR_EXTENSION_DESTINATION "addons/keels2/plugins/${SR_PACKAGE_PLATFORM}")
set(SR_PACKAGE_MODULES "")
set(SR_PACKAGE_LIBRARIES "")
set(SR_PACKAGE_DEPENDENCIES "")

foreach(name random database clientprefs geoip regex http sdktools cstrike dhooks sdkhooks topmenus)
    set(target "source2root_${name}")

    if(TARGET ${target})
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            # Keel stages plugins in <plugin-dir>/.runtime/<handle>. Keep shared
            # libraries outside plugin discovery and resolve from either place.
            set_target_properties(${target} PROPERTIES
                INSTALL_RPATH "$ORIGIN/lib;$ORIGIN/../../lib"
                INSTALL_RPATH_USE_LINK_PATH FALSE)
        endif()

        set(filename "${target}${CMAKE_SHARED_MODULE_SUFFIX}")
        install(TARGETS ${target}
            LIBRARY DESTINATION "${SR_EXTENSION_DESTINATION}" COMPONENT Source2RootExtensions
            RUNTIME DESTINATION "${SR_EXTENSION_DESTINATION}" COMPONENT Source2RootExtensions)
        list(APPEND SR_PACKAGE_MODULES
            "{\"name\":\"${name}\",\"target\":\"${target}\",\"installed\":\"${SR_EXTENSION_DESTINATION}/$<TARGET_FILE_NAME:${target}>\",\"filename\":\"${filename}\"}")
    endif()
endforeach()

foreach(target sr_mysql libmariadb)
    if(TARGET ${target})
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            set_target_properties(${target} PROPERTIES INSTALL_RPATH "$ORIGIN" INSTALL_RPATH_USE_LINK_PATH FALSE)
        endif()

        install(TARGETS ${target}
            LIBRARY DESTINATION "${SR_EXTENSION_DESTINATION}/lib" COMPONENT Source2RootExtensions
            RUNTIME DESTINATION "${SR_EXTENSION_DESTINATION}/lib" COMPONENT Source2RootExtensions)
        list(APPEND SR_PACKAGE_LIBRARIES
            "{\"target\":\"${target}\",\"installed\":\"${SR_EXTENSION_DESTINATION}/lib/$<TARGET_FILE_NAME:${target}>\"}")
    endif()
endforeach()

foreach(pair "sr_sqlite|sqlite" "sr_mariadb|mariadb_connector_c" "sr_maxmind|libmaxminddb"
             "sr_pcre2|pcre2" "sr_curl|curl" "sr_postgresql|postgresql" "sr_meson|meson")
    string(REPLACE "|" ";" fields "${pair}")
    list(GET fields 0 dependency)
    list(GET fields 1 key)

    if(COMMAND FetchContent_GetProperties)
        FetchContent_GetProperties(${dependency} SOURCE_DIR source POPULATED populated)

        if(populated)
            if(dependency STREQUAL "sr_postgresql")
                set(source "${SR_PG_EFFECTIVE_SOURCE}")
            elseif(dependency STREQUAL "sr_mariadb")
                set(source "${SR_MARIADB_EFFECTIVE_SOURCE}")
            endif()

            file(TO_CMAKE_PATH "${source}" source)
            string(REPLACE "\"" "\\\"" source "${source}")
            list(APPEND SR_PACKAGE_DEPENDENCIES
                "{\"name\":\"${key}\",\"fetchcontent\":\"${dependency}\",\"source\":\"${source}\"}")
        endif()
    endif()
endforeach()

set(SR_REQUIRED_EXTERNAL_SOURCES "")

if(WIN32)
    foreach(pair "openssl|OpenSSL::SSL" "zlib|ZLIB::ZLIB")
        string(REPLACE "|" ";" fields "${pair}")
        list(GET fields 0 name)
        list(GET fields 1 target)

        if(TARGET ${target})
            list(APPEND SR_REQUIRED_EXTERNAL_SOURCES "\"${name}\"")
            string(TOUPPER "${name}" key)
            set(SR_${key}_SOURCE "" CACHE PATH "Corresponding ${name} source used by the Windows dependency build")

            if(SR_${key}_SOURCE)
                file(TO_CMAKE_PATH "${SR_${key}_SOURCE}" source)
                string(REPLACE "\"" "\\\"" source "${source}")
                list(APPEND SR_PACKAGE_DEPENDENCIES "{\"name\":\"${name}\",\"source\":\"${source}\"}")
            endif()
        endif()
    endforeach()

    if(SR_REQUIRED_EXTERNAL_SOURCES)
        list(APPEND SR_REQUIRED_EXTERNAL_SOURCES "\"vcpkg\"")
        set(SR_VCPKG_SOURCE "" CACHE PATH "Locked vcpkg repository providing the Windows dependency build recipes")

        if(SR_VCPKG_SOURCE)
            file(TO_CMAKE_PATH "${SR_VCPKG_SOURCE}" source)
            string(REPLACE "\"" "\\\"" source "${source}")
            list(APPEND SR_PACKAGE_DEPENDENCIES "{\"name\":\"vcpkg\",\"source\":\"${source}\",\"git_snapshot\":true}")
        endif()
    endif()
endif()

list(JOIN SR_PACKAGE_MODULES ",\n    " modules)
list(JOIN SR_PACKAGE_LIBRARIES ",\n    " libraries)
list(JOIN SR_PACKAGE_DEPENDENCIES ", " dependencies)
list(JOIN SR_REQUIRED_EXTERNAL_SOURCES ", " external_sources)
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/extension-package-$<CONFIG>.json" CONTENT
"{\n  \"schema\":1,\n  \"platform\":\"${SR_PACKAGE_PLATFORM}\",\n  \"configuration\":\"$<CONFIG>\",\n  \"modules\":[\n    ${modules}\n  ],\n  \"libraries\":[\n    ${libraries}\n  ],\n  \"dependencies\":[${dependencies}],\n  \"required_external_sources\":[${external_sources}]\n}\n")
