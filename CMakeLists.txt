cmake_minimum_required(VERSION 3.15)

project(quicksample VERSION 0.0.1)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(JUCE)

juce_add_gui_app(quicksample
    PRODUCT_NAME "quicksample"
    VERSION 0.0.1
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)

    pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
    pkg_check_modules(WEBKIT2GTK REQUIRED webkit2gtk-4.1)

    include_directories(${GTK3_INCLUDE_DIRS} ${WEBKIT2GTK_INCLUDE_DIRS})
    link_directories(${GTK3_LIBRARY_DIRS} ${WEBKIT2GTK_LIBRARY_DIRS})

    link_libraries(${GTK3_LIBRARIES} ${WEBKIT2GTK_LIBRARIES})
endif()

find_package(CURL REQUIRED)

target_sources(quicksample
    PRIVATE
    Source/Main.cpp
    Source/MainComponent.h
    Source/MainComponent.cpp
)

target_link_libraries(quicksample
    PRIVATE
    juce::juce_core
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_graphics
    juce::juce_events
    juce::juce_data_structures
    CURL::libcurl
)

target_compile_features(quicksample PUBLIC cxx_std_17)

target_compile_definitions(quicksample
    PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=1
    JUCE_USE_MP3AUDIOFORMAT=1
)

juce_generate_juce_header(quicksample)

install(TARGETS quicksample DESTINATION bin)

set(CPACK_PACKAGE_NAME "quicksample")
set(CPACK_PACKAGE_VENDOR "FerretCode")
set(CPACK_PACKAGE_CONTACT "ferret@frrt.space")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A simple sampler application")
set(CPACK_PACKAGE_VERSION "0.0.1")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(CPACK_GENERATOR "DEB;TGZ") 
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "FerretCode")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
elseif(WIN32)
    set(CPACK_GENERATOR "ZIP;NSIS")
    set(CPACK_NSIS_MODIFY_PATH ON)
endif()

include(CPack)
