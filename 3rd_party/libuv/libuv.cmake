# =========== 3rd_party =========== 
set (3RD_PARTY_LIBUV_ROOT_DIR ${CMAKE_CURRENT_LIST_DIR})


find_package(Libuv)

if (NOT LIBUV_FOUND)
    message(FATAL_ERROR "libuv is required.")
endif()

set(3RD_PARTY_LIBUV_INC_DIR ${Libuv_INCLUDE_DIRS})
set(3RD_PARTY_LIBUV_LINK_NAME ${Libuv_LIBRARIES})

include_directories(${3RD_PARTY_LIBUV_INC_DIR})