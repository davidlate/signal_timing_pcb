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
#include "math.h"
#include "esp_timer.h"
#include "driver/gpio.h"

#include "stp_sd_sdcardops.h"
#include "stp_i2s_audio_ops.h"


//SD Card operation defines
#define AUDIO_FILENAME "/SINEA.wav"
#define PIN_NUM_MISO  GPIO_NUM_13   //DO
#define PIN_NUM_MOSI  GPIO_NUM_11   //DI
#define PIN_NUM_CLK   GPIO_NUM_12   //CLK
#define PIN_NUM_CS    GPIO_NUM_10   //CS

//General IO defines
#define LED1_PIN    48          //GPIO enums only go up to 38
#define LED2_PIN    GPIO_NUM_38
#define XSMT_PIN    GPIO_NUM_7
#define B1_PIN      GPIO_NUM_35
#define B2_PIN      GPIO_NUM_17
#define CH_EN_PIN   GPIO_NUM_21     //H-bridge enable pin

//ADC Pin and channel defines from schematic and https://www.luisllamas.es/en/esp32-s3-hardware-details-pinout/
//See example https://github.com/espressif/esp-idf/blob/v4.4/examples/peripherals/adc/single_read/single_read/main/single_read.c
#define VOL_PIN         GPIO_NUM_14     //Volume sample DAC pin
#define VOL_CHAN        ADC2_CHANNEL_3
#define BATT_PIN        GPIO_NUM_16     //9VDIV battery sample DAC pin
#define BATT_CHAN       ADC2_CHANNEL_5
#define HVDIV_PIN       GPIO_NUM_18     //28VDIV pin
#define HVDIV_CHAN      ADC2_CHANNEL_7
#define CH1_KNOB_PIN    GPIO_NUM_1      //Status of ch1 knob DAC pin
#define CH1_KNOB_CHAN   ADC1_CHANNEL_0
#define CH2_KNOB_PIN    GPIO_NUM_15     //Status of ch2 knob DAC pin
#define CH2_KNOB_CHAN   ADC2_CHANNEL_4     //Status of ch2 knob DAC pin

#define ADC_ATTEN       ADC_ATTEN_DB_11
#define ADC_CAL_SCHEME  ESP_ADC_CAL_VAL_EFUSE_TP_FIT

//PWM Pin defines
#define CH1_CURSET_PIN  GPIO_NUM_2
#define CH2_CURSET_PIN  GPIO_NUM_47

//RMT Pin defines
#define CH1_SW_OUTA_PIN     GPIO_NUM_9  //CH1_SW_OUT+
#define CH1_SW_OUTB_PIN     GPIO_NUM_8  //CH1_SW_OUT-
#define CH2_SW_OUTA_PIN     GPIO_NUM_36 //CH2_SW_OUT+
#define CH2_SW_OUTB_PIN     GPIO_NUM_37

//I2S defines
#define I2S_WS_PIN   GPIO_NUM_4      //LCK, LRC, 13 I2S word select io number
#define I2S_DOUT_PIN GPIO_NUM_5      //DIN, 12 I2S data out io number
#define I2S_BCK_PIN  GPIO_NUM_6      //BCK 11  I2S bit clock io number

const int SAMPLE_RATE    = 96000;
const int NUM_DMA_BUFF   = 5;
const int SIZE_DMA_BUFF  = 250;         //can go up to 500


void flash_lights(void * pvParameters){

    gpio_set_direction(XSMT_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED1_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED2_PIN, GPIO_MODE_OUTPUT);

    gpio_set_level(LED1_PIN, 0);
    gpio_set_level(LED2_PIN, 0);
    gpio_set_level(XSMT_PIN, 0);

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
        .chunk_len_wo_dither     = 5760,    //10ms per channel
        .rise_fall_num_samples   = 0,     //1ms rise/fall per channel
        .padding_num_samples     = 100,     //10 samples file padding
        .dither_num_samples      = 5760,    //PCM5102a needs ~30ms of dither to fully power on  Check the Scope, dither is totally messed up TODO fix
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
                        .ms_delay_between_writes  = 2,
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
    gpio_set_level(XSMT_PIN, 1);

    for(int i=0; i<500; i++){

        ESP_ERROR_CHECK(stp_sd__get_audio_chunk(&audio_chunk, &wave_file));
        // ESP_ERROR_CHECK(stp_i2s__preload_buffer(&i2s_config, &audio_chunk, 20.0));
        vTaskDelay(pdMS_TO_TICKS(200));
        ESP_ERROR_CHECK(stp_i2s__i2s_channel_enable(&i2s_config));
        ESP_ERROR_CHECK(stp_i2s__play_audio_chunk(&i2s_config, &audio_chunk, 20.0));
        vTaskDelay(pdMS_TO_TICKS(100));
        ESP_ERROR_CHECK(stp_i2s__i2s_channel_disable(&i2s_config));
        vTaskDelay(pdMS_TO_TICKS(100));

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