cmake_minimum_required(VERSION 3.12...3.29)

project(events
  LANGUAGES CXX
  DESCRIPTION "Event manager"
)

include(AICxxProject)

#==============================================================================
# BUILD PROJECT
#

# This project is an OBJECT-library, used by other git submodules and the main project.
add_library(events_ObjLib OBJECT)

# The list of source files.
target_sources(events_ObjLib
  PRIVATE
   "Events.cxx"
        
   "Events.h"
)

# Required include search-paths.
get_target_property(CWDS_INTERFACE_INCLUDE_DIRECTORIES AICxx::cwds INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(events_ObjLib
  PUBLIC
    "${CWDS_INTERFACE_INCLUDE_DIRECTORIES}" # For sys.h and debug.h.
)

# Require support for C++17.
target_compile_features(events_ObjLib
  PUBLIC cxx_std_17
)

# Set link dependencies.
target_link_libraries(events_ObjLib
  PUBLIC
    AICxx::utils
)

# Create an ALIAS target.
add_library(AICxx::events ALIAS events_ObjLib)

# Prepend this object library to the list.
set(AICXX_OBJECTS_LIST AICxx::events ${AICXX_OBJECTS_LIST} CACHE INTERNAL "List of OBJECT libaries that this project uses.")

#==============================================================================
# Tests
#

add_executable(test1 EXCLUDE_FROM_ALL test1.cxx)
target_link_libraries(test1 PRIVATE AICxx::events AICxx::utils AICxx::cwds)

add_executable(test2 EXCLUDE_FROM_ALL test2.cxx)
target_link_libraries(test2 PRIVATE AICxx::events AICxx::utils AICxx::cwds)

add_executable(test3 EXCLUDE_FROM_ALL test3.cxx)
target_link_libraries(test3 PRIVATE AICxx::events AICxx::utils AICxx::cwds)

add_executable(test4 EXCLUDE_FROM_ALL test4.cxx)
target_link_libraries(test4 PRIVATE AICxx::events AICxx::utils AICxx::cwds)
