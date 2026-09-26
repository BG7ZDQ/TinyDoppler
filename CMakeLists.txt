cmake_minimum_required(VERSION 3.20)
project(TinyDoppler VERSION 1.0.0 LANGUAGES C CXX)

include(CTest)
include(GNUInstallDirs)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui Widgets Network)

add_library(tiny_sgp4 STATIC third_party/sgp4/sgp4.c)
target_include_directories(tiny_sgp4 PUBLIC third_party/sgp4)
if(MSVC)
    target_compile_options(tiny_sgp4 PRIVATE /w)
endif()

add_executable(TinyDoppler
    app/main.cpp
    app/satellite_tracker_dialog.cpp
    app/satellite_tracker_dialog.h
    app/satellite_catalog.cpp
    app/satellite_catalog.h
    app/catalog_dialog.cpp
    app/catalog_dialog.h
    app/resources.qrc)
set_target_properties(TinyDoppler PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
target_include_directories(TinyDoppler PRIVATE app third_party/sgp4)
target_link_libraries(TinyDoppler PRIVATE tiny_sgp4
    Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Network)
target_compile_definitions(TinyDoppler PRIVATE
    TINY_DOPPLER_VERSION="${PROJECT_VERSION}")
if(WIN32)
    string(REPLACE "." "," TINY_DOPPLER_VERSION_COMMA ${PROJECT_VERSION})
    set_source_files_properties(app/doppler.rc PROPERTIES COMPILE_DEFINITIONS
        "ASRTU_VERSION_COMMA=${TINY_DOPPLER_VERSION_COMMA};ASRTU_VERSION_STRING=\"${PROJECT_VERSION}\"")
    set_target_properties(TinyDoppler PROPERTIES WIN32_EXECUTABLE TRUE)
    target_sources(TinyDoppler PRIVATE app/doppler.rc)
    target_link_libraries(TinyDoppler PRIVATE Qt5::WinMain)
    target_compile_definitions(TinyDoppler PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
if(MSVC)
    target_compile_options(TinyDoppler PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(TinyDoppler PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS TinyDoppler RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY assets/translations/ DESTINATION
    ${CMAKE_INSTALL_BINDIR}/translations FILES_MATCHING PATTERN "*.qm")

if(BUILD_TESTING)
    add_executable(tiny_catalog_test tests/catalog_test.cpp
        app/satellite_catalog.cpp app/satellite_catalog.h app/resources.qrc)
    target_include_directories(tiny_catalog_test PRIVATE app)
    target_link_libraries(tiny_catalog_test PRIVATE Qt5::Core)
    add_test(NAME tiny_catalog_test COMMAND tiny_catalog_test)
    add_executable(tiny_alpha5_test tests/alpha5_test.c)
    target_link_libraries(tiny_alpha5_test PRIVATE tiny_sgp4)
    add_test(NAME tiny_alpha5_test COMMAND tiny_alpha5_test)
    add_executable(tiny_ephemeris_test tests/ephemeris_test.cpp
        app/satellite_tracker_dialog.cpp app/satellite_tracker_dialog.h
        app/satellite_catalog.cpp app/satellite_catalog.h
        app/catalog_dialog.cpp app/catalog_dialog.h app/resources.qrc)
    target_include_directories(tiny_ephemeris_test PRIVATE app third_party/sgp4)
    target_link_libraries(tiny_ephemeris_test PRIVATE tiny_sgp4
        Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Network)
    if(WIN32)
        target_compile_definitions(tiny_ephemeris_test PRIVATE
            NOMINMAX WIN32_LEAN_AND_MEAN)
    endif()
    if(MSVC)
        target_compile_options(tiny_catalog_test PRIVATE /utf-8)
        target_compile_options(tiny_ephemeris_test PRIVATE /utf-8)
    endif()
    add_test(NAME tiny_ephemeris_test COMMAND tiny_ephemeris_test)
endif()
