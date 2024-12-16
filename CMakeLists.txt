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

FetchContent_Declare(
    tree_sitter
    QUIET
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter
    GIT_TAG v0.24.5
    SOURCE_SUBDIR lib
)

FetchContent_Declare(
    tree_sitter_lua
    QUIET
    GIT_REPOSITORY https://github.com/tree-sitter-grammars/tree-sitter-lua
    GIT_TAG 34e60e7
    SOURCE_SUBDIR nope
)

FetchContent_MakeAvailable(cli11_proj)
FetchContent_MakeAvailable(tree_sitter)
FetchContent_MakeAvailable(tree_sitter_lua)

find_package(Lua REQUIRED)
find_package(ICU 74.1 COMPONENTS uc REQUIRED)

find_package(LLVM REQUIRED CONFIG)

set(CMAKE_CXX_STANDARD 23)

# Core program
add_executable(igk scanner.cc)
set_target_properties(igk PROPERTIES ENABLE_EXPORTS true)
target_include_directories(igk PRIVATE ${LUA_INCLUDE_DIR} "third_party/cli11" "third_party/sol3")
target_link_libraries(igk PRIVATE ICU::uc fmt::fmt ${LUA_LIBRARIES} CLI11::CLI11)

# Clang plugin
add_library(igk-clang SHARED clang.cc)
target_include_directories(igk-clang PRIVATE ${LUA_INCLUDE_DIR} "third_party/sol3" ${LLVM_INCLUDE_DIRS} ${LLVM_SOURCE_DIR}/clang/include)
target_link_directories(igk-clang PRIVATE ${LLVM_LIBRARY_DIRS})
target_link_libraries(igk-clang PRIVATE fmt::fmt ${LUA_LIBRARIES} clang)
target_link_options(igk-clang PRIVATE LINKER:-undefined,dynamic_lookup)

# TreeSitter plugin
add_library(igk-treesitter SHARED
	treesitter.cc
	"${tree_sitter_lua_SOURCE_DIR}/src/parser.c"
	"${tree_sitter_lua_SOURCE_DIR}/src/scanner.c"
	"third_party/tree-sitter-rego/parser.c"
	"${tree_sitter_SOURCE_DIR}/lib/src/lib.c")
target_include_directories(igk-treesitter PRIVATE ${LUA_INCLUDE_DIR} "third_party/sol3")
target_link_libraries(igk-treesitter PRIVATE fmt::fmt ${LUA_LIBRARIES})
target_include_directories(igk-treesitter PRIVATE "${tree_sitter_SOURCE_DIR}/lib/include")

enable_testing()
add_subdirectory(Tests)
