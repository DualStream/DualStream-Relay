cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

find_package(CURL REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE CURL::libcurl)

if(ENABLE_QT)
  find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.c
    src/relay-output.c
    src/relay-auth.cpp
    src/relay-destinations.cpp
    src/relay-pairing.cpp
    src/relay-status.cpp
    src/vertical-canvas.cpp
    src/vertical-scenes.cpp
    src/vertical-output.cpp
    src/ui/dsr-source-icon.cpp
    src/ui/dsr-ui-common.cpp
    src/ui/dsr-widgets.cpp
    src/ui/relay-dock.cpp
    src/ui/relay-dock-build.cpp
    src/ui/relay-dock-rows.cpp
    src/ui/relay-dock-state.cpp
    src/ui/relay-dock-routing.cpp
    src/ui/relay-dock-text.cpp
    src/ui/relay-module.cpp
    src/ui/vertical-common.cpp
    src/ui/vertical-preview.cpp
    src/ui/vertical-preview-draw.cpp
    src/ui/vertical-preview-edit.cpp
    src/ui/vertical-preview-menu.cpp
    src/ui/vertical-dock.cpp
    src/ui/vertical-sources-dock.cpp
    src/ui/destination-dialog.cpp
    src/ui/settings-dialog.cpp
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
