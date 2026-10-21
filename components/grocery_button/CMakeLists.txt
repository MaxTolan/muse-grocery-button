# ESP-IDF component for the grocery button: the conversation script, the
# Google speech client, the status screen and the device glue. tests/ builds
# the plain-C parts on the host (tools/test.sh).
#
# WHOLE_ARCHIVE: grocery_glue.c registers itself with the SDK (muse_ext.h)
# from a constructor, and nothing else references it.
set(srcs "grocery_parse.c" "grocery_script.c" "grocery_outbox.c" "grocery_speech_util.c"
         "grocery_ui.c" "grocery_mascot.c" "grocery_glue.c")
# The Google voice path (issue #1): off until it's been tried on the board.
if(CONFIG_GROCERY_GOOGLE_SPEECH)
    list(APPEND srcs "google_speech.c" "grocery_voice.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES muse
    PRIV_REQUIRES nvs_flash esp_timer esp_http_client esp-tls mbedtls esp_driver_gpio esp_hw_support
    WHOLE_ARCHIVE
)

# Issue #2: every message to Muse starts with the source tag (grocery_source.h).
# Voice notes carry CONFIG_MUSE_VOICE_NOTE_TEXT, so refuse a build without it.
if(CONFIG_GROCERY_BUTTON AND NOT CMAKE_BUILD_EARLY_EXPANSION)
    file(STRINGS "${COMPONENT_DIR}/include/grocery_source.h" _tag_line REGEX "^#define GROCERY_SOURCE_TAG ")
    string(REGEX REPLACE "^#define GROCERY_SOURCE_TAG \"(.*)\"$" "\\1" _tag "${_tag_line}")
    string(FIND "${CONFIG_MUSE_VOICE_NOTE_TEXT}" "${_tag} " _at)
    if(NOT _at EQUAL 0)
        message(FATAL_ERROR "CONFIG_MUSE_VOICE_NOTE_TEXT must start with \"${_tag} \" (issue #2); it's \"${CONFIG_MUSE_VOICE_NOTE_TEXT}\"")
    endif()
endif()
