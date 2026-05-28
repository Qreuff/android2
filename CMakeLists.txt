cmake_minimum_required(VERSION 3.14)
project(GPSNetworkMonitor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")

find_package(Threads REQUIRED)
find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(CURL REQUIRED)
find_package(PNG REQUIRED)

find_path(ZMQ_INCLUDE_DIRS 
    NAMES zmq.hpp 
    PATHS /opt/homebrew/include /usr/local/include
)
find_library(ZMQ_LIBRARIES 
    NAMES zmq 
    PATHS /opt/homebrew/lib /usr/local/lib
)

if(NOT ZMQ_LIBRARIES)
    message(FATAL_ERROR "ZeroMQ не найден! Выполни в терминале: brew install zeromq cppzmq")
endif()

find_path(NLOHMANN_JSON_INCLUDE_DIR
    NAMES nlohmann/json.hpp
    PATHS
        /opt/homebrew/include
        /usr/local/include
        ${CMAKE_SOURCE_DIR}/third_party/json/include
        ${CMAKE_SOURCE_DIR}/third_party/json/single_include
        ${CMAKE_SOURCE_DIR}/third_party/json
)

if(NOT NLOHMANN_JSON_INCLUDE_DIR)
    message(WARNING "nlohmann/json не найден в системе. Скачиваю...")
    
    file(DOWNLOAD
        "https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp"
        "${CMAKE_SOURCE_DIR}/third_party/json.hpp"
        SHOW_PROGRESS
    )
    set(NLOHMANN_JSON_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/third_party)
else()
    message(STATUS "nlohmann/json найден в: ${NLOHMANN_JSON_INCLUDE_DIR}")
endif()

find_path(PQXX_INCLUDE_DIR
    NAMES pqxx/pqxx
    PATHS /opt/homebrew/include /usr/local/include
)
find_library(PQXX_LIBRARY
    NAMES pqxx
    PATHS /opt/homebrew/lib /usr/local/lib
)

if(PQXX_LIBRARY)
    message(STATUS "libpqxx найден, поддержка PostgreSQL включена")
    set(HAS_PQXX TRUE)
else()
    message(WARNING "libpqxx не найден, поддержка PostgreSQL отключена")
    set(HAS_PQXX FALSE)
endif()

set(THIRD_PARTY_DIR ${PROJECT_SOURCE_DIR}/third_party)
set(SRC_DIR ${PROJECT_SOURCE_DIR}/src)

file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/osm_cache)

set(PROJECT_SOURCES
    ${SRC_DIR}/main.cpp
    ${SRC_DIR}/tile_manager.cpp
    ${SRC_DIR}/heat_worker.cpp
    ${SRC_DIR}/location_data.cpp
    ${SRC_DIR}/server.cpp
    ${SRC_DIR}/map_renderer.cpp
    ${SRC_DIR}/ui_panels.cpp
)

if(HAS_PQXX)
    list(APPEND PROJECT_SOURCES ${SRC_DIR}/database.cpp)
    set(PQXX_DEFINITION -DHAS_POSTGRESQL)
endif()

add_library(imgui STATIC
    ${THIRD_PARTY_DIR}/imgui/imgui.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_draw.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_tables.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_widgets.cpp
    ${THIRD_PARTY_DIR}/imgui/backends/imgui_impl_sdl2.cpp
    ${THIRD_PARTY_DIR}/imgui/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui PUBLIC 
    ${THIRD_PARTY_DIR}/imgui 
    ${THIRD_PARTY_DIR}/imgui/backends
)

target_link_libraries(imgui PUBLIC 
    SDL2::SDL2 
    OpenGL::GL 
    GLEW::GLEW
)

add_library(implot STATIC
    ${THIRD_PARTY_DIR}/implot/implot.cpp
    ${THIRD_PARTY_DIR}/implot/implot_items.cpp
)

target_include_directories(implot PUBLIC ${THIRD_PARTY_DIR}/implot)
target_link_libraries(implot PUBLIC imgui)

add_executable(${PROJECT_NAME} ${PROJECT_SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE 
    ${SRC_DIR}
    ${ZMQ_INCLUDE_DIRS}
    ${NLOHMANN_JSON_INCLUDE_DIR}
    ${THIRD_PARTY_DIR}
    ${CURL_INCLUDE_DIRS}
    ${PNG_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
    imgui
    implot
    ${ZMQ_LIBRARIES}
    ${CURL_LIBRARIES}
    ${PNG_LIBRARIES}
    Threads::Threads
    dl
)

if(HAS_PQXX)
    target_include_directories(${PROJECT_NAME} PRIVATE ${PQXX_INCLUDE_DIR})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${PQXX_LIBRARY})
    target_compile_definitions(${PROJECT_NAME} PRIVATE ${PQXX_DEFINITION})
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/osm_cache
    COMMENT "Creating OSM cache directory"
)

message(STATUS "========================================")
message(STATUS "Build Configuration:")
message(STATUS "  Project: ${PROJECT_NAME}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  ZMQ: ${ZMQ_LIBRARIES}")
message(STATUS "  SDL2: ${SDL2_LIBRARIES}")
message(STATUS "  OpenGL: ${OPENGL_LIBRARIES}")
message(STATUS "  GLEW: ${GLEW_LIBRARIES}")
message(STATUS "  CURL: ${CURL_LIBRARIES}")
message(STATUS "  PNG: ${PNG_LIBRARIES}")
message(STATUS "  PostgreSQL: ${HAS_PQXX}")
if(HAS_PQXX)
    message(STATUS "  libpqxx: ${PQXX_LIBRARY}")
endif()
message(STATUS "========================================")