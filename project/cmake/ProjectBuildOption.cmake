# 默认配置选项
#####################################################################
option(BUILD_SHARED_LIBS "Build shared libraries (DLLs)." OFF)

# 测试配置选项
set(GTEST_ROOT "" CACHE STRING "GTest root directory")
set(BOOST_ROOT "" CACHE STRING "Boost root directory")
option(PROJECT_TEST_ENABLE_BOOST_UNIT_TEST "Enable boost unit test." OFF)

