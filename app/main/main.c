/* SD card_ptr and FAT filesystem example.
   This example uses SPI peripheral to communicate with SD card_ptr.
*/

//This program is written by David Earley (2025) 
//This program borrows heavily from this article and the Espressif examples throughout:
//https://truelogic.org/wordpress/2015/09/04/parsing-a-wav-file-in-c/

#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "sd_test_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h> 
#include <inttypes.h>
#include "esp_random.h"
#include "bootloader_random.h"
#include "math.h"
#include "esp_timer.h"

#include "stp_sd_sdcardops.h"



#define AUDIO_FILENAME "/AUDIO.wav"
#define PIN_NUM_MISO  GPIO_NUM_13   //DO
#define PIN_NUM_MOSI  GPIO_NUM_11   //DI
#define PIN_NUM_CLK   GPIO_NUM_12   //CLK
#define PIN_NUM_CS    GPIO_NUM_10   //CS


void app_main(void)
{
    char* TAG = "main";


    stp_sd__spi_config spi_config ={
        .open        = false,
        .mosi_di_pin = PIN_NUM_MOSI,
        .miso_do_pin = PIN_NUM_MISO,
        .clk_pin     = PIN_NUM_CLK,
        .cs_pin      = PIN_NUM_CS,
        .mount_point = "/sdcard",
        .card_ptr    = NULL,
        .host_ptr    = NULL,
    };

    if(stp_sd__mount_sd_card(&spi_config) != ESP_OK){
        ESP_LOGE(TAG, "Error Mounting SD Card!");
        return;
    }

    stp_sd__wavFile wave_file = {
      .filename = AUDIO_FILENAME,
    };  

    if(sd_stp__open_audio_file(&wave_file) != ESP_OK){
        ESP_LOGE(TAG, "Error Opening Audio File!");
        return;
    }

    stp_sd__audio_chunk audio_chunk = {     //All zeroed or NULL parameters are set by the sd_stp__get_audio_chunk function
        .chunk_len_wo_dither     = 1000,
        .rise_fall_num_samples   = 100,
        .padding_num_samples     = 10,
        .dither_num_samples      = 100,
        .capacity                = 0,
        .start_idx               = 0,
        .end_idx                 = 0,     
        .chunk_data_ptr          = NULL,     
        .chunk_data_pos          = 0,
        .chunk_len_inc_dither    = 0,
    };


    for (int j = 0; j<20; j++){

    if(sd_stp__get_audio_chunk(&audio_chunk, &wave_file) != ESP_OK){
        ESP_LOGE(TAG, "Error getting audio chunk!");
        return;
    };
    
    vTaskDelay(pdMS_TO_TICKS(100));
    }

    if(sd_stp__destruct_audio_chunk(&audio_chunk) != ESP_OK){
        ESP_LOGE(TAG, "Error destructing audio chunk!");
        return;
    }

    if(sd_stp__close_audio_file(&wave_file) != ESP_OK){
        ESP_LOGE(TAG, "Error closing audio file!");
        return;
    }

    if(stp_sd__unmount_sd_card(&spi_config) != ESP_OK){
        ESP_LOGE(TAG, "Error unmounting SD card");
        return;
    }

    return;
}