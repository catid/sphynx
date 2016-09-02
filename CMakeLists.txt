cmake_minimum_required (VERSION 2.6)

set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_COLOR_MAKEFILE   ON)

# Remove 'lib' prefix for shared libraries on Windows
if (WIN32)
  set(CMAKE_SHARED_LIBRARY_PREFIX "")
endif ()

# Use C++14 in GCC
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14")

add_definitions(-DDISABLE_FATAL_SIGNALHANDLING=1)
add_definitions(-DDISABLE_VECTORED_EXCEPTIONHANDLING=1)
add_definitions(-DASIO_STANDALONE=1)
add_definitions(-DASIO_NO_DEPRECATED=1)
add_definitions(-DASIO_HEADER_ONLY=1)

include_directories(sphynx sphynx/asio/include)

# Not ideal but fine if I remember to always rebuild:
file(GLOB SphynxSourceFiles
    "sphynx/*"
    "sphynx/zstd/lib/dictBuilder/*"
    "sphynx/zstd/lib/decompress/*"
    "sphynx/zstd/lib/compress/*"
    "sphynx/zstd/lib/common/*"
    "sphynx/g3log/*"
)

IF (WIN32)
	get_filename_component(OtherCrashHandler "sphynx/g3log/crashhandler_unix.cpp" ABSOLUTE)
	list(REMOVE_ITEM SphynxSourceFiles ${OtherCrashHandler})
ELSE()
	get_filename_component(OtherCrashHandler "sphynx/g3log/crashhandler_windows.cpp" ABSOLUTE)
	list(REMOVE_ITEM SphynxSourceFiles ${OtherCrashHandler})
ENDIF()

file(GLOB SharedDemoSourceFiles
	"sphynxdemo/DemoProtocol.cpp"
)

get_cmake_property(_variableNames VARIABLES)
foreach (_variableName ${_variableNames})
    message(STATUS "${_variableName}=${${_variableName}}")
endforeach()

add_library(SphynxNetworking STATIC ${SphynxSourceFiles})

project (Server)
add_executable(UDPServer "sphynxdemo/UDPServerMain.cpp" ${SharedDemoSourceFiles})
target_link_libraries(UDPServer SphynxNetworking)

project (Client)
add_executable(UDPClient "sphynxdemo/UDPClientMain.cpp" ${SharedDemoSourceFiles})
target_link_libraries(UDPClient SphynxNetworking)
