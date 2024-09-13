cmake_minimum_required(VERSION 3.16)
project(igk)

include(FetchContent)

FetchContent_Declare(
  fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt
  GIT_TAG        e69e5f977d458f2650bb346dadf2ad30c5320281) # 10.2.1
FetchContent_MakeAvailable(fmt)

include(FetchContent)
FetchContent_Declare(
    cli11_proj
    QUIET
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.3.2
)

FetchContent_MakeAvailable(cli11_proj)

find_package(Lua REQUIRED)
find_package(ICU 74.1 COMPONENTS uc REQUIRED)

find_package(LLVM REQUIRED CONFIG)

set(CMAKE_CXX_STANDARD 23)

add_library(igk-clang SHARED clang.cc)
add_executable(igk scanner.cc)
set_target_properties(igk PROPERTIES ENABLE_EXPORTS true)
target_include_directories(igk PRIVATE ${LUA_INCLUDE_DIR} "third_party/cli11" "third_party/sol3")
target_include_directories(igk-clang PRIVATE ${LUA_INCLUDE_DIR} "third_party/sol3" ${LLVM_INCLUDE_DIRS} ${LLVM_SOURCE_DIR}/clang/include)

target_link_directories(igk-clang PRIVATE ${LLVM_LIBRARY_DIRS})
target_link_libraries(igk-clang PRIVATE fmt::fmt ${LUA_LIBRARIES} clang)
target_link_options(igk-clang PRIVATE LINKER:-undefined,dynamic_lookup)

target_link_libraries(igk PRIVATE ICU::uc fmt::fmt ${LUA_LIBRARIES} CLI11::CLI11)

enable_testing()
add_subdirectory(Tests)
