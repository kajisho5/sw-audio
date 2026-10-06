# SW AUDIO — build for CLAP / VST3 / AUv2 (macOS) via clap-wrapper (CLAP-first)
cmake_minimum_required(VERSION 3.21)
if (NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0 CACHE STRING "Minimum macOS version")
endif()
project(sw-audio VERSION 0.11.0 LANGUAGES C CXX)
if (APPLE)
    enable_language(OBJC OBJCXX)  # clap-wrapper (AUv2 / VST3 on macOS) compiles Objective-C++ sources
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if (MINGW)
    set(CMAKE_SHARED_MODULE_PREFIX "")  # VST3/CLAP binaries must not get a "lib" prefix
endif()
if (MSVC)
    add_compile_options(/utf-8)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")  # self-contained plugins
endif()

option(SW_BUILD_TESTS "Build unit tests" ON)
option(SW_BUILD_PLUGINS "Build CLAP/VST3/AU plugins" ON)

include(FetchContent)
# Pinned third-party sources (all permissive: CLAP MIT, clap-wrapper MIT, VST3 SDK MIT, doctest MIT)
FetchContent_Declare(clap GIT_REPOSITORY https://github.com/free-audio/clap.git GIT_TAG 1.2.10 GIT_SHALLOW TRUE)
FetchContent_Declare(clap-wrapper GIT_REPOSITORY https://github.com/free-audio/clap-wrapper.git GIT_TAG v0.16.0 GIT_SHALLOW TRUE)
FetchContent_Declare(doctest URL https://raw.githubusercontent.com/doctest/doctest/v2.4.11/doctest/doctest.h DOWNLOAD_NO_EXTRACT TRUE)

# ---- DSP core (framework independent)
add_library(sw-core INTERFACE)
target_include_directories(sw-core INTERFACE core/include)

# one static DSP library per product: products/<code>/<code>.cpp
set(SW_PRODUCTS rv01 gt05 gt04 gt02 gt01 lo03 lo02 lo01 sa08 sa01 sa02 sa03 sa04 sa05 sa06 sa07 ms01 ms03 ms05 ms06 cs01 cs02 cs03 cs04 eq01 eq02 eq03 eq04 eq05 eq06 eq07 eq08 eq09 dy01 dy02 dy03 dy04 dy05 dy06 dy07 dy08 dy09 dy10 dy11 dy12 ms02 ms04 ms07 lv04 lv16 lv17)
foreach(code IN LISTS SW_PRODUCTS)
    add_library(sw-${code} STATIC products/${code}/${code}.cpp)
    target_include_directories(sw-${code} PUBLIC products)
    target_link_libraries(sw-${code} PUBLIC sw-core)
endforeach()

# ---- tests
if (SW_BUILD_TESTS)
    FetchContent_MakeAvailable(doctest)
    file(GLOB SW_TEST_SOURCES CONFIGURE_DEPENDS tests/test_*.cpp)
    add_executable(sw-tests ${SW_TEST_SOURCES})
    target_include_directories(sw-tests PRIVATE ${doctest_SOURCE_DIR})
    foreach(code IN LISTS SW_PRODUCTS)
        target_link_libraries(sw-tests PRIVATE sw-${code})
    endforeach()
    enable_testing()
    add_test(NAME sw-tests COMMAND sw-tests)
endif()

# ---- plugins
if (SW_BUILD_PLUGINS)
    FetchContent_MakeAvailable(clap)
    set(CLAP_SDK_ROOT ${clap_SOURCE_DIR} CACHE PATH "" FORCE)
    set(CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES TRUE CACHE BOOL "" FORCE)  # VST3 SDK / AudioUnit SDK
    FetchContent_MakeAvailable(clap-wrapper)

    # sw_add_plugin(<code> <display name> <AU subtype 4cc> [aufx|aumf])
    function(sw_add_plugin code name ausub)
        set(SW_CODE ${code})
        configure_file(${CMAKE_SOURCE_DIR}/plugin/clap/entry.cpp.in ${CMAKE_BINARY_DIR}/entry/${code}_entry.cpp @ONLY)
        add_library(sw-${code}-impl STATIC plugin/clap/${code}_clap.cpp)
        target_link_libraries(sw-${code}-impl PUBLIC sw-${code} clap)
        set(formats CLAP VST3)
        if (APPLE)
            list(APPEND formats AUV2)
        endif()
        make_clapfirst_plugins(
            TARGET_NAME sw-${code}-plugin
            IMPL_TARGET sw-${code}-impl
            OUTPUT_NAME "${name}"
            ENTRY_SOURCE ${CMAKE_BINARY_DIR}/entry/${code}_entry.cpp
            BUNDLE_IDENTIFIER "com.seventh-well.sw-audio.${code}"
            BUNDLE_VERSION ${PROJECT_VERSION}
            COPY_AFTER_BUILD FALSE
            WINDOWS_FOLDER_VST3 TRUE  # bundle folder: required by the Steinberg validator on Windows
            PLUGIN_FORMATS ${formats}
            ASSET_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins
            AUV2_MANUFACTURER_NAME "SEVENTHWELL"
            AUV2_MANUFACTURER_CODE "SvWl"
            AUV2_SUBTYPE_CODE "${ausub}"
            AUV2_INSTRUMENT_TYPE "aufx"
        )
    endfunction()

    # ---- product list (code, display name, AU subtype)
    sw_add_plugin(cs01 "SW CS01 Inductor Strip" Cs01)
    sw_add_plugin(cs02 "SW CS02 Console Strip" Cs02)
    sw_add_plugin(cs03 "SW CS03 Stepped Strip" Cs03)
    sw_add_plugin(cs04 "SW CS04 Modular Strip" Cs04)
    sw_add_plugin(eq01 "SW EQ01 Passive" Eq01)
    sw_add_plugin(eq02 "SW EQ02 Surgical" Eq02)
    sw_add_plugin(eq03 "SW EQ03 Mid Shaper" Eq03)
    sw_add_plugin(eq04 "SW EQ04 Inductor" Eq04)
    sw_add_plugin(eq05 "SW EQ05 Console" Eq05)
    sw_add_plugin(eq06 "SW EQ06 Stepped" Eq06)
    sw_add_plugin(eq07 "SW EQ07 Dynamic" Eq07)
    sw_add_plugin(eq08 "SW EQ08 Linear" Eq08)
    sw_add_plugin(eq09 "SW EQ09 Tilt" Eq09)
    sw_add_plugin(dy01 "SW DY01 FET" Dy01)
    sw_add_plugin(dy03 "SW DY03 Bus" Dy03)
    sw_add_plugin(dy04 "SW DY04 Gate" Dy04)
    sw_add_plugin(dy07 "SW DY07 Snap" Dy07)
    sw_add_plugin(dy08 "SW DY08 Clean" Dy08)
    sw_add_plugin(ms02 "SW MS02 True Peak" Ms02)
    sw_add_plugin(ms04 "SW MS04 Clipper" Ms04)
    sw_add_plugin(ms07 "SW MS07 Dither" Ms07)
    sw_add_plugin(lv04 "SW LV04 Safety limiter" Lv04)
    sw_add_plugin(lv16 "SW LV16 Live Gate" Lv16)
    sw_add_plugin(lv17 "SW LV17 Bus Comp" Lv17)
    sw_add_plugin(dy02 "SW DY02 Opto" Dy02)
    sw_add_plugin(dy05 "SW DY05 De-ess" Dy05)
    sw_add_plugin(dy06 "SW DY06 Vari-Mu" Dy06)
    sw_add_plugin(dy09 "SW DY09 Transient" Dy09)
    sw_add_plugin(dy10 "SW DY10 Multiband 4" Dy10)
    sw_add_plugin(dy11 "SW DY11 Multiband 6" Dy11)
    sw_add_plugin(dy12 "SW DY12 Parallel" Dy12)
    sw_add_plugin(ms01 "SW MS01 Maximizer" Ms01)
    sw_add_plugin(ms03 "SW MS03 Multiband Limit" Ms03)
    sw_add_plugin(ms05 "SW MS05 Leveler" Ms05)
    sw_add_plugin(ms06 "SW MS06 Master Chain" Ms06)
    sw_add_plugin(sa01 "SW SA01 Tape" Sa01)
    sw_add_plugin(sa02 "SW SA02 Console Sum" Sa02)
    sw_add_plugin(sa03 "SW SA03 Tube" Sa03)
    sw_add_plugin(sa04 "SW SA04 Transformer" Sa04)
    sw_add_plugin(sa05 "SW SA05 Exciter" Sa05)
    sw_add_plugin(sa06 "SW SA06 Saturator" Sa06)
    sw_add_plugin(sa07 "SW SA07 Lo-Fi" Sa07)
    sw_add_plugin(sa08 "SW SA08 Bitcrush" Sa08)
    sw_add_plugin(lo01 "SW LO01 Low Harm" Lo01)
    sw_add_plugin(lo02 "SW LO02 Sub Gen" Lo02)
    sw_add_plugin(lo03 "SW LO03 Low Focus" Lo03)
    sw_add_plugin(gt01 "SW GT01 Amp" Gt01)
    sw_add_plugin(gt02 "SW GT02 Cab IR" Gt02)
    sw_add_plugin(gt04 "SW GT04 Bass Amp" Gt04)
    sw_add_plugin(gt05 "SW GT05 Reamp" Gt05)
    sw_add_plugin(rv01 "SW RV01 Hall" Rv01)
endif()
