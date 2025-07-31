set(srcs "main.c" "stp_i2s_audio_ops.c" "stp_sd_sdcardops.c")

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "."
                       REQUIRES fatfs sd_card esp_timer esp_driver_i2s bootloader_support
                       WHOLE_ARCHIVE)
