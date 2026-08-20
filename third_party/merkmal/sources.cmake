# The pinned merkmal core's translation units, as a list.
#
# A list rather than a library target, because cognator compiles these straight
# into libcognator: merkmal appears nowhere in include/cognator.h, so it is an
# implementation detail, and a static libcognator that carries an unresolved
# dependency on a libmerkmal nobody installed is not one. It also makes the
# CMake build and setup.py compile the same eight files with the same flags,
# which is the property that keeps the C CLI and the Python wrapper clustering
# alike.
#
# Upstream's list, in upstream's order. Recheck it against the commit named in
# PINNED when moving the pin.

set(MERKMAL_PINNED_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/src/feature_set.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/geometry.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/registry.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/status.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/string_list.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/system.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/unicode.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/generated/builtin_data.c"
)
set(MERKMAL_PINNED_INCLUDE_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/src"
)
