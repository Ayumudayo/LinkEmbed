# Original from https://github.com/brainboxdotcc/DPP/blob/master/cmake/FindDPP.cmake
# Find dpp
#
#  dpp_INCLUDE_DIRS - where to find dpp.h, etc.
#  dpp_LIBRARIES    - List of libraries when using dpp.
#  dpp_FOUND        - True if dpp found.

if(dpp_INCLUDE_DIRS)
    # Already in cache, be silent
    set(dpp_FIND_QUIETLY TRUE)
endif()

find_path(dpp_INCLUDE_DIRS NAMES dpp/dpp.h)
find_library(dpp_LIBRARIES NAMES dpp)

# handle the QUIETLY and REQUIRED arguments and set dpp_FOUND to TRUE if
# all listed variables are TRUE
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(dpp DEFAULT_MSG dpp_LIBRARIES dpp_INCLUDE_DIRS)

mark_as_advanced(dpp_INCLUDE_DIRS dpp_LIBRARIES)