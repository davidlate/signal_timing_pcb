#ifndef STP_SD_SDCARDOPS_H
#define STP_SD_SDCARDOPS_H

#include <esp_err.h>
#include "sdmmc_cmd.h"
#include "sd_test_io.h"
#include "driver/i2s_std.h"
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "sdmmc_cmd.h"
#include "sd_test_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h> 
#include <inttypes.h>
#include "esp_random.h"
#include "bootloader_random.h"
#include "math.h"
#include "esp_vfs_fat.h"


typedef struct {
    bool open;
    int miso_do_pin;
    int mosi_di_pin;
    int clk_pin;
    int cs_pin;
    char* mount_point;
    sdmmc_card_t* card_ptr;
    sdmmc_host_t host;
} stp_sd__spi_config;

typedef struct {
    int       capacity;
    char      filename[20];
    FILE*     fp;
    char      RIFF[4];
    int32_t   filesize;
    char      WAVE[4];
    char      fmt[4];
    int32_t   fmt_length;
    int16_t   fmt_type;
    int16_t   num_channels;
    int32_t   sample_rate;
    int32_t   SampleRateBitsPerSampleChannels_8;
    int16_t   BitsPerSampleChannels_8_1;
    int16_t   bitness;
    char      data_header[4];
    int32_t   data_size;
    int32_t   audio_len;
    long int  audiofile_data_start_pos;
    long int  audiofile_data_end_pos;
} stp_sd__wavFile;

typedef struct {
    int      chunk_len_wo_dither;         //REQUIRED INPUT: length of chunk in number of samples, not including dither
    int      rise_fall_num_samples;       //REQUIRED INPUT: Number of samples to apply rise/fall scaling to (nominally 96 [1ms @ 96000Hz]) at the beginning and end of the chunk
    int      padding_num_samples;         //REQUIRED INPUT: Number of samples to offset from the beginning and end of the audio data
    int      pre_dither_num_samples;      //REQUIRED INPUT: Number of samples of dither to append to the beginning and end of the audio file (to appease the PCM5102a chip we are using)
    int      post_dither_num_samples;
    int      capacity;                    //memory capacity of chunk_data_ptr
    int      chunk_size;                  //size of chunk in bytes
    int      start_idx;                   //starting index of chunk relative to audio data
    int      data_idx;                    //current data location idx, not including dither
    int      end_idx;                     //ending index of chunk relative to audio data
    int32_t* chunk_data_ptr;              //array of int32_t audio samples
    int      chunk_data_pos;              //index in audio chunk we currently are, not including dither
    int      chunk_len_inc_dither;

} stp_sd__audio_chunk;

esp_err_t stp_sd__mount_sd_card(stp_sd__spi_config*);
esp_err_t stp_sd__unmount_sd_card(stp_sd__spi_config*);
esp_err_t stp_sd__open_audio_file(stp_sd__wavFile*);

/**
 * @brief //This function selects a "chunk" of specified length from the wave file, beginning at a random start point, with padding after the beginning and before the ending
*/
 esp_err_t stp_sd__get_audio_chunk(stp_sd__audio_chunk*, stp_sd__wavFile*);


esp_err_t stp_sd__destruct_audio_chunk(stp_sd__audio_chunk*);
esp_err_t stp_sd__close_audio_file(stp_sd__wavFile*);














#endif