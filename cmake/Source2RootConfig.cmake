include(CMakeFindDependencyMacro)
find_dependency(KeelS2 1.0 CONFIG)

if(NOT TARGET Source2Root::Extension)
    add_library(Source2Root::Extension INTERFACE IMPORTED)
    get_filename_component(_sr_prefix "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
    set_target_properties(Source2Root::Extension PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${_sr_prefix}/include"
        INTERFACE_LINK_LIBRARIES "KeelS2::SDK;KeelS2::SourceSDK")
    unset(_sr_prefix)
endif()
