cmake_minimum_required(VERSION 3.20)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/version.json" NLSI_VERSION_JSON)
string(JSON NLSI_PROJECT_VERSION ERROR_VARIABLE NLSI_VERSION_ERROR
    GET "${NLSI_VERSION_JSON}" version)
if(NOT NLSI_VERSION_ERROR STREQUAL "NOTFOUND"
    OR NOT NLSI_PROJECT_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "version.json must contain a numeric X.Y.Z application version")
endif()
project(NLSIExclusiveLogbook VERSION "${NLSI_PROJECT_VERSION}" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/version.json"
)
add_subdirectory(native)
