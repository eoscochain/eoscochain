# Override default CMAKE_FIND_LIBRARY_SUFFIXES
if (CPPKAFKA_SHARED_LIB)
    set(CPPKAFKA_SUFFIX so)
else()
    set(CPPKAFKA_SUFFIX a)
endif()
message(STATUS "Cppkafka finding .${CPPKAFKA_SUFFIX} library")

FIND_PATH(
        CPPKAFKA_INCLUDE_DIR cppkafka.h
        PATH "/usr/local"
        PATH_SUFFIXES "" "cppkafka")
MARK_AS_ADVANCED(CPPKAFKA_INCLUDE_DIR)

SET(CPPKAFKA_INCLUDE_DIR ${CPPKAFKA_INCLUDE_DIR})

FIND_LIBRARY(
        CPPKAFKA_LIBRARY
        NAMES cppkafka.${CPPKAFKA_SUFFIX} libcppkafka.${CPPKAFKA_SUFFIX}
        HINTS ${CPPKAFKA_INCLUDE_DIR}/..
        PATH_SUFFIXES lib${LIB_SUFFIX})
MARK_AS_ADVANCED(CPPKAFKA_LIBRARY)

SET(CPPKAFKA_LIBRARY ${CPPKAFKA_LIBRARY})
message(STATUS "Cppkafka found ${CPPKAFKA_LIBRARY}")

include(FindPackageHandleStandardArgs)
SET(_CPPKAFKA_REQUIRED_VARS CPPKAFKA_INCLUDE_DIR CPPKAFKA_LIBRARY)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(Cppkafka DEFAULT_MSG ${_CPPKAFKA_REQUIRED_VARS})
