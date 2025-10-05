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
#include "hal/adc_types.h"
#include "esp_adc/adc_oneshot.h"
#include "freertos/queue.h"

#include "stp_sd_sdcardops.h"
#include "stp_i2s_audio_ops.h"
#include "stp_adc.h"


//SD Card operation defines
//#define AUDIO_FILENAME "/SINEA.wav"
#define AUDIO_FILENAME "/WHITE.wav"

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
#define VOL_ADC_UNIT    ADC_UNIT_2
#define VOL_ADC_CHAN    ADC_CHANNEL_3
#define CH1_KNOB_PIN    GPIO_NUM_1      //Status of ch1 knob DAC pin
#define CH1_ADC_UNIT    ADC_UNIT_1
#define CH1_ADC_CHAN    ADC_CHANNEL_0
#define CH2_KNOB_PIN    GPIO_NUM_15     //Status of ch2 knob DAC pin
#define CH2_ADC_UNIT    ADC_UNIT_2
#define CH2_ADC_CHAN    ADC_CHANNEL_4
#define BATT_PIN        GPIO_NUM_16     //9VDIV battery sample DAC pin
#define BATT_ADC_UNIT   ADC_UNIT_2
#define BATT_ADC_CHAN   ADC_CHANNEL_5
#define HVDIV_PIN       GPIO_NUM_18     //28VDIV pin
#define HVDIV_ADC_UNIT  ADC_UNIT_2
#define HVDIV_ADC_CHAN  ADC_CHANNEL_7

#define ADC_UPDATE_PERIOD_MS    100
#define PRINT_UPDATE_PERIOD_MS  100

//PWM Pin defines
#define CH1_CURSET_PIN  GPIO_NUM_2
#define CH2_CURSET_PIN  GPIO_NUM_47

//RMT Pin defines
#define CH1_SW_OUTA_PIN     GPIO_NUM_9  //CH1_SW_OUT+
#define CH1_SW_OUTB_PIN     GPIO_NUM_8  //CH1_SW_OUT-
#define CH2_SW_OUTA_PIN     GPIO_NUM_36 //CH2_SW_OUT+
#define CH2_SW_OUTB_PIN     GPIO_NUM_37 //CH2_SW_OUT-

//I2S defines
#define I2S_WS_PIN   GPIO_NUM_4      //LCK, LRC, 13 I2S word select io number
#define I2S_DOUT_PIN GPIO_NUM_5      //DIN, 12 I2S data out io number
#define I2S_BCK_PIN  GPIO_NUM_6      //BCK 11  I2S bit clock io number

const int SAMPLE_RATE    = 96000;
const int NUM_DMA_BUFF   = 8;
const int SIZE_DMA_BUFF  = 500;         //can go up to 500


typedef struct {
    SemaphoreHandle_t         adc_mutex;
    stp_adc__adc_chan_results adc_results;
} Adc_Update_Struct;

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

void update_adc(void* pvParameters){

    Adc_Update_Struct* adc_update_struct = (Adc_Update_Struct*)pvParameters;
    
    stp_adc__adc_setup_struct adc_chan_setup = {
        .vol_adc_unit  = VOL_ADC_UNIT,
        .vol_adc_chan  = VOL_ADC_CHAN,
        .ch1_adc_unit  = CH1_ADC_UNIT,
        .ch1_adc_chan  = CH1_ADC_CHAN,
        .ch2_adc_unit  = CH2_ADC_UNIT,
        .ch2_adc_chan  = CH2_ADC_CHAN,
        .batt_adc_unit = BATT_ADC_UNIT,
        .batt_adc_chan = BATT_ADC_CHAN,
        .hv_adc_unit   = HVDIV_ADC_UNIT,
        .hv_adc_chan   = HVDIV_ADC_CHAN,
    };

    stp_adc__adc_chan_struct adc_chan_struct;
    stp_adc__setup_adc_chans(adc_chan_setup, &adc_chan_struct);

    while (true)
    {
        if(xSemaphoreTake(adc_update_struct->adc_mutex, portMAX_DELAY)==pdTRUE)
        {
            stp_adc__read_all_adc_chans(&adc_chan_struct, &(adc_update_struct->adc_results));

            xSemaphoreGive(adc_update_struct->adc_mutex);
            vTaskDelay(pdMS_TO_TICKS(ADC_UPDATE_PERIOD_MS));
        }
    }
}

void print_to_terminal(void* pvParameters){
    vTaskDelay(pdMS_TO_TICKS(100));
    Adc_Update_Struct* adc_update_struct = (Adc_Update_Struct*)pvParameters;

    while (true)
    {
        if (xSemaphoreTake(adc_update_struct->adc_mutex, portMAX_DELAY) == pdTRUE)
        {
            stp_adc__adc_chan_results adc_results = adc_update_struct->adc_results;

            printf("\rVol: %.0f%% | Ch2: %0.0f%% | Ch1: %0.0f%% |Batt: %.1fV | HV: %.1fV  ",
                adc_results.vol_percent,
                adc_results.ch1_percent,
                adc_results.ch2_percent,
                adc_results.batt_voltage,
                adc_results.hv_voltage);

            xSemaphoreGive(adc_update_struct->adc_mutex);
        }
            vTaskDelay(pdMS_TO_TICKS(PRINT_UPDATE_PERIOD_MS));
    }
}


void app_main(void)
{
    char* TAG = "main";

    
    printf("\33[?25l"); //Hide terminal cursor, quoted from https://stackoverflow.com/questions/30126490/how-to-hide-console-cursor-in-c;

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

    BaseType_t flash_light_task_created;
    TaskHandle_t flash_light_task = NULL;
    int flash_light_task_priority = 0;
    flash_light_task_created = xTaskCreate(flash_lights, "Flash Lights", 1024, (void*) 0, flash_light_task_priority, &flash_light_task);
    if(flash_light_task_created != pdPASS){
        ESP_LOGE(TAG, "Error creating flashing light task!");
        return;
    }

    SemaphoreHandle_t adc_mutex = NULL;
    adc_mutex = xSemaphoreCreateMutex();
    if( adc_mutex == NULL ) ESP_LOGE(TAG, "Error creating adc semaphore!");

    Adc_Update_Struct adc_update_struct = {0};
    adc_update_struct.adc_mutex = adc_mutex;

    BaseType_t adc_task_created;
    TaskHandle_t update_adc_task = NULL;
    int adc_task_priority = 1;
    adc_task_created = xTaskCreate(update_adc, "Update ADC Values", 4096, &adc_update_struct, adc_task_priority, &update_adc_task);
    if(adc_task_created != pdPASS){
        ESP_LOGE(TAG, "Error creating adc update task!");
        return;
    }

    BaseType_t print_task_created;
    TaskHandle_t update_print_task = NULL;
    int print_task_priority = 1;
    print_task_created = xTaskCreate(print_to_terminal, "Print to Terminal", 4096, &adc_update_struct, print_task_priority, &update_print_task);
    if(print_task_created != pdPASS){
        ESP_LOGE(TAG, "Error creating print to terminal update task!");
        return;
    }

    
    stp_sd__wavFile wave_file = {
      .filename = AUDIO_FILENAME,
    };  
    if(stp_sd__open_audio_file(&wave_file) != ESP_OK){
        ESP_LOGE(TAG, "Error Opening Audio File!");
        return;
    }

    stp_sd__audio_chunk_setup audio_chunk_setup = {
        .chunk_len_wo_dither        = 7680,    //REQUIRED INPUT: length of chunk in number of samples, not including dither
        .rise_fall_num_samples      = 2400,      //REQUIRED INPUT: Number of samples to apply rise/fall scaling to (nominally 96 [1ms @ 96000Hz]) at the beginning and end of the chunk
        .padding_num_samples        = 100,      //REQUIRED INPUT: Number of samples to offset from the beginning and end of the audio data
        .pre_dither_num_samples     = 0,     //REQUIRED INPUT: Number of samples of dither to append to the beginning and end of the audio file (to appease the PCM5102a chip we are using)
        .post_dither_num_samples    = 0,
        .max_chunk_buf_size_bytes   = NUM_DMA_BUFF*SIZE_DMA_BUFF*sizeof(int32_t)*4+1, //Factor of four is needed to match i2s buff size.  +1 is added to make the buffer bigger just in case
        .wavFile_ptr                = &wave_file,
    } ;
    stp_sd__audio_chunk audio_chunk = {0};
    stp_sd__init_audio_chunk(&audio_chunk_setup, &audio_chunk);

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
                        .min_vol_dBFS             = -60,
                        .set_vol_percent          = 10,
                        .vol_scale_factor         = 0,
                        .min_vol_percent          = 2,
                        .actual_dbFS              = 0,
                        .preloaded                = false
                        };
    ESP_ERROR_CHECK(stp_i2s__i2s_channel_setup(&i2s_config));


    for(int i=0; i<500; i++){

        ESP_ERROR_CHECK(stp_sd__get_new_audio_chunk(&audio_chunk, &wave_file, false));

        if(xSemaphoreTake(adc_update_struct.adc_mutex, portMAX_DELAY) != pdTRUE) ESP_LOGE(TAG, "Error taking adc mutex for volume!");
        double vol_set_perc = (adc_update_struct.adc_results).vol_percent;
        // ESP_ERROR_CHECK(stp_i2s__preload_buffer(&i2s_config, &audio_chunk, 20.0));
        ESP_ERROR_CHECK(stp_i2s__i2s_channel_enable(&i2s_config));
        gpio_set_level(XSMT_PIN, 1);
        ESP_ERROR_CHECK(stp_i2s__play_audio_chunk(&i2s_config, &audio_chunk, vol_set_perc));
        
        xSemaphoreGive(adc_update_struct.adc_mutex);
        // gpio_set_level(XSMT_PIN, 0);
        ESP_ERROR_CHECK(stp_i2s__i2s_channel_disable(&i2s_config));
        vTaskDelay(pdMS_TO_TICKS(500));

    }


    if(stp_sd__free_audio_chunk(&audio_chunk) != ESP_OK){
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