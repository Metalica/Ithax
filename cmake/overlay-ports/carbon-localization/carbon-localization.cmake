if(TARGET EveLocalization)
    return()
endif()

get_filename_component(_IMPORT_PREFIX "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
get_filename_component(_IMPORT_PREFIX "${_IMPORT_PREFIX}" DIRECTORY)

add_library(EveLocalization SHARED IMPORTED)
set_property(
    TARGET EveLocalization
    APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG
)
set_target_properties(
    EveLocalization
    PROPERTIES
        IMPORTED_IMPLIB_DEBUG
            "${_IMPORT_PREFIX}/lib/_evelocalization_debug.lib"
        IMPORTED_LOCATION_DEBUG
            "${_IMPORT_PREFIX}/bin/_evelocalization_debug.pyd"
)

list(APPEND _cmake_import_check_targets EveLocalization)
list(
    APPEND
    _cmake_import_check_files_for_EveLocalization
    "${_IMPORT_PREFIX}/lib/_evelocalization_debug.lib"
    "${_IMPORT_PREFIX}/bin/_evelocalization_debug.pyd"
)

foreach(_cmake_file IN LISTS _cmake_import_check_files_for_EveLocalization)
    if(NOT EXISTS "${_cmake_file}")
        message(FATAL_ERROR
            "The imported target EveLocalization references a missing file: "
            "${_cmake_file}"
        )
    endif()
endforeach()

unset(_cmake_file)
unset(_IMPORT_PREFIX)
