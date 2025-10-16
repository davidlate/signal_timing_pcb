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

typedef struct {        //This is used to pass the requisite data to the task that reloads the memory buffer while the audio task runs.
    int      new_chunk_data_pos;
    int      B_idx;
    int      chunk_start_pos_filebytes;
    int      chunk_len_wo_dither;
    int32_t* chunk_load_ptr;
    int      chunk_load_ptr_cap;
    TaskHandle_t task_to_notify;
} stp_sd__reload_memory_data_struct;

typedef struct {        //This is used to pass the requisite data to the task that reloads the memory buffer while the audio task runs.
    stp_sd__wavFile*    wave_file_ptr;
    QueueHandle_t       reload_audio_buff_Queue;
} stp_sd__reload_memory_Task_struct;

typedef struct {
    int              chunk_len_wo_dither;         //REQUIRED INPUT: length of chunk in number of samples, not including dither
    int              rise_fall_num_samples;       //REQUIRED INPUT: Number of samples to apply rise/fall scaling to (nominally 96 [1ms @ 96000Hz]) at the beginning and end of the chunk
    int              padding_num_samples;         //REQUIRED INPUT: Number of samples to offset from the beginning and end of the audio data
    int              pre_dither_num_samples;      //REQUIRED INPUT: Number of samples of dither to append to the beginning and end of the audio file (to appease the PCM5102a chip we are using)
    int              post_dither_num_samples;
    int              chunk_buf_size_bytes;    //REQUIRED INPUT: Number of bytes we can allocate to holding audio data
    int              capacity;
    int              audio_sample_rate;
    bool*            status;
    stp_sd__wavFile* wavFile_ptr;                 //Pointer to wave file object used for audio
    QueueHandle_t    reload_audio_buff_Queue;
    stp_sd__reload_memory_data_struct* reload_memory_struct_ptr;
} stp_sd__audio_chunk_setup;

typedef struct {
    int              chunk_len_wo_dither;         //length of chunk in number of samples, not including dither
    int              rise_fall_num_samples;       //Number of samples to apply rise/fall scaling to (nominally 96 [1ms @ 96000Hz]) at the beginning and end of the chunk
    int              padding_num_samples;         //Number of samples to offset from the beginning and end of the audio data
    int              pre_dither_num_samples;      //Number of samples of dither to append to the beginning and end of the audio file (to appease the PCM5102a chip we are using)
    int              post_dither_num_samples;
    stp_sd__wavFile* wavFile_ptr;                 //Pointer to wave file object used for audio
    int              capacity;                    //memory capacity of chunk_data_ptr
    int              chunk_size;                  //size of chunk in bytes
    int              start_idx;                   //starting index of chunk relative to audio data
    int              data_idx;                    //current data location idx, not including dither
    int              end_idx;                     //ending index of chunk relative to audio data
    int32_t*         chunk_data_ptr;              //array of int32_t audio samples
    int32_t*         chunk_load_ptr;              //Buffer to load in next set of audio data from SD card
    float*          cos_ramp_LUT_ptr;
    int              chunk_data_pos;              //index in audio chunk we currently are, including dither
    int              memory_buffer_pos;           //index in buffer used to store audio data that we are currently at.
    int              chunk_len_inc_dither;
    int              A_idx;
    int              B_idx;
    int              C_idx;
    int              D_idx;
    int              E_idx;
    int              F_idx;
    int              start_file_pos_samples;
    int              end_file_pos_samples;
    int              delta_file_pos_samples;
    int              audio_sample_rate;
    long             chunk_start_pos_filebytes;  //In the wav file on the sd card, this is the number of bytes from the beginning of the file to the point of the beginning of the audio chunk
    QueueHandle_t    reload_audio_buff_Queue;
    stp_sd__reload_memory_data_struct* reload_memory_struct_ptr;

} stp_sd__audio_chunk;


esp_err_t stp_sd__mount_sd_card(stp_sd__spi_config*);
esp_err_t stp_sd__unmount_sd_card(stp_sd__spi_config*);
esp_err_t stp_sd__open_audio_file(stp_sd__wavFile*);

/**
 * @brief //This function selects a "chunk" of specified length from the wave file, beginning at a random start point, with padding after the beginning and before the ending
*/
esp_err_t stp_sd__init_audio_chunk(stp_sd__audio_chunk_setup*, stp_sd__audio_chunk*);

esp_err_t stp_sd__get_new_audio_chunk(stp_sd__audio_chunk*, stp_sd__wavFile*, bool);
esp_err_t stp_sd__get_next_audio_sample(stp_sd__audio_chunk*, int32_t*, bool*);

void stp_sd__threadsafe_reload_chunk_memory_buffer_Task(void*);

esp_err_t stp_sd__free_audio_chunk(stp_sd__audio_chunk*);

esp_err_t stp_sd__close_audio_file(stp_sd__wavFile*);

#endif