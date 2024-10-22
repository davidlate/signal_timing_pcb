/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
//https://github.com/infrasonicaudio/esp32-i2s-synth-example/blob/main/main/i2s_example_main.c
#include <stdint.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "soc/esp32/rtc.h"
#include "esp_timer.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "esp_system.h" 
#include "esp_task_wdt.h"
#include "esp_sntp.h"

#define _USE_MATH_DEFINES
//https://esp32.com/viewtopic.php?t=31147



/* Set 1 to allocate rx & tx channels in duplex mode on a same I2S controller, they will share 
the BCLK and WS signal
 * Set 0 to allocate rx & tx channels in simplex mode, these two channels will be totally separated,
 * Specifically, due to the hardware limitation, the simplex rx & tx channels can't be registered on the same controllers on ESP32 and ESP32-S2,
 * and ESP32-S2 has only one I2S controller, so it can't allocate two simplex channels */
#define EXAMPLE_I2S_DUPLEX_MODE         CONFIG_USE_DUPLEX

#define EXAMPLE_STD_BCLK_IO1        14      // I2S bit clock io number
#define EXAMPLE_STD_WS_IO1          15      // I2S word select io number
#define EXAMPLE_STD_DOUT_IO1        22     // I2S data out io number

#define BITS_IN_32BIT               2147483647
#define VOL_PERCENT                 10

#define SAMPLE_RATE                 16000
#define DURATION_MS                 100
#define WAVEFORM_LEN                SAMPLE_RATE/1000*DURATION_MS
#define NUM_DMA_BUFF                8
#define SIZE_DMA_BUFF               1023
#define I2S_BUFF_SIZE               NUM_DMA_BUFF * SIZE_DMA_BUFF

static i2s_chan_handle_t                tx_chan;        // I2S tx channel handler

int i;

// const int SAMPLE_RATE = 16000;
// const int DURATION_MS = 10;
int FREQUENCY          = 1000;





void create_sine_wave(int32_t * waveform, double * waveform_double, int FREQUENCY) {
    // Calculate the number of samples
    printf("Free heap size before allocation: %lu bytes\n", esp_get_free_heap_size());

    printf("Num Samples: %d\n", WAVEFORM_LEN);
    printf("Size of double: %u\n", sizeof(double));

    // Allocate memory for the waveform array
    // int32_t *waveform = (int32_t *)malloc(WAVEFORM_LEN * sizeof(int32_t));
    printf("Free heap size after allocation: %lu bytes\n", esp_get_free_heap_size());

    // Check if memory allocation was successful
    if (waveform == NULL) {
        printf("Memory allocation failed!\n");
        // return NULL;  // Return NULL if allocation failed
    }

    // Populate the waveform array with sine values
    for (int i = 0; i < WAVEFORM_LEN; i++) {
        double timestep = (double)i / (double)SAMPLE_RATE;
        double volume = (double)VOL_PERCENT / (double)100;                  //Operands must be cast to double for this to work.
        // printf("Volume: %0.10f\n", volume);
        double sine_point = (volume * sin(2 * M_PI * FREQUENCY * timestep));  // Use 2 * PI for full sine wave cycle
        int32_t int_point = (int32_t)(sine_point*BITS_IN_32BIT);
        (waveform_double[i]) = sine_point;
        (waveform[i]) = int_point;
    }
    printf("Made it through sine wave function\n");

}


static void i2s_example_write_task(void *waveform)
{
    vTaskDelay(pdMS_TO_TICKS(5000));
    esp_task_wdt_add(xTaskGetCurrentTaskHandle());
    int32_t *audio_waveform = (int32_t*)waveform;
    
    int32_t *w_buf = (int32_t *)calloc(1, I2S_BUFF_SIZE);
    int32_t *zero_buf = (int32_t *)calloc(1, I2S_BUFF_SIZE);

    assert(w_buf); // Check if w_buf allocation success
    size_t w_bytes = I2S_BUFF_SIZE;
    size_t zero_bytes = I2S_BUFF_SIZE;


    size_t audio_samples_pos = 0; // Keep track of where we are in the audio data

    size_t bytes_written = 0;
    /* (Optional) Preload the data before enabling the TX channel, so that the valid data can be transmitted immediately */
    // while (w_bytes == I2S_BUFF_SIZE) {
    //     /* Here we load the target buffer repeatedly, until all the DMA buffers are preloaded */
    //     ESP_ERROR_CHECK(i2s_channel_preload_data(tx_chan, w_buf, I2S_BUFF_SIZE, &w_bytes));
    // }

    /* Enable the TX channel */
    ESP_ERROR_CHECK(i2s_channel_enable(tx_chan));

    while (audio_samples_pos<WAVEFORM_LEN) {
        w_buf[audio_samples_pos] = (audio_waveform[audio_samples_pos]);
        audio_samples_pos++;
        // printf("audio_samples_pos: %i\n audio_waveform[audio_samples_pos]: %ld\n", audio_samples_pos,audio_waveform[audio_samples_pos]);
        }

    for (int i = 0; i < I2S_BUFF_SIZE; i++) {
        zero_buf[i] = 0;
        }
    int64_t start_time = esp_timer_get_time();
    int32_t start_time_ms  = start_time / 1000;
    printf("Time Of Loop Start: %ld ms", start_time_ms);
    int64_t curr_time;
    int32_t curr_time_ms = start_time_ms;
    float curr_time_s;
    vTaskDelay(pdMS_TO_TICKS(1000));
    while(1){
        curr_time_ms = (int32_t)(esp_timer_get_time())/(int32_t)1000 - start_time_ms;

        printf("\n\nCurrent Loop Period: %ld ms\n", curr_time_ms);

        for (int tot_bytes = 0; tot_bytes < WAVEFORM_LEN * sizeof(int32_t); tot_bytes += w_bytes){
            if (i2s_channel_write(tx_chan, w_buf, I2S_BUFF_SIZE, &w_bytes, DURATION_MS*2) == ESP_OK) {
                // printf("Write Task: i2s write %d bytes\n", w_bytes);

                bytes_written += w_bytes;
                            }
            else {
                // printf("Write Task: i2s write failed\n");
            }
            
            for (int j=0; j<I2S_BUFF_SIZE; j+=zero_bytes/4){        //4 bytes per 32-byte note
                    if(i2s_channel_write(tx_chan, zero_buf, I2S_BUFF_SIZE, &zero_bytes, DURATION_MS*2)==ESP_OK){
                        printf("Zero Task: zeroed %d bytes\n", zero_bytes);
                    }
                    else{
                        printf("Zeroing Buffer failed\n");
                    }
            }

            vTaskDelay(pdMS_TO_TICKS(1000));
        }
            if (esp_task_wdt_reset()==ESP_OK){   //Feed the watchdog timer using esp_task_wdt_reset()
            }
    }
    // free(w_buf);
    // vTaskDelete(NULL);
    }


static void i2s_example_init_std_simplex(void)
{
    /* Setp 1: Determine the I2S channel configuration and allocate two channels one by one
     * The default configuration can be generated by the helper macro,
     * it only requires the I2S controller id and I2S role
     * The tx and rx channels here are registered on different I2S controller,
     * Except ESP32 and ESP32-S2, others allow to register two separate tx & rx channels on a same controller */
    i2s_chan_config_t tx_chan_cfg = {
        .id = I2S_NUM_AUTO,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = NUM_DMA_BUFF,
        .dma_frame_num = SIZE_DMA_BUFF,
    }

    ESP_ERROR_CHECK(i2s_new_channel(&tx_chan_cfg, &tx_chan, NULL));

    /* Step 2: Setting the configurations of standard mode and initialize each channels one by one
     * The slot configuration and clock configuration can be generated by the macros
     * These two helper macros is defined in 'i2s_std.h' which can only be used in STD mode.
     * They can help to specify the slot and clock configurations for initialization or re-configuring */
    i2s_std_config_t tx_std_cfg = {
        .clk_cfg  = {
            .sample_rate_hz = SAMPLE_RATE,
            .clk_src = I2S_CLK_SRC_APLL,
            .mclk_multiple = 256,
            },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_32BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode      = I2S_SLOT_MODE_MONO,
            .slot_mask      = I2S_STD_SLOT_BOTH,
        },
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,    // some codecs may require mclk signal, this example doesn't need it
            .bclk = EXAMPLE_STD_BCLK_IO1,
            .ws   = EXAMPLE_STD_WS_IO1,
            .dout = EXAMPLE_STD_DOUT_IO1,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_chan, &tx_std_cfg));
}

void app_main(void)
{

    int64_t start_time = esp_timer_get_time();
    double start_time_ms  = (double)(start_time) / (double)(1000);
    printf("Current Time:  %0.10f ms\n", start_time_ms);
    esp_task_wdt_config_t watchdog_config = {   //Configure watchdog timer
        .timeout_ms = 500000,                      //Set watchdog timeout in ms
        .idle_core_mask = 0,                    //Set to 0 to allow "feeding" the watchdog, set to 1 if you enjoy unhappiness
        .trigger_panic = false                  //Watchdog timer does not cause panic
    };

    // esp_task_wdt_init(&watchdog_config);            //Initialize watchdog using configuration.  May not strictly be necessary and may throw a harmless error
                                                        // if the watchdog has already started
    esp_task_wdt_reconfigure(&watchdog_config);     //Reconfigure task using configuration.  This ~IS~ necessary because the watchdog may have already been started
                                                        //with undesireable parameters.
    esp_task_wdt_add(xTaskGetCurrentTaskHandle());  //Add the current task to the watchdog.  This is necessary for the "feeding" to function
    
    esp_task_wdt_reset();                           //Feed the watchdog timer by calling esp_task_wdt_reset() periodically
                                                        //more frequently than the "timeout_ms" amount of time



    int32_t * wave = calloc (1, sizeof(int32_t)*(int32_t)WAVEFORM_LEN);
    double * wave_double = calloc(1, sizeof(double)*(int32_t)WAVEFORM_LEN);
    // Create the sine wave
    create_sine_wave(wave, wave_double, FREQUENCY);


    // Do something with the generated sine wave
    // for (int i = 0; i < WAVEFORM_LEN; i++) {
    //     if (i%10==0){
    //     printf("wave[%i] = %ld\n", i, wave[i]);
    //     printf("Double wave[%i] = %0.5f\n", i, wave_double[i]); 
    //     }
    //     if (esp_task_wdt_reset()==ESP_OK){   //Feed the watchdog timer using esp_task_wdt_reset()
    //     // printf("successfully reset\n");
    //     }   
    //     // else
    //         //  printf("Reset failure\n");
    // }

    int size1 = sizeof(*wave);
    int len1 = size1 / sizeof((wave[0]));    
    // printf("Waveform size: %i bytes\nWaveform Length: %i samples\n:Allocated Space: %i bytes\n", size1, len1, sizeof(int32_t)*WAVEFORM_LEN);

    // printf("Wave of idx 295 = %ld", wave[295]);

    // printf("Waveform value:");
    i2s_example_init_std_simplex();

    /* Step 3: Create writing and reading task, enable and start the channels */

    xTaskCreate(i2s_example_write_task, "i2s_example_write_task", 65536, wave, 5, NULL);

    while(1){
        if (esp_task_wdt_reset()==ESP_OK){   //Feed the watchdog timer using esp_task_wdt_reset()
        vTaskDelay(pdMS_TO_TICKS(1000));
            }   
    }

}