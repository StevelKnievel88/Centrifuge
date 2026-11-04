# Psyched Devices skin for JUCE apps.
#   include(path/to/psyched-skin/juce/psyched_skin.cmake)
#   psyched_skin_add(MyTarget)
# Adds PsychedSkin.cpp, the include path, and the fonts as binary data (namespace PsychedSkinFonts).

set(PSYCHED_SKIN_DIR "${CMAKE_CURRENT_LIST_DIR}/.." CACHE INTERNAL "")

function(psyched_skin_add target)
    if(NOT TARGET PsychedSkinFonts)
        juce_add_binary_data(PsychedSkinFonts
            HEADER_NAME PsychedSkinFonts.h
            NAMESPACE PsychedSkinFonts
            SOURCES
                ${PSYCHED_SKIN_DIR}/fonts/Unbounded-SemiBold.ttf
                ${PSYCHED_SKIN_DIR}/fonts/Unbounded-ExtraBold.ttf
                ${PSYCHED_SKIN_DIR}/fonts/Manrope-Regular.ttf
                ${PSYCHED_SKIN_DIR}/fonts/Manrope-SemiBold.ttf
                ${PSYCHED_SKIN_DIR}/fonts/Manrope-ExtraBold.ttf
                ${PSYCHED_SKIN_DIR}/fonts/JetBrainsMono-Medium.ttf
                ${PSYCHED_SKIN_DIR}/fonts/JetBrainsMono-SemiBold.ttf)
        set_target_properties(PsychedSkinFonts PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
    endif()
    target_sources(${target} PRIVATE ${PSYCHED_SKIN_DIR}/juce/PsychedSkin.cpp ${PSYCHED_SKIN_DIR}/juce/PsychedSkin.h)
    target_include_directories(${target} PRIVATE ${PSYCHED_SKIN_DIR}/juce)
    target_link_libraries(${target} PRIVATE PsychedSkinFonts)
endfunction()
