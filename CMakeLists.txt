cmake_minimum_required(VERSION 3.25)
project(ShadowRecomp C CXX)
# Windows (build.cmd) and Linux (build.sh) build from this one file; see PORTAGE_LINUX.md for the Linux port.
if(WIN32)
  enable_language(RC)   # runtime/shadow.rc: icon + launcher art
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# --- Aurora (GX renderer, window, input) --------------------------------------------------
set(AURORA_ENABLE_DVD OFF CACHE BOOL "" FORCE)
set(AURORA_ENABLE_CARD OFF CACHE BOOL "" FORCE)
set(AURORA_ENABLE_THP OFF CACHE BOOL "" FORCE)
set(AURORA_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(AURORA_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
if(WIN32)
  file(TO_CMAKE_PATH "$ENV{USERPROFILE}/miniconda3" _conda)   # keep a user conda out of the package search
  set(CMAKE_IGNORE_PREFIX_PATH "${_conda}" CACHE STRING "" FORCE)
endif()
add_subdirectory(third_party/aurora ${CMAKE_BINARY_DIR}/aurora EXCLUDE_FROM_ALL)

if(NOT EXISTS ${CMAKE_CURRENT_LIST_DIR}/generated/files.cmake)
  message(FATAL_ERROR "generated/ is missing: it is made from your own disc. Unpack the disc "
                      "(python tools/game_import/gameimport.py <disc> ../Shadow), then run: python tools/gen.py")
endif()
include(generated/files.cmake)
file(GLOB RT_SOURCES ${CMAKE_CURRENT_LIST_DIR}/runtime/*.cpp)
# The exe icon and launcher art (runtime/shadow.ico, runtime/assets/*.png) come from the game, so they are not in git:
# a fresh clone builds with the neutral placeholders instead. An existing file is never replaced.
foreach(pair "shadow.ico=icon.ico" "assets/emblem.png=emblem.png" "assets/hero.png=hero.png")
  string(REPLACE "=" ";" pair "${pair}")
  list(GET pair 0 dst)
  list(GET pair 1 src)
  if(NOT EXISTS ${CMAKE_CURRENT_LIST_DIR}/runtime/${dst})
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/runtime/assets)
    file(COPY_FILE ${CMAKE_CURRENT_LIST_DIR}/tools/release/placeholder/${src} ${CMAKE_CURRENT_LIST_DIR}/runtime/${dst})
  endif()
endforeach()
# WIN32: a normal Windows program (no console window when double-clicked); runtime/shadow.rc embeds the icon.
# #137 build number: version (VERSION file) + configure time + short git hash -> one generated source file
set(SHADOW_VERSION "dev")
if(EXISTS ${CMAKE_CURRENT_LIST_DIR}/VERSION)
  file(READ ${CMAKE_CURRENT_LIST_DIR}/VERSION SHADOW_VERSION)
  string(STRIP "${SHADOW_VERSION}" SHADOW_VERSION)
endif()
string(TIMESTAMP SHADOW_BUILD_DATE "%Y-%m-%d %H:%M")
execute_process(COMMAND git rev-parse --short HEAD WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
                OUTPUT_VARIABLE SHADOW_GIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT SHADOW_GIT_HASH)
  set(SHADOW_GIT_HASH "nogit")
else()
  execute_process(COMMAND git status --porcelain --untracked-files=no WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
                  OUTPUT_VARIABLE SHADOW_GIT_DIRTY OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  if(SHADOW_GIT_DIRTY)
    set(SHADOW_GIT_HASH "${SHADOW_GIT_HASH}+")
  endif()
endif()
configure_file(${CMAKE_CURRENT_LIST_DIR}/runtime/build_info.cpp.in ${CMAKE_BINARY_DIR}/build_info.cpp @ONLY)
if(WIN32)
  add_executable(shadow WIN32 ${RT_SOURCES} ${GEN_SOURCES} runtime/shadow.rc ${CMAKE_BINARY_DIR}/build_info.cpp)
  target_compile_options(shadow PRIVATE -O2 -gcodeview -gline-tables-only -fno-strict-aliasing -fno-asynchronous-unwind-tables -w)
  target_link_options(shadow PRIVATE -Wl,/STACK:268435456 -Wl,/DEBUG -Wl,/ENTRY:mainCRTStartup)
  target_link_libraries(shadow PRIVATE dwmapi dbghelp winmm)
else()
  # Linux. The launcher art that Windows keeps as resources (shadow.rc 201 / 202) is compiled in with .incbin and
  # read through plat::resource(); the deep game stack comes from plat::run_on_big_stack(), not from the linker.
  set(SHADOW_EMBLEM_PNG ${CMAKE_CURRENT_LIST_DIR}/runtime/assets/emblem.png)
  set(SHADOW_HERO_PNG ${CMAKE_CURRENT_LIST_DIR}/runtime/assets/hero.png)
  configure_file(${CMAKE_CURRENT_LIST_DIR}/runtime/platform_resources.cpp.in ${CMAKE_BINARY_DIR}/platform_resources.cpp @ONLY)
  set_property(SOURCE ${CMAKE_BINARY_DIR}/platform_resources.cpp APPEND PROPERTY OBJECT_DEPENDS "${SHADOW_EMBLEM_PNG};${SHADOW_HERO_PNG}")
  add_executable(shadow ${RT_SOURCES} ${GEN_SOURCES} ${CMAKE_BINARY_DIR}/build_info.cpp ${CMAKE_BINARY_DIR}/platform_resources.cpp)
  # -g1: function names in a crash backtrace / perf, without the size of full debug info for 34,000 functions
  target_compile_options(shadow PRIVATE -O2 -g1 -fno-strict-aliasing -w)
  # a plain setjmp/longjmp is what the guest exception code needs (no fortify checks on the fiber stacks)
  target_compile_options(shadow PRIVATE -U_FORTIFY_SOURCE)
  find_package(Threads REQUIRED)
  target_link_libraries(shadow PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
  # everything the game needs next to the binary resolves from its own folder (shared SDL3 / Dawn, when chosen)
  set_target_properties(shadow PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
endif()
if(WIN32)
  target_include_directories(shadow PRIVATE runtime generated third_party/aurora/lib third_party/aurora/include)
else()
  # runtime/ and generated/ only for #include "...": runtime/sched.h must not hide the C library's <sched.h>
  # (which <pthread.h> includes)
  target_compile_options(shadow PRIVATE "SHELL:-iquote ${CMAKE_CURRENT_LIST_DIR}/runtime" "SHELL:-iquote ${CMAKE_CURRENT_LIST_DIR}/generated")
  target_include_directories(shadow PRIVATE third_party/aurora/lib third_party/aurora/include)
endif()
target_compile_definitions(shadow PRIVATE WEBGPU_DAWN)
target_link_libraries(shadow PRIVATE aurora_gx aurora_core aurora_vi
    absl::btree absl::flat_hash_map Tracy::TracyClient dawn::webgpu_dawn sqlite3 PNG::PNG)
