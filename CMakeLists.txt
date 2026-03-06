cmake_minimum_required(VERSION 3.20)

project(mycoroutine
    VERSION 1.0.0
    DESCRIPTION "Stackful coroutine library based on ucontext and epoll"
    LANGUAGES CXX
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)

option(MYCOROUTINE_BUILD_EXAMPLES "Build example programs" ON)
option(MYCOROUTINE_BUILD_NETWORK_DEMOS "Build long-running network demo targets" OFF)
option(MYCOROUTINE_ENABLE_WARNINGS "Enable strict compiler warnings" ON)

add_library(mycoroutine STATIC)
add_library(mycoroutine::mycoroutine ALIAS mycoroutine)

target_sources(mycoroutine
    PRIVATE
        src/thread.cpp
        src/fiber.cpp
        src/scheduler.cpp
        src/timer.cpp
        src/iomanager.cpp
        src/hook.cpp
        src/fd_manager.cpp
        src/utils.cpp
)

target_compile_features(mycoroutine PUBLIC cxx_std_17)

set_target_properties(mycoroutine PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

target_include_directories(mycoroutine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

find_package(Threads REQUIRED)
target_link_libraries(mycoroutine PRIVATE Threads::Threads)

if(MYCOROUTINE_ENABLE_WARNINGS)
    target_compile_options(mycoroutine
        PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
            $<$<CXX_COMPILER_ID:MSVC>:/W4>
    )
endif()

if(MYCOROUTINE_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

install(TARGETS mycoroutine
    EXPORT mycoroutineTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT mycoroutineTargets
    FILE mycoroutineTargets.cmake
    NAMESPACE mycoroutine::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mycoroutine
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/mycoroutineConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/mycoroutineConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mycoroutine
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/mycoroutineConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/mycoroutineConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/mycoroutineConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mycoroutine
)
