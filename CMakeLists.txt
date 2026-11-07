cmake_minimum_required(VERSION 3.22)

project(Centrifuge VERSION 0.8.3 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(MSVC)
    # Static runtime: the .exe runs without installing the VC++ redistributable.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ---------------------------------------------------------------------------
# Dependencies (fetched from GitHub; set FETCHCONTENT_SOURCE_DIR_<NAME> to use
# local copies instead)
# ---------------------------------------------------------------------------
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.10
    GIT_SHALLOW    ON)

FetchContent_Declare(sqlite3mc
    GIT_REPOSITORY https://github.com/utelle/SQLite3MultipleCiphers.git
    GIT_TAG        v2.5.1
    GIT_SHALLOW    ON
    SOURCE_SUBDIR  do-not-configure)

FetchContent_Declare(sigstretch
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
    GIT_TAG        1.4.0
    GIT_SHALLOW    ON
    SOURCE_SUBDIR  do-not-configure)

FetchContent_Declare(siglinear
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/linear.git
    GIT_TAG        0.6.4
    GIT_SHALLOW    ON
    SOURCE_SUBDIR  do-not-configure)

FetchContent_MakeAvailable(juce sqlite3mc sigstretch siglinear)

# ---------------------------------------------------------------------------
# SQLite3 Multiple Ciphers (reads rekordbox's encrypted master.db)
# ---------------------------------------------------------------------------
set(SQ ${sqlite3mc_SOURCE_DIR}/src)
add_library(orbit_sqlite3mc STATIC ${SQ}/sqlite3mc.c)
target_include_directories(orbit_sqlite3mc PUBLIC ${SQ} PRIVATE ${SQ}/aegis/include ${SQ}/argon2/include)
target_compile_definitions(orbit_sqlite3mc PRIVATE
    SQLITE_THREADSAFE=1 SQLITE_DQS=0 SQLITE_OMIT_LOAD_EXTENSION=1)

if(MSVC)
    target_compile_options(orbit_sqlite3mc PRIVATE /W0)
else()
    target_compile_options(orbit_sqlite3mc PRIVATE -w -O2)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        target_compile_options(orbit_sqlite3mc PRIVATE -msse4.2 -maes)
    endif()
    find_package(Threads REQUIRED)
    target_link_libraries(orbit_sqlite3mc PUBLIC Threads::Threads ${CMAKE_DL_LIBS} m)
endif()

# ---------------------------------------------------------------------------
# ONNX Runtime (stem separation). Optional: without it the app still builds,
# stems are just unavailable.
# ---------------------------------------------------------------------------
set(ORT_ROOT "" CACHE PATH "ONNX Runtime folder containing include/ and lib/")
set(ORBIT_HAS_ORT 0)
set(ORBIT_HAS_DML 0)
if(ORT_ROOT)
    find_path(ORT_INCLUDE_DIR onnxruntime_cxx_api.h PATHS ${ORT_ROOT}/include ${ORT_ROOT}/include/onnxruntime NO_DEFAULT_PATH)
    find_library(ORT_LIBRARY NAMES onnxruntime PATHS ${ORT_ROOT}/lib NO_DEFAULT_PATH)
    if(ORT_INCLUDE_DIR AND ORT_LIBRARY)
        set(ORBIT_HAS_ORT 1)
        if(EXISTS ${ORT_INCLUDE_DIR}/dml_provider_factory.h)
            set(ORBIT_HAS_DML 1)
        endif()
        message(STATUS "ONNX Runtime: ${ORT_LIBRARY} (DirectML: ${ORBIT_HAS_DML})")
    else()
        message(WARNING "ORT_ROOT set but ONNX Runtime not found there; building without stems")
    endif()
endif()

# ---------------------------------------------------------------------------
# ASIO (Windows). Point ASIO_SDK_DIR at the extracted Steinberg ASIO SDK.
# ---------------------------------------------------------------------------
set(ASIO_SDK_DIR "" CACHE PATH "Steinberg ASIO SDK folder (contains common/iasiodrv.h)")
set(ORBIT_HAS_ASIO 0)
if(WIN32 AND ASIO_SDK_DIR AND EXISTS ${ASIO_SDK_DIR}/common/iasiodrv.h)
    set(ORBIT_HAS_ASIO 1)
    message(STATUS "ASIO SDK: ${ASIO_SDK_DIR}")
endif()

# ---------------------------------------------------------------------------
# The app
# ---------------------------------------------------------------------------
juce_add_gui_app(Centrifuge
    PRODUCT_NAME "Centrifuge"
    COMPANY_NAME "Centrifuge"
    VERSION ${PROJECT_VERSION}
    ICON_BIG ${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.png)

include(${CMAKE_CURRENT_SOURCE_DIR}/skin/psyched-skin/juce/psyched_skin.cmake)
juce_add_binary_data(CentrifugeData SOURCES
    resources/fonts/Inter-Regular.otf
    resources/fonts/Inter-SemiBold.otf)

file(GLOB_RECURSE ORBIT_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)
target_sources(Centrifuge PRIVATE ${ORBIT_SOURCES})
psyched_skin_add(Centrifuge)
target_include_directories(Centrifuge PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${sigstretch_SOURCE_DIR}/include
    ${siglinear_SOURCE_DIR}/include)

target_compile_definitions(Centrifuge PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Centrifuge,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Centrifuge,JUCE_VERSION>"
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1
    JUCE_USE_WINDOWS_MEDIA_FORMAT=1
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    ORBIT_HAS_ORT=${ORBIT_HAS_ORT}
    ORBIT_HAS_DML=${ORBIT_HAS_DML}
    ORBIT_HAS_ASIO=${ORBIT_HAS_ASIO})

# Build number and commit shown in the About screen (set by GitHub Actions)
if(DEFINED ENV{GITHUB_RUN_NUMBER} AND DEFINED ENV{GITHUB_SHA})
    string(SUBSTRING "$ENV{GITHUB_SHA}" 0 7 CENTRIFUGE_SHA)
    target_compile_definitions(Centrifuge PRIVATE CENTRIFUGE_BUILD_INFO="build $ENV{GITHUB_RUN_NUMBER}, ${CENTRIFUGE_SHA}")
endif()

if(UNIX AND NOT APPLE)
    # Linux is only used for the preview/test build: no audio drivers needed.
    target_compile_definitions(Centrifuge PRIVATE
        JUCE_ALSA=0 JUCE_JACK=0 JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0)
endif()

if(ORBIT_HAS_ASIO)
    target_compile_definitions(Centrifuge PRIVATE JUCE_ASIO=1)
    target_include_directories(Centrifuge PRIVATE ${ASIO_SDK_DIR}/common)
endif()

if(ORBIT_HAS_ORT)
    target_include_directories(Centrifuge PRIVATE ${ORT_INCLUDE_DIR})
    target_link_libraries(Centrifuge PRIVATE ${ORT_LIBRARY})
endif()

if(WIN32)
    target_link_libraries(Centrifuge PRIVATE mfplat mfreadwrite mfuuid propsys)
endif()

target_link_libraries(Centrifuge
    PRIVATE
        orbit_sqlite3mc
        CentrifugeData
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_cryptography
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

if(MSVC)
    target_compile_options(Centrifuge PRIVATE /bigobj /fp:fast)
else()
    target_compile_options(Centrifuge PRIVATE -ffast-math -Wno-shadow -Wno-sign-conversion -Wno-float-conversion
        -Wno-implicit-float-conversion -Wno-conversion -Wno-unused-parameter)
endif()
