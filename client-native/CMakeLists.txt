cmake_minimum_required(VERSION 3.25)

project(osrs-prototype-client
  LANGUAGES CXX C
  DESCRIPTION "Native OpenGL 4.6 client for the OSRS-inspired MMO prototype"
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# vcpkg-supplied dependencies
find_package(glfw3       CONFIG REQUIRED)
find_package(glad        CONFIG REQUIRED)
find_package(glm         CONFIG REQUIRED)
find_package(glaze       CONFIG REQUIRED)
find_package(ixwebsocket CONFIG REQUIRED)
find_package(imgui       CONFIG REQUIRED)

# ---- Main executable --------------------------------------------------------

add_executable(client-native
  src/main.cpp
  src/app/App.cpp
  src/app/Window.cpp
  src/camera/GameCamera.cpp
  src/input/Picker.cpp
  src/net/NetworkClient.cpp
  src/render/GlDebug.cpp
  src/render/Mesh.cpp
  src/render/MsaaFramebuffer.cpp
  src/render/Shader.cpp
  src/world/MapGenerator.cpp
  src/world/ObstacleSystem.cpp
  src/world/TerrainBuilder.cpp
)

target_include_directories(client-native PRIVATE src)

target_link_libraries(client-native PRIVATE
  glfw
  glad::glad
  glm::glm
  glaze::glaze
  ixwebsocket::ixwebsocket
  imgui::imgui
)

# ixwebsocket pulls in mbedtls for TLS; mbedtls's entropy_poll on Windows uses
# BCryptGenRandom from bcrypt.lib (the CNG API). vcpkg should propagate this
# but doesn't always — link it explicitly.
if (WIN32)
  target_link_libraries(client-native PRIVATE bcrypt)
endif()

if (MSVC)
  target_compile_options(client-native PRIVATE
    /W4 /permissive- /Zc:preprocessor
    $<$<CONFIG:Release>:/O2>
  )
  set_target_properties(client-native PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:client-native>"
  )
endif()

# ---- Copy shaders next to the exe -----------------------------------------
#
# Map files are NOT copied: the world is generated procedurally in-process
# (see src/world/MapGenerator.cpp). Map I/O is deferred until after Phase 11.

add_custom_command(TARGET client-native POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:client-native>/shaders
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders
    $<TARGET_FILE_DIR:client-native>/shaders
  COMMENT "Copying shaders to build output"
)
