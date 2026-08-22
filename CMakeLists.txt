cmake_minimum_required(VERSION 3.20)
project(voxmine CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(VK $ENV{VULKAN_SDK})
if(NOT VK)
  set(VK "C:/VulkanSDK/1.4.357.0")
endif()

add_executable(voxmine
  src/main.cpp
  src/window.cpp
  src/vk.cpp
  src/png.cpp
  src/atlas.cpp
  src/noise.cpp
  src/generator.cpp
  src/world.cpp
  src/mesher.cpp
  src/raycast.cpp
  src/camera.cpp
  src/player.cpp
  src/renderer.cpp
  src/volk_impl.c
)

target_include_directories(voxmine PRIVATE src ${VK}/Include ${VK}/Include/volk)
target_compile_definitions(voxmine PRIVATE VK_NO_PROTOTYPES)
target_compile_options(voxmine PRIVATE -O2)
target_link_libraries(voxmine PRIVATE user32 gdi32 ole32 oleaut32)

# shaders
set(SPV ${CMAKE_CURRENT_BINARY_DIR}/shaders)
set(GLSLC ${VK}/Bin/glslc.exe)
set(SHADER_FILES
  terrain.vert terrain.frag
  water.vert water.frag
  ui.vert ui.frag
  sky.vert sky.frag
)
set(SPV_TARGETS "")
foreach(s ${SHADER_FILES})
  add_custom_command(
    OUTPUT ${SPV}/${s}.spv
    COMMAND ${GLSLC} -O -o ${SPV}/${s}.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${s}
    DEPENDS shaders/${s}
    COMMENT "glslc ${s}"
  )
  list(APPEND SPV_TARGETS ${SPV}/${s}.spv)
endforeach()
add_custom_target(shaders ALL DEPENDS ${SPV_TARGETS})
add_dependencies(voxmine shaders)

# copy assets so the exe finds them next to itself (asset path is relative: assets/block)
add_custom_command(TARGET voxmine POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_CURRENT_SOURCE_DIR}/assets ${CMAKE_CURRENT_BINARY_DIR}/assets
)
