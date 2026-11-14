cmake_minimum_required(VERSION 3.22)

# MAZZFLATOR - version ordinateur (Mac, Windows, Linux) - GPL v3
# Mac : un seul fichier pour Intel et Apple Silicon (à fixer avant project())
set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "")

project(Mazzflator VERSION 2.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE (9.0.5) : ~/JUCE par défaut, ou -DJUCE_DIR=...
if(NOT DEFINED JUCE_DIR)
    set(JUCE_DIR $ENV{HOME}/JUCE)
endif()
# clap-juce-extensions : libs/clap-juce-extensions par défaut, ou -DCLAP_JUCE_DIR=...
if(NOT DEFINED CLAP_JUCE_DIR)
    set(CLAP_JUCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs/clap-juce-extensions)
endif()
add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)
add_subdirectory(${CLAP_JUCE_DIR} ${CMAKE_BINARY_DIR}/clap-juce-extensions EXCLUDE_FROM_ALL)

# formats par système (CLAP ajouté plus bas pour tous)
if(APPLE)
    set(MAZZ_FORMATS AU VST3)
elseif(WIN32)
    set(MAZZ_FORMATS VST3)
else()
    set(MAZZ_FORMATS VST3 LV2)
endif()

# copie dans les dossiers de plugins de l'utilisateur après compilation : oui sur Mac en local, non ailleurs
if(APPLE)
    option(MAZZ_COPY "Copier les plugins après compilation" ON)
else()
    option(MAZZ_COPY "Copier les plugins après compilation" OFF)
endif()

juce_add_plugin(Mazzflator
    COMPANY_NAME "Mazztock"
    COMPANY_WEBSITE "https://github.com/mazztock/MAZZFLATOR"
    BUNDLE_ID com.mazztock.mazzflator
    PLUGIN_MANUFACTURER_CODE Mazz
    PLUGIN_CODE Mzfl
    FORMATS ${MAZZ_FORMATS}
    PRODUCT_NAME "MAZZFLATOR"
    DESCRIPTION "Loudness enhancer inspired by the Inflator"
    LV2URI "https://github.com/mazztock/MAZZFLATOR"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Distortion Mastering
    COPY_PLUGIN_AFTER_BUILD ${MAZZ_COPY})

clap_juce_extensions_plugin(TARGET Mazzflator
    CLAP_ID "com.mazztock.mazzflator"
    CLAP_FEATURES audio-effect distortion mastering stereo mono)

# le moteur est partagé avec la version MPC : source/src/
set(MAZZ_ENGINE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../source/src)
target_include_directories(Mazzflator PRIVATE ${MAZZ_ENGINE_DIR})
target_sources(Mazzflator PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    ${MAZZ_ENGINE_DIR}/MazzflatorEngine.cpp)

# skin 2026 (images x2 + police de l'afficheur), intégré au plugin
file(GLOB SKIN_FICHIERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Skin/*.png
    ${CMAKE_CURRENT_SOURCE_DIR}/Skin/*.ttf)
juce_add_binary_data(MazzflatorSkin SOURCES ${SKIN_FICHIERS})

target_compile_definitions(Mazzflator PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Mazzflator
    PRIVATE juce::juce_audio_utils
            MazzflatorSkin
    PUBLIC juce::juce_recommended_config_flags
           juce::juce_recommended_lto_flags
           juce::juce_recommended_warning_flags)

# Mac : le CLAP n'est pas signé par défaut (VST3/AU le sont) -> signature locale après compilation
if(APPLE)
    add_custom_command(TARGET Mazzflator_CLAP POST_BUILD
        COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:Mazzflator_CLAP>"
        COMMAND /bin/sh -c "test -d \"$ENV{HOME}/Library/Audio/Plug-Ins/CLAP/MAZZFLATOR.clap\" && codesign --force --sign - \"$ENV{HOME}/Library/Audio/Plug-Ins/CLAP/MAZZFLATOR.clap\" || true"
        COMMENT "Signature locale du CLAP")
endif()
