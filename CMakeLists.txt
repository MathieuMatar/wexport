# Builds the portable core, the command-line pipeline and the unit tests.
# The WinUI app itself is built with ChatKeeper.sln (MSBuild), which compiles
# the same src/Core sources.
cmake_minimum_required(VERSION 3.21)
project(ChatKeeperCore LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SQLite3 REQUIRED)
find_package(nlohmann_json 3.10 REQUIRED)

file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS src/Core/*.cpp)
if(NOT WIN32)
  list(FILTER CORE_SOURCES EXCLUDE REGEX ".*/Wpd[^/]*\\.cpp$")
endif()

add_library(ChatKeeperCore STATIC ${CORE_SOURCES})
target_include_directories(ChatKeeperCore PUBLIC src)
target_link_libraries(ChatKeeperCore PUBLIC SQLite::SQLite3 nlohmann_json::nlohmann_json)
if(WIN32)
  target_compile_definitions(ChatKeeperCore PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(ChatKeeperCore PUBLIC PortableDeviceGuids ole32 oleaut32 propsys)
endif()
if(MSVC)
  target_compile_options(ChatKeeperCore PRIVATE /W4 /utf-8 /permissive-)
else()
  target_compile_options(ChatKeeperCore PRIVATE -Wall -Wextra)
  find_package(Threads REQUIRED)
  target_link_libraries(ChatKeeperCore PUBLIC Threads::Threads)
endif()

add_executable(chatkeeper-cli src/Cli/main.cpp)
target_link_libraries(chatkeeper-cli PRIVATE ChatKeeperCore)
if(MSVC)
  target_compile_options(chatkeeper-cli PRIVATE /utf-8)
endif()

enable_testing()
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS tests/unit/*.cpp)
add_executable(core-tests ${TEST_SOURCES})
target_link_libraries(core-tests PRIVATE ChatKeeperCore)
if(MSVC)
  target_compile_options(core-tests PRIVATE /utf-8)
endif()
add_test(NAME core-tests COMMAND core-tests)
target_compile_definitions(core-tests PRIVATE CK_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
