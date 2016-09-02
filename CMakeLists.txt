cmake_minimum_required (VERSION 2.6)

add_definitions(-DDISABLE_FATAL_SIGNALHANDLING=1)
add_definitions(-DDISABLE_VECTORED_EXCEPTIONHANDLING=1)

include_directories(sphynx)

# Not ideal but fine if I remember to always rebuild:
file(GLOB SharedSourceFiles
	"sphynxdemo/DemoProtocol.cpp"
    "sphynx/*"
    "sphynx/zstd/lib/dictBuilder/*"
    "sphynx/zstd/lib/decompress/*"
    "sphynx/zstd/lib/compress/*"
    "sphynx/zstd/lib/common/*"
    "sphynx/g3log/*"
)

IF (WIN32)
	get_filename_component(OtherCrashHandler "sphynx/g3log/crashhandler_unix.cpp" ABSOLUTE)
	list(REMOVE_ITEM SharedSourceFiles ${OtherCrashHandler})
ELSE()
	get_filename_component(OtherCrashHandler "sphynx/g3log/crashhandler_windows.cpp" ABSOLUTE)
	list(REMOVE_ITEM SharedSourceFiles ${OtherCrashHandler})
ENDIF()

get_cmake_property(_variableNames VARIABLES)
foreach (_variableName ${_variableNames})
    message(STATUS "${_variableName}=${${_variableName}}")
endforeach()

project (Server)
add_executable(UDPServer "sphynxdemo/UDPServerMain.cpp" ${SharedSourceFiles})

project (Client)
add_executable(UDPClient "sphynxdemo/UDPClientMain.cpp" ${SharedSourceFiles})
