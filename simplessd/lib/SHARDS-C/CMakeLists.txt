cmake_minimum_required(VERSION 3.10)
project(shards)

# Set output directory
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})


include_directories(
  ${PROJECT_SOURCE_DIR}
  /usr/include/glib-2.0
  /usr/lib/x86_64-linux-gnu/glib-2.0/include
)

# Platform specific settings
if (MSVC)
  add_definitions(-D_CRT_SECURE_NO_WARNINGS)
  add_compile_options(/wd4819)  # Surpress unicode warning
  add_compile_options(/wd4244)  # Surpress conversion warning
  add_compile_options(/wd4267)  # Surpress conversion warning
  add_compile_options(/wd4305)  # Surpress truncation warning
  add_compile_options(/wd4800)  # Surpress int -> boolean waring
else ()
  set(CMAKE_CXX_FLAGS "-g -rdynamic -Wall -Wextra -Werror -Wno-implicit-fallthrough -fstack-protector-all -fbounds-check ${CMAKE_CXX_FLAGS}")

  if (DEBUG_BUILD)
    set(CMAKE_CXX_FLAGS "-O0 ${CMAKE_CXX_FLAGS}")
  else ()
    set(CMAKE_CXX_FLAGS "-O2 ${CMAKE_CXX_FLAGS}")
  endif ()

  if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS "6.0")
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-strict-aliasing")
    endif ()
  endif ()
endif ()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(shards STATIC shards_utils.cc SHARDS.cc)

target_link_libraries(shards /usr/lib/x86_64-linux-gnu/libglib-2.0.so)