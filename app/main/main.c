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

#include "driver/ledc.h"
#include "driver/rmt_tx.h"
#include "driver/gptimer.h"
#include <driver/ledc.h>

#include "stp_sd_sdcardops.h"
#include "stp_audio__audio_ops.h"
#include "stp_adc.h"


//SD Card operation defines
#define AUDIO_FILENAME "/SINEA.wav"
// #define AUDIO_FILENAME "/WHITE.wav"

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
//#define CH_EN_PIN   GPIO_NUM_21     //H-bridge enable pin

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

//PWM Pin defines QUOTE: https://randomnerdtutorials.com/esp-idf-esp32-gpio-pwm-ledc/
#define CH1_CURSET_PIN  GPIO_NUM_2
#define CH1_CURSET_CHANNEL LEDC_CHANNEL_0
#define CH1_CURSET_TIMER LEDC_TIMER_0
#define CH1_CURSET_MODE LEDC_LOW_SPEED_MODE
#define CH1_CURSET_DUTY_RES LEDC_TIMER_8_BIT 
#define CH1_CURSET_FREQUENCY 200000             // 1 kHz PWM frequency

#define CH2_CURSET_PIN  GPIO_NUM_47

//ENDQUOTE

//RMT Pin defines
#define CH1_SW_OUTA_PIN     GPIO_NUM_8  //CH1_SW_OUT+   //TODO: This should be pin 9
#define CH1_SW_OUTB_PIN     GPIO_NUM_21  //CH1_SW_OUT-   //   Using the ch_en pin of GPIO21, GPIO9 is burnt out
#define CH2_SW_OUTA_PIN     GPIO_NUM_36 //CH2_SW_OUT+
#define CH2_SW_OUTB_PIN     GPIO_NUM_37 //CH2_SW_OUT-

//I2S defines
#define I2S_WS_PIN   GPIO_NUM_4      //LCK, LRC, 13 I2S word select io number
#define I2S_DOUT_PIN GPIO_NUM_5      //DIN, 12 I2S data out io number
#define I2S_BCK_PIN  GPIO_NUM_6      //BCK 11  I2S bit clock io number

#define SAMPLE_RATE    96000
#define NUM_DMA_BUFF   8
#define SIZE_DMA_BUFF  500         //can go up to 500

#define MAX_VOL_DBFS            -20
#define MIN_VOL_DBFS            -60

#define AUDIO_PLAY_INTERVAL_MS              1000
#define PCM5102A_LATENCY_COMPENSATION_US    4975                    
#define PRE_DITHER_MS                       0

#define RMT_PLAY_DELAY_MS                       10+PCM5102A_LATENCY_COMPENSATION_US/1000

#define ADC_UPDATE_PERIOD_MS    100
#define PRINT_UPDATE_PERIOD_MS  100
#define BACKGROUND_TASK_PRIORTIY 1
#define AUDIO_TASK_PRIORITY      5
#define RMT_TASK_PRIORITY        7

#define RMT_TASK_CORE            1
#define AUDIO_TASK_CORE          0

typedef struct {
    SemaphoreHandle_t         adc_mutex;
    stp_adc__adc_chan_results adc_results;
} Adc_Update_Struct;

typedef struct {
    stp_audio__i2s_config* i2s_config_ptr;
    stp_sd__audio_chunk* audio_chunk_ptr;
    stp_sd__wavFile* wave_file_ptr;
    Adc_Update_Struct* adc_update_struct_ptr;
    TaskHandle_t rmt_task_to_notify;
    QueueHandle_t rmt_start_queue;
    bool (*rmt_callback_func_ptr)(struct gptimer_t *, const gptimer_alarm_event_data_t *, void *);
} Audio_Task_Setup_Struct;

void flash_lights(void * pvParameters){

    gpio_set_direction(XSMT_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED1_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED2_PIN, GPIO_MODE_OUTPUT);

    gpio_set_level(LED1_PIN, 0);
    gpio_set_level(LED2_PIN, 0);
    gpio_set_level(XSMT_PIN, 0);

    gpio_set_direction(LED2_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(LED1_PIN, 0);


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
        .ch1_adc_unit  = CH2_ADC_UNIT,  //TODO fix
        .ch1_adc_chan  = CH2_ADC_CHAN,  //TODO fix
        .ch2_adc_unit  = CH2_ADC_UNIT,
        .ch2_adc_chan  = CH2_ADC_CHAN,
        .batt_adc_unit = BATT_ADC_UNIT,
        .batt_adc_chan = BATT_ADC_CHAN,
        .hv_adc_unit   = HVDIV_ADC_UNIT,
        .hv_adc_chan   = HVDIV_ADC_CHAN,
    };

    stp_adc__adc_chan_struct adc_chan_struct;
    stp_adc__setup_adc_chans(adc_chan_setup, &adc_chan_struct);

    //QUOTE: https://randomnerdtutorials.com/esp-idf-esp32-gpio-pwm-ledc/
    ledc_timer_config_t ledc_timer = {
        .speed_mode = CH1_CURSET_MODE,
        .duty_resolution = CH1_CURSET_DUTY_RES,
        .timer_num = CH1_CURSET_TIMER,
        .freq_hz = CH1_CURSET_FREQUENCY
    };
    ledc_timer_config(&ledc_timer);

    // Configure LEDC channel
    ledc_channel_config_t ledc_channel = {
        .gpio_num = CH1_CURSET_PIN,
        .speed_mode = CH1_CURSET_MODE,
        .channel = CH1_CURSET_CHANNEL,
        .timer_sel = CH1_CURSET_TIMER,
        .duty = 0
    };
    ledc_channel_config(&ledc_channel);
    //ENDQUOTE
    int ch1_duty = 0;
    int ch1_duty_bits = pow(2, CH1_CURSET_DUTY_RES)-1;
    double ch1_percent = 0;
    while (true)
    {
        if(xSemaphoreTake(adc_update_struct->adc_mutex, portMAX_DELAY)==pdTRUE)
        {
            stp_adc__read_all_adc_chans(&adc_chan_struct, &(adc_update_struct->adc_results));
            ch1_percent = adc_update_struct->adc_results.ch1_percent;
            xSemaphoreGive(adc_update_struct->adc_mutex);
            // if(ch1_percent < 40) ch1_percent = 40;
            ch1_duty = ch1_percent/100*(ch1_duty_bits);
            ledc_set_duty(CH1_CURSET_MODE, CH1_CURSET_CHANNEL, ch1_duty);
            ledc_update_duty(CH1_CURSET_MODE, CH1_CURSET_CHANNEL);
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
            xSemaphoreGive(adc_update_struct->adc_mutex);

            printf("\rVol: %.0f%% | Ch2: %0.0f%% | Ch1: %0.0f%% |Batt: %.1fV | HV: %.1fV  ",
                adc_results.vol_percent,
                adc_results.ch2_percent,
                adc_results.ch1_percent,
                adc_results.batt_voltage,
                adc_results.hv_voltage);
        }
        vTaskDelay(pdMS_TO_TICKS(PRINT_UPDATE_PERIOD_MS));
    }
}

static bool IRAM_ATTR start_audio_GPTimer_callback(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx)
{
    Audio_GPTimer_Args_Struct* data_ptr = (Audio_GPTimer_Args_Struct*)user_ctx;
    gptimer_start(data_ptr->rmt_gptimer_hdnl); //immediately start timer to run RMT.. There seems to be an issue with recycling this timer for repeated use
    i2s_channel_enable_from_ISR(data_ptr->tx_chan);             //start audio playing
    QueueHandle_t audio_play_queue = data_ptr->audio_start_queue;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    int8_t item_to_queue = 0;
    xQueueSendFromISR(audio_play_queue, &item_to_queue, &xHigherPriorityTaskWoken);

    return 0;
}

void play_audio_Task(void* pvParameters)
{
    char* TAG = "play_audio_task";
    Audio_Task_Setup_Struct* task_setup_struct_ptr = (Audio_Task_Setup_Struct*)pvParameters;
    stp_audio__i2s_config* i2s_config_ptr       = task_setup_struct_ptr->i2s_config_ptr;
    stp_sd__audio_chunk* audio_chunk_ptr        = task_setup_struct_ptr->audio_chunk_ptr;
    stp_sd__wavFile* wave_file_ptr              = task_setup_struct_ptr->wave_file_ptr;
    Adc_Update_Struct* adc_update_struct_ptr    = task_setup_struct_ptr->adc_update_struct_ptr;

    gptimer_handle_t audio_gptimer_hndl = NULL;
    gptimer_config_t timer_config = {
                                    .clk_src = GPTIMER_CLK_SRC_DEFAULT,
                                    .direction = GPTIMER_COUNT_UP,
                                    .resolution_hz = 1e6, // 1MHz, 1 tick = 1us
                                };
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &audio_gptimer_hndl));
    gptimer_alarm_config_t alarm_config = {
                                        .reload_count = 0, // counter will reload with 0 on alarm event
                                        .alarm_count = AUDIO_PLAY_INTERVAL_MS * 1000,
                                        .flags.auto_reload_on_alarm = true, // enable auto-reload
                                    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(audio_gptimer_hndl, &alarm_config));
    gptimer_event_callbacks_t cbs = {
        .on_alarm = start_audio_GPTimer_callback, // register user callback
    };

    QueueHandle_t start_audio_play_queue;
    start_audio_play_queue = xQueueCreate(1, sizeof(int8_t));
    if(start_audio_play_queue == NULL) ESP_LOGE(TAG, "Error creating audio play queue!");

    gptimer_handle_t rmt_gptimer = NULL;
    gptimer_config_t rmt_timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT, // Select the default clock source
        .direction = GPTIMER_COUNT_UP,      // Counting direction is up
        .resolution_hz = 1e6,   // Resolution is 1 MHz, i.e., 1 tick equals 1 microsecond
    };
    // Create a timer instance
    ESP_ERROR_CHECK(gptimer_new_timer(&rmt_timer_config, &rmt_gptimer));

    gptimer_alarm_config_t rmt_alarm_config = {
                                            .reload_count = 0, // counter will reload with 0 on alarm event
                                            .alarm_count = RMT_PLAY_DELAY_MS * 1000, // Set the actual alarm period, since the resolution is 1us, 1000000 represents 1s
                                            .flags.auto_reload_on_alarm = true, // Disable auto-reload function
                                        };

    ESP_ERROR_CHECK(gptimer_set_alarm_action(rmt_gptimer, &rmt_alarm_config));

    gptimer_event_callbacks_t rmt_gptimer_callback = {
                                    .on_alarm = task_setup_struct_ptr->rmt_callback_func_ptr, // Call the user callback function when the alarm event occurs
                                };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(rmt_gptimer, &rmt_gptimer_callback, task_setup_struct_ptr));
    // Enable the timer

    Audio_GPTimer_Args_Struct timer_cb_args = {
        .tx_chan = i2s_config_ptr->tx_chan,
        .audio_start_queue = start_audio_play_queue,
        .rmt_gptimer_hdnl = rmt_gptimer,
    };

    ESP_ERROR_CHECK(gptimer_register_event_callbacks(audio_gptimer_hndl, &cbs, &timer_cb_args));
    gptimer_enable(rmt_gptimer);

    ESP_ERROR_CHECK(gptimer_enable(audio_gptimer_hndl));
    ESP_ERROR_CHECK(gptimer_start(audio_gptimer_hndl));


    gpio_set_level(XSMT_PIN, 1);

    // while (true)
    // {
        bool use_random = false;
        ESP_ERROR_CHECK(stp_sd__get_new_audio_chunk(audio_chunk_ptr, wave_file_ptr, use_random));

        if(xSemaphoreTake(adc_update_struct_ptr->adc_mutex, portMAX_DELAY) != pdTRUE) ESP_LOGE(TAG, "Error taking adc mutex for volume!");
        double vol_set_perc = (adc_update_struct_ptr->adc_results).vol_percent;
        xSemaphoreGive(adc_update_struct_ptr->adc_mutex);

        ESP_ERROR_CHECK(stp_audio__preload_buffer(i2s_config_ptr, audio_chunk_ptr, vol_set_perc));
        int8_t queue_receive_num = 0; //placeholder for dummy item received from queue.
        if(xQueueReceive(start_audio_play_queue, &queue_receive_num, portMAX_DELAY) == pdTRUE)
        {
            i2s_channel_finish_enabling_after_ISR(i2s_config_ptr->tx_chan);
            // printf("Audio playing\n");
            ESP_ERROR_CHECK(stp_audio__play_audio_chunk(i2s_config_ptr, audio_chunk_ptr, vol_set_perc));
            vTaskDelay(pdMS_TO_TICKS(50));
            ESP_ERROR_CHECK(stp_audio__i2s_channel_disable(i2s_config_ptr));
        }
        else{
            ESP_LOGE(TAG, "Play queue failure!\n\n");
        }
    // }
    printf("Exiting Audio Task\n");
    vTaskDelete(NULL);
}


static bool IRAM_ATTR start_RMT_output_gptimer_cb(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx)
{
    //This is linked to the gptimer in the audio play task
    Audio_Task_Setup_Struct* task_setup_struct_ptr = (Audio_Task_Setup_Struct*)user_ctx;
    xTaskNotifyFromISR(task_setup_struct_ptr->rmt_task_to_notify, 0, eNoAction, NULL);
    gptimer_stop(timer);
    return false;
}

void run_rmt_Task(void* pvParameters)
{
    char* TAG = "run_rmt_Task";
    
    rmt_channel_handle_t ch1_phaseA_rmt_chan = NULL;
    rmt_tx_channel_config_t ch1_phaseA_rmt_chan_config = {
                                                        .clk_src = RMT_CLK_SRC_DEFAULT,   // select source clock
                                                        .gpio_num = CH1_SW_OUTA_PIN,                    // GPIO number
                                                        .mem_block_symbols = 64,          // memory block size, 64 * 4 = 256 Bytes
                                                        .resolution_hz = 1e6,             // 1 MHz tick resolution, i.e., 1 tick = 1 µs
                                                        .trans_queue_depth = 4,           // set the number of transactions that can pend in the background
                                                        .flags.invert_out = false,        // do not invert output signal
                                                        .flags.with_dma = false,          // do not need DMA backend
                                                        };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&ch1_phaseA_rmt_chan_config, &ch1_phaseA_rmt_chan));

    rmt_copy_encoder_config_t ch1_pha_copy_encoder_cfg = {};
    rmt_encoder_handle_t ch1_pha_copy_encoder = NULL;
    ESP_ERROR_CHECK(rmt_new_copy_encoder(&ch1_pha_copy_encoder_cfg, &ch1_pha_copy_encoder)); // copies symbols as-is

    rmt_symbol_word_t channel1_pha_symbols[] = {
        {
            .level0 = 0, .duration0 = 1,   
            .level1 = 0, .duration1 = 2,
        },
        {
            .level0 = 1, .duration0 = 150,   
            .level1 = 0, .duration1 = 150,
        },
        {
            .level0 = 0, .duration0 = 350,   
            .level1 = 0, .duration1 = 350,
        },
        {
            .level0 = 1, .duration0 = 150,   
            .level1 = 0, .duration1 = 150,
        },
        {
            .level0 = 0, .duration0 = 350,   
            .level1 = 0, .duration1 = 350,
        },
        {
            .level0 = 1, .duration0 = 150,   
            .level1 = 0, .duration1 = 150,
        },
    };

    rmt_channel_handle_t ch1_phaseB_rmt_chan = NULL;
    rmt_tx_channel_config_t ch1_phaseB_rmt_chan_config = {
                                                        .clk_src = RMT_CLK_SRC_DEFAULT,   // select source clock
                                                        .gpio_num = CH1_SW_OUTB_PIN,                    // GPIO number
                                                        .mem_block_symbols = 64,          // memory block size, 64 * 4 = 256 Bytes
                                                        .resolution_hz = 1e6,             // 1 MHz tick resolution, i.e., 1 tick = 1 µs
                                                        .trans_queue_depth = 4,           // set the number of transactions that can pend in the background
                                                        .flags.invert_out = false,        // do not invert output signal
                                                        .flags.with_dma = false,          // do not need DMA backend
                                                        };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&ch1_phaseB_rmt_chan_config, &ch1_phaseB_rmt_chan));

    rmt_copy_encoder_config_t ch1_phb_copy_encoder_cfg = {};
    rmt_encoder_handle_t ch1_phb_copy_encoder = NULL;
    ESP_ERROR_CHECK(rmt_new_copy_encoder(&ch1_phb_copy_encoder_cfg, &ch1_phb_copy_encoder)); // copies symbols as-is

    rmt_symbol_word_t channel1_phb_symbols[] = {
        {
            .level0 = 0, .duration0 = 150,   
            .level1 = 1, .duration1 = 150,
        },
        {
            .level0 = 0, .duration0 = 350,   
            .level1 = 0, .duration1 = 350,
        },
        {
            .level0 = 0, .duration0 = 150,   
            .level1 = 1, .duration1 = 150,
        },
        {
            .level0 = 0, .duration0 = 350,   
            .level1 = 0, .duration1 = 350,
        },
        {
            .level0 = 0, .duration0 = 150,   
            .level1 = 1, .duration1 = 150,
        },
    };


    rmt_transmit_config_t tx_config_ch1A = {
        .loop_count = 0, // no transfer loop
    };

    rmt_transmit_config_t tx_config_ch1B = {
        .loop_count = 0, // no transfer loop
    };


    size_t sizeof_rmt_preload_struct = stp__rmt_get_size_of_rmt_preload_struct();
    stp__rmt_preload_struct *rmt_preload_struct_ch1_pha_ptr = malloc(sizeof_rmt_preload_struct);
    if(rmt_preload_struct_ch1_pha_ptr == NULL) ESP_LOGE(TAG, "Error allocating memory for RMT preload struct ch1 phA!");
    memset(rmt_preload_struct_ch1_pha_ptr, 0, sizeof_rmt_preload_struct);

    stp__rmt_preload_struct *rmt_preload_struct_ch1_phb_ptr = malloc(sizeof_rmt_preload_struct);
    if(rmt_preload_struct_ch1_phb_ptr == NULL) ESP_LOGE(TAG, "Error allocating memory for RMT preload struct ch1 phB!");
    memset(rmt_preload_struct_ch1_phb_ptr, 0, sizeof_rmt_preload_struct);

    while(true)
    {
        ESP_ERROR_CHECK(rmt_enable(ch1_phaseA_rmt_chan));
        stp__rmt_transmit_preload(ch1_phaseA_rmt_chan, ch1_pha_copy_encoder, channel1_pha_symbols, sizeof(channel1_pha_symbols), &tx_config_ch1A, rmt_preload_struct_ch1_pha_ptr);

        ESP_ERROR_CHECK(rmt_enable(ch1_phaseB_rmt_chan));
        stp__rmt_transmit_preload(ch1_phaseB_rmt_chan, ch1_phb_copy_encoder, channel1_phb_symbols, sizeof(channel1_phb_symbols), &tx_config_ch1B, rmt_preload_struct_ch1_phb_ptr);

        if(xTaskNotifyWait(ULONG_MAX, ULONG_MAX, NULL, pdMS_TO_TICKS(10000)) == pdTRUE)
        {
            stp__rmt_do_transaction(rmt_preload_struct_ch1_pha_ptr);         //There is an issue with the rmt_do_transaction function.  Seemingly it doesn't have a good handle on which channel to turn on
            stp__rmt_do_transaction(rmt_preload_struct_ch1_phb_ptr);
            vTaskDelay(pdMS_TO_TICKS(150));
            ESP_ERROR_CHECK(rmt_disable(ch1_phaseA_rmt_chan));
            ESP_ERROR_CHECK(rmt_disable(ch1_phaseB_rmt_chan));

        }
        else
        {
            ESP_LOGE(TAG, "RMT do transaction didn't work!");
            ESP_ERROR_CHECK(rmt_disable(ch1_phaseA_rmt_chan));
            ESP_ERROR_CHECK(rmt_disable(ch1_phaseB_rmt_chan));

        }
    }
}


void app_main(void)
{
    char* TAG = "main";

    printf("\33[?25l"); //Hide terminal cursor, quoted from https://stackoverflow.com/questions/30126490/how-to-hide-console-cursor-in-c;

    // gpio_set_direction(CH1_SW_OUTB_PIN, GPIO_MODE_OUTPUT);
    // gpio_set_level(CH1_SW_OUTB_PIN, 1);
    // gpio_set_direction(CH1_SW_OUTA_PIN, GPIO_MODE_OUTPUT);
    // gpio_set_level(CH1_SW_OUTA_PIN, 0);

    stp_sd__spi_config spi_config = {
        .open        = false,
        .mosi_di_pin = PIN_NUM_MOSI,
        .miso_do_pin = PIN_NUM_MISO,
        .clk_pin     = PIN_NUM_CLK,
        .cs_pin      = PIN_NUM_CS,
        .mount_point = "/sdcard",
        .card_ptr    = NULL,
    };

    if(stp_sd__mount_sd_card(&spi_config) != ESP_OK)
    {
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
    int flash_light_task_priority = BACKGROUND_TASK_PRIORTIY;
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
    int adc_task_priority = BACKGROUND_TASK_PRIORTIY;
    adc_task_created = xTaskCreate(update_adc, "Update ADC Values", 4096, &adc_update_struct, adc_task_priority, &update_adc_task);
    if(adc_task_created != pdPASS){
        ESP_LOGE(TAG, "Error creating adc update task!");
        return;
    }

    BaseType_t print_task_created;
    TaskHandle_t update_print_task = NULL;
    int print_task_priority = BACKGROUND_TASK_PRIORTIY;
    print_task_created = xTaskCreate(print_to_terminal, "Print to Terminal", 4096, &adc_update_struct, print_task_priority, &update_print_task);
    if(print_task_created != pdPASS){
        ESP_LOGE(TAG, "Error creating print to terminal update task!");
        return;
    }

    stp_sd__wavFile wave_file = {
      .filename = AUDIO_FILENAME,
    };  
    if(stp_sd__open_audio_file(&wave_file) != ESP_OK)
    {
        ESP_LOGE(TAG, "Error Opening Audio File!");
        return;
    }

    QueueHandle_t reload_audio_buff_Queue;
    reload_audio_buff_Queue = xQueueCreate(1, sizeof(stp_sd__reload_memory_data_struct));
    if(reload_audio_buff_Queue == NULL) ESP_LOGE(TAG, "Error creating audio reload queue!");

    stp_sd__reload_memory_Task_struct reload_mem_Task_struct = {
        .reload_audio_buff_Queue = reload_audio_buff_Queue,
        .wave_file_ptr = &wave_file,
    };

    BaseType_t reload_audio_memory_buff_task_created;
    TaskHandle_t reload_audio_memory_buff_task = NULL;
    int reload_audio_memory_buff_task_priority = AUDIO_TASK_PRIORITY;
    reload_audio_memory_buff_task_created = xTaskCreatePinnedToCore(stp_sd__threadsafe_reload_chunk_memory_buffer_Task, "Reload Audio Memory Buffer", 4096, &reload_mem_Task_struct, reload_audio_memory_buff_task_priority, &reload_audio_memory_buff_task, AUDIO_TASK_CORE);
    if(reload_audio_memory_buff_task_created != pdPASS)
    {
        ESP_LOGE(TAG, "Error creating reload audio buffer update task!");
        return;
    }
    
    stp_sd__reload_memory_data_struct reload_memory_struct = {};    //This is the data passed from the play_audio_chunk function to the reload_memory task
                                                                    //It's defined here to be in both tasks scope.
    stp_sd__audio_chunk_setup audio_chunk_setup = {
        .chunk_len_wo_dither        = 9600,    //REQUIRED INPUT: length of chunk in number of samples, not including dither
        .rise_fall_num_samples      = 0,       //REQUIRED INPUT: Number of samples to apply rise/fall scaling to (nominally 96 [1ms @ 96000Hz]) at the beginning and end of the chunk
        .padding_num_samples        = 100,     //REQUIRED INPUT: Number of samples to offset from the beginning and end of the audio data
        .pre_dither_num_samples     = PRE_DITHER_MS/1000*96000,     //REQUIRED INPUT: Number of samples of dither to append to the beginning and end of the audio file (to appease the PCM5102a chip we are using)
        .post_dither_num_samples    = 384,
        .chunk_buf_size_bytes       = NUM_DMA_BUFF*SIZE_DMA_BUFF*sizeof(int32_t)*2, //Factor of four is needed to match i2s buff size.  +1 is added to make the buffer bigger just in case
        .wavFile_ptr                = &wave_file,
        .reload_audio_buff_Queue    = reload_audio_buff_Queue,
        .reload_memory_struct_ptr   = &reload_memory_struct,
        .audio_sample_rate          = SAMPLE_RATE,
    };

    stp_sd__audio_chunk audio_chunk = {};
    stp_sd__init_audio_chunk(&audio_chunk_setup, &audio_chunk);

    stp_audio__i2s_config i2s_config = {
                                        .buf_capacity             = 0,
                                        .buf_len                  = 0,
                                        .num_dma_buf              = NUM_DMA_BUFF,
                                        .size_dma_buf             = SIZE_DMA_BUFF,
                                        .ms_delay_between_writes  = 0,
                                        .bclk_pin                 = I2S_BCK_PIN,    
                                        .ws_pin                   = I2S_WS_PIN,
                                        .dout_pin                 = I2S_DOUT_PIN,
                                        .sample_rate_Hz           = SAMPLE_RATE,
                                        .max_vol_dBFS             = MAX_VOL_DBFS,
                                        .min_vol_dBFS             = MIN_VOL_DBFS,
                                        .set_vol_percent          = 10,
                                        .vol_scale_factor         = 0,
                                        .min_vol_percent          = 2,
                                        .actual_dbFS              = 0,
                                        .preloaded                = false
                                        };
    ESP_ERROR_CHECK(stp_audio__i2s_channel_setup(&i2s_config));

    QueueHandle_t rmt_start_queue;
    rmt_start_queue = xQueueCreate(1, sizeof(int8_t));
    if(rmt_start_queue == NULL) ESP_LOGE(TAG, "Error creating audio play queue!");

    BaseType_t run_rmt_task_created;
    TaskHandle_t run_rmt_task_hndl = NULL;
    int run_rmt_task_priority = RMT_TASK_PRIORITY;
    run_rmt_task_created = xTaskCreatePinnedToCore(run_rmt_Task, "Run RMT Task", 4096, &rmt_start_queue, run_rmt_task_priority, &run_rmt_task_hndl, RMT_TASK_CORE);
    if(run_rmt_task_created != pdPASS)
    {
        ESP_LOGE(TAG, "Error creating run RMT task!");
        return;
    }

    Audio_Task_Setup_Struct play_audio_task_struct = {
        .i2s_config_ptr  = &i2s_config,
        .audio_chunk_ptr = &audio_chunk,
        .wave_file_ptr = &wave_file,
        .adc_update_struct_ptr = &adc_update_struct,
        .rmt_task_to_notify = run_rmt_task_hndl,
        .rmt_start_queue = rmt_start_queue,
        .rmt_callback_func_ptr = start_RMT_output_gptimer_cb,
    };
    
    BaseType_t play_audio_task_created;
    TaskHandle_t play_audio_task_hndl = NULL;
    int play_audio_task_priority = AUDIO_TASK_PRIORITY;
    play_audio_task_created = xTaskCreatePinnedToCore(play_audio_Task, "Play Audio Task", 4096, &play_audio_task_struct, play_audio_task_priority, &play_audio_task_hndl, AUDIO_TASK_CORE);
    if(play_audio_task_created != pdPASS)
    {
        ESP_LOGE(TAG, "Error creating audio play task!");
        return;
    }

    
    
    for(int i=0; i<20; i++)
    { 

        vTaskDelay(pdMS_TO_TICKS(500));
    }

    esp_err_t stp_audio__i2s_channel_disable(stp_audio__i2s_config* i2s_config_ptr);

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

        for(int i=0; i<800; i++)
    { 

        vTaskDelay(pdMS_TO_TICKS(500));
    }

    return;
}