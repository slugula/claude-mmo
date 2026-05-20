cmake_minimum_required(VERSION 3.25)

project(osrs-prototype-client
  LANGUAGES CXX C
  DESCRIPTION "Native OpenGL 4.6 client + level editor for the OSRS-inspired MMO prototype"
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

# cgltf is a single-header library. The vcpkg port installs just the header
# (no CMake config), so locate it directly and add as an include directory.
find_path(CGLTF_INCLUDE_DIR    cgltf.h     REQUIRED)
# miniaudio ships the same way — header only.
find_path(MINIAUDIO_INCLUDE_DIR miniaudio.h REQUIRED)
# stb — header only (stb_image.h).  WaterRenderer.cpp defines STB_IMAGE_IMPLEMENTATION.
find_path(STB_INCLUDE_DIR      stb_image.h REQUIRED)

# ---- Shared rendering / world / camera static library ----------------------
#
# Both client-native and level-editor link this. It contains everything that
# does NOT depend on networking (ixwebsocket) or the game client UI.

add_library(game_core STATIC
  src/app/Window.cpp
  src/audio/AudioEngine.cpp
  src/camera/GameCamera.cpp
  src/input/Picker.cpp
  src/render/GlDebug.cpp
  src/render/Mesh.cpp
  src/render/MsaaFramebuffer.cpp
  src/render/Shader.cpp
  src/render/ShadowMap.cpp
  src/world/EntityRenderer.cpp
  src/world/GltfLoader.cpp
  src/world/MapGenerator.cpp
  src/world/ObstacleSystem.cpp
  src/world/SkinnedMesh.cpp
  src/world/TerrainBuilder.cpp
  src/world/WaterMesh.cpp
  src/world/WaterRenderer.cpp
)

target_include_directories(game_core PUBLIC
  src
  ${CGLTF_INCLUDE_DIR}
  ${MINIAUDIO_INCLUDE_DIR}
  ${STB_INCLUDE_DIR}
)

target_link_libraries(game_core PUBLIC
  glfw
  glad::glad
  glm::glm
  glaze::glaze
  imgui::imgui
)

if (MSVC)
  target_compile_options(game_core PRIVATE
    /W4 /permissive- /Zc:preprocessor
    $<$<CONFIG:Release>:/O2>
  )
endif()

# ---- Game client executable ------------------------------------------------

add_executable(client-native
  src/main.cpp
  src/app/App.cpp
  src/net/NetworkClient.cpp
  src/ui/Panels.cpp
  src/ui/WorldOverlays.cpp
)

target_link_libraries(client-native PRIVATE
  game_core
  ixwebsocket::ixwebsocket
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

add_custom_command(TARGET client-native POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:client-native>/shaders
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders
    $<TARGET_FILE_DIR:client-native>/shaders
  COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:client-native>/assets
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/assets
    $<TARGET_FILE_DIR:client-native>/assets
  COMMENT "Copying shaders + assets to client-native build output"
)

# ---- Level editor executable -----------------------------------------------

add_executable(level-editor
  src/editor/main.cpp
  src/editor/EditorApp.cpp
  src/editor/EntityClient.cpp
  src/editor/MinimapRenderer.cpp
  src/editor/UndoStack.cpp
)

target_link_libraries(level-editor PRIVATE game_core)

if (MSVC)
  target_compile_options(level-editor PRIVATE
    /W4 /permissive- /Zc:preprocessor
    $<$<CONFIG:Release>:/O2>
  )
  set_target_properties(level-editor PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:level-editor>"
  )
endif()

add_custom_command(TARGET level-editor POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:level-editor>/shaders
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders
    $<TARGET_FILE_DIR:level-editor>/shaders
  COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:level-editor>/assets
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/assets
    $<TARGET_FILE_DIR:level-editor>/assets
  COMMENT "Copying shaders + assets to level-editor build output"
)
