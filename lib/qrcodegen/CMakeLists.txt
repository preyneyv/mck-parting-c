add_library(qrcodegen STATIC qrcodegen.c)

target_include_directories(qrcodegen PUBLIC ${CMAKE_CURRENT_LIST_DIR})
