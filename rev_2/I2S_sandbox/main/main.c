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

#define SAMPLE_RATE                 160000
#define DURATION_MS                 30
#define WAVEFORM_LEN                SAMPLE_RATE/1000*DURATION_MS
#define NUM_DMA_BUFF                8
#define SIZE_DMA_BUFF               1023
#define I2S_BUFF_SIZE               NUM_DMA_BUFF * SIZE_DMA_BUFF

static i2s_chan_handle_t                tx_chan;        // I2S tx channel handler

int i;

// const int SAMPLE_RATE = 16000;
// const int DURATION_MS = 10;
int FREQUENCY          = 1000;

//TESTING GIT BRANCHING



void create_sine_wave(int32_t * waveform, double * waveform_double, int FREQUENCY) {
    printf("WAVEFORM LEN: %i\n", WAVEFORM_LEN);


    // Populate the waveform array with sine values
    for (int i = 0; i < WAVEFORM_LEN; i++) {
        double timestep = (double)i / (double)SAMPLE_RATE;
        double volume = (double)VOL_PERCENT / (double)100;                  //Operands must be cast to double for this to work.
        // printf("Volume: %0.10f\n", volume);
        double sine_point = volume * sin(2 * M_PI * FREQUENCY * timestep);  // Use 2 * PI for full sine wave cycle
        int32_t int_point = (int32_t)(sine_point*BITS_IN_32BIT);
        waveform_double[i] = sine_point;

        (waveform[i]) = int_point;
    }


    printf("Made it through sine wave function\n");

}


static void i2s_write_function(void *waveform)
{    
    printf("\n5\n");

    int32_t *audio_waveform = (int32_t*)waveform;           //Cast the waveform argumen to a 32-bit int pointer
    printf("\n6\n");

    size_t WAVEFORM_SIZE = (int32_t)WAVEFORM_LEN * sizeof(int32_t);
    printf("Here");

    int32_t *w_buf = (int32_t *)calloc(sizeof(int32_t), WAVEFORM_LEN);   //Allocate memory for the I2S write buffer
    assert(w_buf);                                          //Check if buffer was allocated successfully
    size_t w_bytes = I2S_BUFF_SIZE;                         //Create variable to track how many bytes are written to the I2S DMA buffer
    size_t audio_samples_pos = 0;                           // Keep track of where we are in the audio data


    /*Here we iterate through each index in the audio waveform, and assign the value to the wbuf*/
    while (audio_samples_pos<WAVEFORM_LEN) {
        w_buf[audio_samples_pos] = (audio_waveform[audio_samples_pos]);
        audio_samples_pos++;
        }

    /* Here we load the target buffer repeatedly, until all the DMA buffers are preloaded 
    This function returns the number of bytes written to the I2S buffer to the wbytes variable.
    When the buffer is almost full, wbytes will be less than I2S BUFF SIZE, because there is not enough space to write the
    entire data length.  We exit the loop at that point.  This would be more useful if len(wbuf) > I2S BUFF Size, and is not necessary here*/
    // while (w_bytes == I2S_BUFF_SIZE) {
    //     ESP_ERROR_CHECK(i2s_channel_preload_data(tx_chan, w_buf, WAVEFORM_SIZE, &w_bytes));
    // }

    /*Here, we initialize our time-tracking and index-tracking variables*/
    float start_time_us = (float)esp_timer_get_time();
    printf("Time Of Loop Start: %0.6f ms", start_time_us/(float)1000);
    float curr_time_us = start_time_us;
    float last_time_us = curr_time_us;
    float period_us    = 0;
    int idx = 0;

    /*This begins the sound-writing loop, which is limited to 300 iterations for testing purposes*/
    while(idx < 300){           
        
        /*The I2S channel is enabled and disabled every loop.  This has been found to enable the most repeatable results*/
        ESP_ERROR_CHECK(i2s_channel_enable(tx_chan));       

        /*Find current time and period of last loop*/
        curr_time_us = (float)(esp_timer_get_time()) - start_time_us;   
        period_us = curr_time_us - last_time_us;
        last_time_us = curr_time_us;

        /*Iterate through and write wbuf to I2S DMA buffer.  If len(wbuf) were > than I2S buff size, 
        we would use the wbytes variable to move along wbuf and start a new write at the position where the 
        last one left off.  That's not the case here, though*/
        for (int tot_bytes = 0; tot_bytes < WAVEFORM_SIZE; tot_bytes += w_bytes){

            i2s_channel_write(tx_chan, w_buf, WAVEFORM_SIZE, &w_bytes, DURATION_MS);

        };

        printf("Current Loop Time: %0.1f ms\n", curr_time_us/1000);
        printf("Current Loop Period: %0.6f ms\n", period_us/1000);

        ESP_ERROR_CHECK(i2s_channel_disable(tx_chan));      //Disable channel each loop, as described above
        
        w_bytes = I2S_BUFF_SIZE;
        while (w_bytes == I2S_BUFF_SIZE) {                  //Optionally pre-load buffer for next loop.
        ESP_ERROR_CHECK(i2s_channel_preload_data(tx_chan, w_buf, WAVEFORM_SIZE, &w_bytes));
        }

        vTaskDelay(pdMS_TO_TICKS(200));                      //This delay was found empirically to be required to enable a 5Hz frequency.
        idx++;

    }


    // free(w_buf);
    
    // vTaskDelete(NULL);
    printf("Loop Ended\n");
    vTaskDelay(pdMS_TO_TICKS(10000));

    }


static void i2s_channel_setup(void)
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
        .auto_clear_before_cb = true,
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


    printf("\n1\n");

    int32_t *wave = calloc(WAVEFORM_LEN, sizeof(int32_t));
    double *wave_double = calloc(WAVEFORM_LEN, sizeof(double));
    // Create the sine wave
    printf("\n2\n");

    create_sine_wave(wave, wave_double, FREQUENCY);
    printf("\n3\n");


    i2s_channel_setup();
    printf("\n4\n");

    /* Step 3: Create writing and reading task, enable and start the channels */

    // xTaskCreate(i2s_example_write_task, "i2s_example_write_task", 65536, wave, 5, NULL);

    i2s_write_function(wave);        //wave is the pointer to the array which contains the sine wave

    while(1){
        if (esp_task_wdt_reset()==ESP_OK){   //Feed the watchdog timer using esp_task_wdt_reset()
        vTaskDelay(pdMS_TO_TICKS(1000));
            }   
    }

}