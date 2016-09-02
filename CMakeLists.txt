cmake_minimum_required (VERSION 2.6)

set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_COLOR_MAKEFILE   ON)

if (WIN32)
	# Remove 'lib' prefix for shared libraries on Windows
	set(CMAKE_SHARED_LIBRARY_PREFIX "")
else ()
	# Use C++14 in GCC
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -fexceptions -march=native -mtune=native")
endif ()


add_definitions(-DCHANGE_G3LOG_DEBUG_TO_DBUG=1)
add_definitions(-DDISABLE_FATAL_SIGNALHANDLING=1)
add_definitions(-DDISABLE_VECTORED_EXCEPTIONHANDLING=1)
add_definitions(-DASIO_STANDALONE=1)
add_definitions(-DASIO_NO_DEPRECATED=1)
add_definitions(-DASIO_HEADER_ONLY=1)

include_directories(sphynx)

# Not ideal but fine if I remember to always rebuild:
file(GLOB SphynxSourceFiles
    "sphynx/*"
    "sphynx/zstd/*"
    "sphynx/g3log/*"
)

if (WIN32)
	get_filename_component(OtherCrashHandler "sphynx/g3log/crashhandler_unix.cpp" ABSOLUTE)
	list(REMOVE_ITEM SphynxSourceFiles ${OtherCrashHandler})
else ()
	get_filename_component(OtherCrashHandler "sphynx/g3log/crashhandler_windows.cpp" ABSOLUTE)
	list(REMOVE_ITEM SphynxSourceFiles ${OtherCrashHandler})
	get_filename_component(OtherStackTrace "sphynx/g3log/stacktrace_windows.cpp" ABSOLUTE)
	list(REMOVE_ITEM SphynxSourceFiles ${OtherStackTrace})
endif ()

file(GLOB SharedDemoSourceFiles
	"sphynxdemo/DemoProtocol.cpp"
	"sphynxdemo/DemoProtocol.h"
	"sphynxdemo/NeighborTracker.h"
)

get_cmake_property(_variableNames VARIABLES)
foreach (_variableName ${_variableNames})
    message(STATUS "${_variableName}=${${_variableName}}")
endforeach()

add_library(SphynxNetworking STATIC ${SphynxSourceFiles})

project (Server)
add_executable(UDPServer "sphynxdemo/UDPServerMain.cpp" ${SharedDemoSourceFiles})
target_link_libraries(UDPServer SphynxNetworking)

find_package (Threads)
if (WIN32)
else ()
target_link_libraries(UDPServer ${CMAKE_THREAD_LIBS_INIT})
endif ()

project (Client)
add_executable(UDPClient "sphynxdemo/UDPClientMain.cpp" "sphynxdemo/UDPClientLib.h" "sphynxdemo/UDPClientLib.cpp" ${SharedDemoSourceFiles})
target_link_libraries(UDPClient SphynxNetworking)

find_package (Threads)
if (WIN32)
else ()
target_link_libraries(UDPClient ${CMAKE_THREAD_LIBS_INIT})
endif ()
