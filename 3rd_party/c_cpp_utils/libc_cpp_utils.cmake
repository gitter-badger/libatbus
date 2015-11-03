
# =========== 3rdparty c_cpp_utils ==================
set (3RD_PARTY_C_CPP_UTILS_BASE_DIR ${CMAKE_CURRENT_LIST_DIR})
set (3RD_PARTY_C_CPP_UTILS_PKG_DIR "${3RD_PARTY_C_CPP_UTILS_BASE_DIR}/repo")

set (3RD_PARTY_C_CPP_UTILS_INC_DIR "${3RD_PARTY_C_CPP_UTILS_PKG_DIR}/include")
set (3RD_PARTY_C_CPP_UTILS_SRC_DIR "${3RD_PARTY_C_CPP_UTILS_PKG_DIR}/src")
set (3RD_PARTY_C_CPP_UTILS_LINK_NAME c_cpp_utils)

include_directories(${3RD_PARTY_C_CPP_UTILS_INC_DIR})

message(STATUS "tmp c&cpp utils inc=${3RD_PARTY_C_CPP_UTILS_INC_DIR}")
message(STATUS "tmp c&cpp utils src=${3RD_PARTY_C_CPP_UTILS_INC_DIR}")
message(STATUS "tmp c&cpp utils libname=${3RD_PARTY_C_CPP_UTILS_LINK_NAME}")