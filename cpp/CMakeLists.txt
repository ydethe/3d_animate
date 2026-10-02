cmake_minimum_required(VERSION 3.16)
project(3d_animate_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(BUILD_INTERACTIVE "Build the interactive GLFW viewer (needs libglfw3-dev)" ON)

# --- Dependencies ---------------------------------------------------------- #
find_package(assimp REQUIRED)

find_library(EGL_LIBRARY NAMES EGL REQUIRED)
find_library(GL_LIBRARY NAMES GL REQUIRED)

set(SOURCES
  src/main.cpp
  src/app.cpp
  src/color.cpp
  src/stl.cpp
  src/mesh.cpp
  src/assimp_load.cpp
  src/gl.cpp
  src/glmesh.cpp
  src/renderer.cpp
  src/egl_context.cpp
  src/exporter.cpp
  src/glfw_context.cpp
)

add_executable(3d_animate ${SOURCES})

target_include_directories(3d_animate PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

# Assimp exports either an imported target or plain variables depending on
# version; handle both.
if(TARGET assimp::assimp)
  target_link_libraries(3d_animate PRIVATE assimp::assimp)
else()
  target_include_directories(3d_animate PRIVATE ${ASSIMP_INCLUDE_DIRS})
  target_link_libraries(3d_animate PRIVATE ${ASSIMP_LIBRARIES})
endif()

target_link_libraries(3d_animate PRIVATE ${EGL_LIBRARY} ${GL_LIBRARY} dl)

if(BUILD_INTERACTIVE)
  find_package(glfw3 QUIET)
  if(TARGET glfw)
    target_compile_definitions(3d_animate PRIVATE ANIMATE_BUILD_INTERACTIVE)
    target_link_libraries(3d_animate PRIVATE glfw)
  else()
    find_library(GLFW_LIBRARY NAMES glfw glfw3)
    if(GLFW_LIBRARY)
      target_compile_definitions(3d_animate PRIVATE ANIMATE_BUILD_INTERACTIVE)
      target_link_libraries(3d_animate PRIVATE ${GLFW_LIBRARY})
    else()
      message(WARNING "GLFW not found; building export-only (no interactive viewer).")
    endif()
  endif()
endif()
