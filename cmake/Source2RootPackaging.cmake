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

foreach(name random database clientprefs geoip regex http sdktools cstrike dhooks)
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
            endif()
            file(TO_CMAKE_PATH "${source}" source)
            string(REPLACE "\"" "\\\"" source "${source}")
            list(APPEND SR_PACKAGE_DEPENDENCIES
                "{\"name\":\"${key}\",\"fetchcontent\":\"${dependency}\",\"source\":\"${source}\"}")
        endif()
    endif()
endforeach()
list(JOIN SR_PACKAGE_MODULES ",\n    " modules)
list(JOIN SR_PACKAGE_LIBRARIES ",\n    " libraries)
list(JOIN SR_PACKAGE_DEPENDENCIES ", " dependencies)
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/extension-package-$<CONFIG>.json" CONTENT
"{\n  \"schema\":1,\n  \"platform\":\"${SR_PACKAGE_PLATFORM}\",\n  \"configuration\":\"$<CONFIG>\",\n  \"modules\":[\n    ${modules}\n  ],\n  \"libraries\":[\n    ${libraries}\n  ],\n  \"dependencies\":[${dependencies}]\n}\n")
