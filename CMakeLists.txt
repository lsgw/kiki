cmake_minimum_required( VERSION 2.8.0 )
project(longnet)

#SET (CMAKE_VERBOSE_MAKEFILE ON)
SET (CMAKE_BUILD_TYPE "Debug")
#SET (CMAKE_BUILD_TYPE "Release")

IF (APPLE)
	SET (CMAKE_MACOSX_RPATH 0)
	SET (CMAKE_SHARED_LINKER_FLAGS  "-dynamiclib -Wl,-undefined,dynamic_lookup")
ENDIF(APPLE)

add_compile_options(-fPIC)

SET (CMAKE_C_COMPILER        "gcc")
SET (CMAKE_C_FLAGS           "-std=c99")
SET (CMAKE_C_FLAGS_DEBUG     "-O0 -g -Wall")
SET (CMAKE_C_FLAGS_RELEASE   "-O2 -g -Wall")

SET (CMAKE_CXX_COMPILER      "g++")
SET (CMAKE_CXX_FLAGS         "-std=c++1y")
SET (CMAKE_CXX_FLAGS_DEBUG   "-O0 -g -Wall")
SET (CMAKE_CXX_FLAGS_RELEASE "-O2 -g -Wall")

SET (EXECUTABLE_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/bin/)
SET (LIBRARY_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/lib/)

# ${CMAKE_SOURCE_DIR}         cmake 的根目录
# ${CMAKE_CURRENT_SOURCE_DIR} 当前文件 cmake 目录
# ${PROJECT_BINARY_DIR}       build 目录
include_directories(
	${CMAKE_SOURCE_DIR}/src/core/base/
	${CMAKE_SOURCE_DIR}/src/core/

	${CMAKE_SOURCE_DIR}/src/service/
	${CMAKE_SOURCE_DIR}/3rd/
	${CMAKE_SOURCE_DIR}/3rd/lua/
	${CMAKE_SOURCE_DIR}/3rd/udns-0.2/
	${CMAKE_SOURCE_DIR}/3rd/c-ares/
	${CMAKE_SOURCE_DIR}/3rd/http-parser/
)

add_subdirectory(3rd)
add_subdirectory(src)
add_subdirectory(lualib)
add_subdirectory(test/testexe/)