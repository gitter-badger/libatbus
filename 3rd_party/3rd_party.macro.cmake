# =========== 3rd_party =========== 
set (PROJECT_3RDPARTY_ROOT_DIR "${CMAKE_SOURCE_DIR}/3rd_party")



# ------------- libuv -------------
find_package(Libuv)
if(NOT LIBUV_FOUND)
	execute_process(
		COMMAND git init
		COMMAND git update "3rd_party/libuv"
	)
    if(MSVC)
		file(MAKE_DIRECTORY "${PROJECT_3RDPARTY_ROOT_DIR}/libuv/lib")
        if ( "Debug" STREQUAL "${CMAKE_BUILD_TYPE}")
            execute_process(
                COMMAND "vcbuild.bat" debug
                WORKING_DIRECTORY "${PROJECT_3RDPARTY_ROOT_DIR}/libuv"
            )
			
            file(
                COPY "${PROJECT_3RDPARTY_ROOT_DIR}/libuv/Debug/lib" 
                DESTINATION "${PROJECT_3RDPARTY_ROOT_DIR}/libuv/lib"
                USE_SOURCE_PERMISSIONS
            )
        else()
            execute_process(
                COMMAND "vcbuild.bat" release
                WORKING_DIRECTORY "${PROJECT_3RDPARTY_ROOT_DIR}/libuv"
            )
			
            file(
                COPY "${PROJECT_3RDPARTY_ROOT_DIR}/libuv/Release/lib" 
                DESTINATION "${PROJECT_3RDPARTY_ROOT_DIR}/libuv/lib"
                USE_SOURCE_PERMISSIONS
            )
        endif()
    else()
        message(STATUS "${PROJECT_3RDPARTY_ROOT_DIR}/libuv")
        execute_process(
            COMMAND "./gyp_uv.py" "-f" "make"
            COMMAND make -C out
            WORKING_DIRECTORY "${PROJECT_3RDPARTY_ROOT_DIR}/libuv"
        )
    endif()
    
    set(LIBUV_ROOT "${PROJECT_3RDPARTY_ROOT_DIR}/libuv")
    find_package(Libuv)
endif()

if(NOT LIBUV_FOUND)
    message(FATAL_ERROR "libuv is required.")
endif()

include_directories(${LIBUV_INCLUDE_DIRS})
link_directories(${LIBUV_LIBRARIES})
