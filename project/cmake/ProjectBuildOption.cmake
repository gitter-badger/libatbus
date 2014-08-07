# 默认配置选项
#####################################################################
option(BUILD_SHARED_LIBS "Build shared libraries (DLLs)." OFF)

# atbus 选项
set(ATBUS_MACRO_BUSID_TYPE "uint64_t" CACHE STRING "busid type of atbus")

# 测试配置选项
set(GTEST_ROOT "" CACHE STRING "GTest root directory")
set(BOOST_ROOT "" CACHE STRING "Boost root directory")
option(PROJECT_TEST_ENABLE_BOOST_UNIT_TEST "Enable boost unit test." OFF)

