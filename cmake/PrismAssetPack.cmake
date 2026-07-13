include_guard(GLOBAL)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

function(prism_add_asset_pack TARGET)
    set(options)
    set(oneValueArgs ID NAME VERSION TARGET_ID TARGET_MIN_VERSION TARGET_MAX_VERSION OUTPUT)
    set(multiValueArgs FILES)
    cmake_parse_arguments(PACK "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
    foreach(REQUIRED ID NAME VERSION TARGET_ID OUTPUT)
        if(NOT PACK_${REQUIRED})
            message(FATAL_ERROR "prism_add_asset_pack(${TARGET}) requires ${REQUIRED}")
        endif()
    endforeach()
    if(NOT PACK_FILES)
        message(FATAL_ERROR "prism_add_asset_pack(${TARGET}) requires FILES")
    endif()
    if(NOT DEFINED PACK_TARGET_MIN_VERSION OR PACK_TARGET_MIN_VERSION STREQUAL "")
        set(PACK_TARGET_MIN_VERSION 0)
    endif()
    if(NOT DEFINED PACK_TARGET_MAX_VERSION OR PACK_TARGET_MAX_VERSION STREQUAL "")
        set(PACK_TARGET_MAX_VERSION 0)
    endif()
    set(FILE_ARGS "")
    set(FILE_DEPENDS "")
    foreach(FILE_SPEC IN LISTS PACK_FILES)
        string(FIND "${FILE_SPEC}" "=" EQUAL_INDEX)
        if(EQUAL_INDEX LESS 1)
            message(FATAL_ERROR "asset files must use path=source syntax: ${FILE_SPEC}")
        endif()
        math(EXPR SOURCE_INDEX "${EQUAL_INDEX} + 1")
        string(SUBSTRING "${FILE_SPEC}" ${SOURCE_INDEX} -1 SOURCE_FILE)
        list(APPEND FILE_ARGS --file "${FILE_SPEC}")
        list(APPEND FILE_DEPENDS "${SOURCE_FILE}")
    endforeach()
    add_custom_command(
        OUTPUT "${PACK_OUTPUT}"
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../scripts/package_assets.py"
            --output "${PACK_OUTPUT}"
            --id "${PACK_ID}"
            --name "${PACK_NAME}"
            --version "${PACK_VERSION}"
            --target-id "${PACK_TARGET_ID}"
            --target-min-version "${PACK_TARGET_MIN_VERSION}"
            --target-max-version "${PACK_TARGET_MAX_VERSION}"
            ${FILE_ARGS}
        DEPENDS ${FILE_DEPENDS} "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../scripts/package_assets.py"
        VERBATIM
        COMMENT "Packaging Prism asset pack ${PACK_NAME}"
    )
    add_custom_target(${TARGET} DEPENDS "${PACK_OUTPUT}")
    set(${TARGET}_PRISM_PACK_FILE "${PACK_OUTPUT}" PARENT_SCOPE)
endfunction()
