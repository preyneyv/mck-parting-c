function(prism_check_cartridge_files)
    foreach(FILE IN LISTS ARGN)
        file(READ "${FILE}" CONTENT)
        if(CONTENT MATCHES "#[ \t]*include[ \t]*[<\"]+(shared/|platform/|host/|rp2/)")
            message(FATAL_ERROR
                "Cartridge crosses the Prism SDK boundary: ${FILE}\n"
                "Include prism/* or cartridge-local headers instead."
            )
        endif()
        if(CONTENT MATCHES "(^|[^A-Za-z0-9_])g_engine([^A-Za-z0-9_]|$)")
            message(FATAL_ERROR
                "Cartridge accesses OS engine state directly: ${FILE}"
            )
        endif()
    endforeach()
endfunction()

function(prism_check_cartridge_boundary ROOT)
    file(GLOB_RECURSE CARTRIDGE_CODE CONFIGURE_DEPENDS
        "${ROOT}/*.c"
        "${ROOT}/*.h"
    )
    prism_check_cartridge_files(${CARTRIDGE_CODE})
endfunction()
