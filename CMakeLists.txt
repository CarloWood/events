cmake_minimum_required( VERSION 3.12...3.16 )

project( events
         LANGUAGES CXX
         DESCRIPTION "Event manager" )

include( AICxxProject )

#==============================================================================
# BUILD PROJECT
#

# This project is an OBJECT-library, used by other git submodules and the main project.
add_library( events_ObjLib OBJECT )

# Set SOURCE_DIR to the current directory.
set_property(
    TARGET     events_ObjLib
    PROPERTY   SOURCE_DIR       "${CMAKE_CURRENT_SOURCE_DIR}"
)

# The list of source files.
target_sources( events_ObjLib
    PRIVATE
        "Events.cxx"
        
        "Events.h"
)

# Required include search-paths.
get_target_property( CWDS_INTERFACE_INCLUDE_DIRECTORIES AICxx::cwds INTERFACE_INCLUDE_DIRECTORIES )
target_include_directories( events_ObjLib
    PUBLIC
        "${CWDS_INTERFACE_INCLUDE_DIRECTORIES}" # For sys.h and debug.h.
)

# Require support for C++17.
target_compile_features( events_ObjLib
    PUBLIC cxx_std_17
)

# Set link dependencies.
target_link_libraries( events_ObjLib
    PUBLIC
        Threads::Threads
)

if ( OptionEnableLibcwd )
  target_link_libraries( events_ObjLib
      PUBLIC
          PkgConfig::Libcwd_r
)
endif ()

# Create an ALIAS target.
add_library( AICxx::events ALIAS events_ObjLib )

# Prepend this object library to the list.
set( AICXX_OBJECTS_LIST AICxx::events ${AICXX_OBJECTS_LIST} CACHE INTERNAL "List of OBJECT libaries that this project uses." )

#==============================================================================
# Tests
#

add_executable( test1 test1.cxx )
target_link_libraries( test1 PRIVATE AICxx::events AICxx::utils AICxx::cwds )

add_executable( test2 test2.cxx )
target_link_libraries( test2 PRIVATE AICxx::events AICxx::utils AICxx::cwds )

add_executable( test3 test3.cxx )
target_link_libraries( test3 PRIVATE AICxx::events AICxx::utils AICxx::cwds )

add_executable( test4 test4.cxx )
target_link_libraries( test4 PRIVATE AICxx::events AICxx::utils AICxx::cwds )
