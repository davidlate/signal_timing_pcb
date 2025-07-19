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
#include "driver/gpio.h"

#include "stp_sd_sdcardops.h"
#include "stp_i2s_audio_ops.h"



//#define AUDIO_FILENAME "/AUDIO.wav"
#define AUDIO_FILENAME "/SINEA.wav"
#define PIN_NUM_MISO  GPIO_NUM_13   //DO
#define PIN_NUM_MOSI  GPIO_NUM_11   //DI
#define PIN_NUM_CLK   GPIO_NUM_12   //CLK
#define PIN_NUM_CS    GPIO_NUM_10   //CS

#define LED1_PIN    GPIO_NUM_48
#define LED2_PIN    GPIO_NUM_38


const int    I2S_WS_PIN         = GPIO_NUM_4;      //LCK, LRC, 13 I2S word select io number
const int    I2S_DOUT_PIN       = GPIO_NUM_5;      //DIN, 12 I2S data out io number
const int    I2S_BCK_PIN        = GPIO_NUM_6;      //BCK 11  I2S bit clock io number
const float  VOL_PERCENT        = 100.00;
const double SAMPLE_RATE        = 96000.00;
const double DURATION_MS        = 10.00;
const double AUDIO_RISE_TIME_MS = 1.0;


const double NUM_DMA_BUFF  = 5;
const double SIZE_DMA_BUFF = 500;


void flash_lights(void * pvParameters){

    gpio_set_direction(LED1_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED2_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(LED1_PIN, 0);
    gpio_set_level(LED2_PIN, 0);

    while(true){
        gpio_set_level(LED1_PIN, 1);
        gpio_set_level(LED2_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(500));
        gpio_set_level(LED1_PIN, 0);
        gpio_set_level(LED2_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}


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
    };
    printf("Here!");
    vTaskDelay(pdMS_TO_TICKS(2000));
    if(stp_sd__mount_sd_card(&spi_config) != ESP_OK){
        ESP_LOGE(TAG, "Error Mounting SD Card!");
        ESP_LOGI(TAG, "Trying to mount again...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        if(stp_sd__mount_sd_card(&spi_config) != ESP_OK){
            ESP_LOGE(TAG, "Second SD card mount also failed!");
            return;
        }
    }

    BaseType_t task_created;
    TaskHandle_t flash_light_task = NULL;
    task_created = xTaskCreate(flash_lights, "Flash Lights", 1024, (void*) 0, 0, &flash_light_task);
    if(task_created != pdPASS){
        ESP_LOGE(TAG, "Error creating flashing light task!");
        return;
    }

    stp_sd__wavFile wave_file = {
      .filename = AUDIO_FILENAME,
    };  
    if(stp_sd__open_audio_file(&wave_file) != ESP_OK){
        ESP_LOGE(TAG, "Error Opening Audio File!");
        return;
    }

    stp_sd__audio_chunk audio_chunk = {     //All zeroed or NULL parameters are set by the sd_stp__get_audio_chunk function
        .chunk_len_wo_dither     = 1920,    //10ms per channel
        .rise_fall_num_samples   = 0,     //1ms rise/fall per channel
        .padding_num_samples     = 10,      //10 samples file padding
        .dither_num_samples      = 1920,     //NEED TO EVALUATE IDEAL LENGTH OF DITHER TO AVOID SOFT MUTE POP
        .capacity                = 0,
        .start_idx               = 0,
        .data_idx                = 0,
        .end_idx                 = 0,     
        .chunk_data_ptr          = NULL,     
        .chunk_data_pos          = 0,
        .chunk_len_inc_dither    = 0,
    };

    stp_i2s__i2s_config i2s_config = {
                        .buf_capacity             = 0,
                        .buf_len                  = 0,
                        .num_dma_buf              = NUM_DMA_BUFF,
                        .size_dma_buf             = SIZE_DMA_BUFF,
                        .ms_delay_between_writes  = 0,
                        .bclk_pin                 = I2S_BCK_PIN,    
                        .ws_pin                   = I2S_WS_PIN,
                        .dout_pin                 = I2S_DOUT_PIN,
                        .sample_rate_Hz           = SAMPLE_RATE,
                        .max_vol_dBFS             = -20,
                        .min_vol_dB_rel_to_max    = -60,
                        .set_vol_percent          = 10,
                        .vol_scale_factor         = 0,
                        .min_vol_percent          = 2,
                        .actual_dbFS              = 0,
                        .preloaded                = false
                        };

    if(stp_i2s__i2s_channel_setup(&i2s_config) != ESP_OK){
        ESP_LOGE(TAG, "Error setting up i2s audio channel!");
        // return;
    };

    for(int i=0; i<500; i++){
        ESP_ERROR_CHECK(stp_sd__get_audio_chunk(&audio_chunk, &wave_file));
        ESP_ERROR_CHECK(stp_i2s__preload_buffer(&i2s_config, &audio_chunk, 20.0));
        ESP_ERROR_CHECK(stp_i2s__i2s_channel_enable(&i2s_config));
        ESP_ERROR_CHECK(stp_i2s__play_audio_chunk(&i2s_config, &audio_chunk, 20.0));
        ESP_ERROR_CHECK(stp_i2s__i2s_channel_disable(&i2s_config));
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    if(stp_sd__destruct_audio_chunk(&audio_chunk) != ESP_OK){
        ESP_LOGE(TAG, "Error destructing audio chunk!");
        return;
    }
    if(stp_sd__close_audio_file(&wave_file) != ESP_OK){
        ESP_LOGE(TAG, "Error closing audio file!");
        return;
    }
    if(stp_sd__unmount_sd_card(&spi_config) != ESP_OK){
        ESP_LOGE(TAG, "Error unmounting SD card");
        return;
    }

    return;
}