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
