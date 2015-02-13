#.rst:
# FindLibuv
# --------
#
# Find the native libuv includes and library.
#
# IMPORTED Targets
# ^^^^^^^^^^^^^^^^
#
#
# Result Variables
# ^^^^^^^^^^^^^^^^
#
# This module defines the following variables:
#
# ::
#
#   LIBUV_INCLUDE_DIRS   - where to find uv.h, etc.
#   LIBUV_LIBRARIES      - List of libraries when using libuv.
#   LIBUV_FOUND          - True if libuv found.
#
# ::
#
#
# Hints
# ^^^^^
#
# A user may set ``LIBUV_ROOT`` to a libuv installation root to tell this
# module where to look.

#=============================================================================
# Copyright 2014-2015 OWenT.
#
# Distributed under the OSI-approved BSD License (the "License");
# see accompanying file Copyright.txt for details.
#
# This software is distributed WITHOUT ANY WARRANTY; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# See the License for more information.
#=============================================================================
# (To distribute this file outside of CMake, substitute the full
#  License text for the above reference.)

set(_LIBUV_SEARCHES)

# Search LIBUV_ROOT first if it is set.
if(LIBUV_ROOT)
  set(_LIBUV_SEARCH_ROOT PATHS ${LIBUV_ROOT} NO_DEFAULT_PATH)
  list(APPEND _LIBUV_SEARCHES _LIBUV_SEARCH_ROOT)
endif()

# Normal search.
set(_LIBUV_SEARCH_NORMAL
  PATHS "[HKEY_LOCAL_MACHINE\\SOFTWARE\\GnuWin32\\libuv;InstallPath]"
        "$ENV{PROGRAMFILES}/libuv"
  )
list(APPEND _LIBUV_SEARCHES _LIBUV_SEARCH_NORMAL)

set(LIBUV_NAMES uv libuv)

# Try each search configuration.
foreach(search ${_LIBUV_SEARCHES})
  find_path(LIBUV_INCLUDE_DIR NAMES uv.h        ${${search}} PATH_SUFFIXES include)
  find_library(LIBUV_LIBRARY  NAMES ${LIBUV_NAMES} ${${search}} PATH_SUFFIXES lib)
endforeach()

mark_as_advanced(LIBUV_LIBRARY LIBUV_INCLUDE_DIR)

# handle the QUIETLY and REQUIRED arguments and set LIBUV_FOUND to TRUE if
# all listed variables are TRUE
include("FindPackageHandleStandardArgs")
FIND_PACKAGE_HANDLE_STANDARD_ARGS(LIBUV REQUIRED_VARS LIBUV_LIBRARY LIBUV_INCLUDE_DIR)

if(LIBUV_FOUND)
    set(LIBUV_INCLUDE_DIRS ${LIBUV_INCLUDE_DIR})
    set(LIBUV_LIBRARIES ${LIBUV_LIBRARY})

    if(NOT TARGET LIBUV::LIBUV)
      add_library(LIBUV::LIBUV UNKNOWN IMPORTED)
      set_target_properties(LIBUV::LIBUV PROPERTIES
        IMPORTED_LOCATION "${LIBUV_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${LIBUV_INCLUDE_DIRS}")
    endif()
endif()
