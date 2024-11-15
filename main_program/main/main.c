#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/rmt_tx.h"
#include "driver/gptimer.h"
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





//I2S DEFINITIONS__________________________________________________________________I2S START_______________________I2S START___________________________________


#define _USE_MATH_DEFINES

/* Set 1 to allocate rx & tx channels in duplex mode on a same I2S controller, they will share 
the BCLK and WS signal
 * Set 0 to allocate rx & tx channels in simplex mode, these two channels will be totally separated,
 * Specifically, due to the hardware limitation, the simplex rx & tx channels can't be registered on the same controllers on ESP32 and ESP32-S2,
 * and ESP32-S2 has only one I2S controller, so it can't allocate two simplex channels */
#define EXAMPLE_I2S_DUPLEX_MODE         CONFIG_USE_DUPLEX

#define EXAMPLE_STD_WS_IO1          13      // I2S word select io number
#define EXAMPLE_STD_DOUT_IO1        12     // I2S data out io number
#define EXAMPLE_STD_BCLK_IO1        11      // I2S bit clock io number

#define BITS_IN_32BIT               2147483647
#define VOL_PERCENT                 10

#define SAMPLE_RATE                 96000
#define DURATION_MS                 50
#define WAVEFORM_LEN                SAMPLE_RATE/1000*DURATION_MS
#define NUM_DMA_BUFF                6
#define SIZE_DMA_BUFF               800
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


static void i2s_write_function(void *waveform)
{    
    // printf("\n5\n");

    int32_t *audio_waveform = (int32_t*)waveform;           //Cast the waveform argumen to a 32-bit int pointer
    // printf("\n6\n");

    size_t WAVEFORM_SIZE = (int32_t)WAVEFORM_LEN * sizeof(int32_t);
    // printf("Here");

    int32_t *w_buf = (int32_t *)calloc(sizeof(int32_t), WAVEFORM_LEN);   //Allocate memory for the I2S write buffer
    assert(w_buf);                                          //Check if buffer was allocated successfully
    size_t w_bytes = I2S_BUFF_SIZE;                         //Create variable to track how many bytes are written to the I2S DMA buffer
    size_t audio_samples_pos = 0;                           // Keep track of where we are in the audio data


    /*Here we iterate through each index in the audio waveform, and assign the value to the wbuf*/
    while (audio_samples_pos<WAVEFORM_LEN) {
        w_buf[audio_samples_pos] = (audio_waveform[audio_samples_pos]);
        audio_samples_pos++;
        }


    /*Here, we initialize our time-tracking and index-tracking variables*/
    float start_time_us = (float)esp_timer_get_time();
    // printf("Time Of Loop Start: %0.6f ms", start_time_us/(float)1000);
    float curr_time_us = start_time_us;
    float last_time_us = curr_time_us;
    float period_us    = 0;
    int idx = 0;

    /*This begins the sound-writing loop, which is limited to 300 iterations for testing purposes*/
    // while(idx < 300){           

    /*The I2S channel is enabled and disabled every loop.  This has been found to enable the most repeatable results*/
    // ESP_ERROR_CHECK(i2s_channel_enable(tx_chan));       

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

    // printf("Current Loop Time: %0.1f ms\n", curr_time_us/1000);
    // printf("Current Loop Period: %0.6f ms\n", period_us/1000);

    // ESP_ERROR_CHECK(i2s_channel_disable(tx_chan));      //Disable channel each loop, as described above
    
    idx++;


    free(w_buf);
    printf("Loop Ended\n");
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
    printf("\n20\n");

    i2s_std_config_t tx_std_cfg = {
    .clk_cfg    = { 
                    .sample_rate_hz = SAMPLE_RATE,
                    .clk_src = I2S_CLK_SRC_PLL_160M,
                    .mclk_multiple = I2S_MCLK_MULTIPLE_256,
                },
    .slot_cfg   = { 
                .data_bit_width = I2S_DATA_BIT_WIDTH_32BIT,
                .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
                .slot_mode = I2S_SLOT_MODE_MONO,
                .slot_mask = I2S_STD_SLOT_BOTH,
                .ws_width = I2S_DATA_BIT_WIDTH_32BIT,
                .ws_pol = 0,
                .bit_shift = 0,
                .left_align = 1,
                .big_endian = 0,
                .bit_order_lsb = 0 
                },
    .gpio_cfg = {
        .mclk = I2S_GPIO_UNUSED,
        .bclk = EXAMPLE_STD_BCLK_IO1,
        .ws = EXAMPLE_STD_WS_IO1,
        .dout = EXAMPLE_STD_DOUT_IO1,
        .din = I2S_GPIO_UNUSED,
        .invert_flags = {
            .mclk_inv = false,
            .bclk_inv = false,
            .ws_inv = false,
            },
        }
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_chan, &tx_std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_chan));       

}

//RMT DEFINITIONS__________________________________________________________________I2S END_______________________I2S END___________________________________

#define RMT_RESOLUTION_HZ 10000000              //10MHz resolution, 1 tick = 0.1us (led strip needs a high resolution)
#define RMT_TENS_PHASE_A_GPIO_NUM      5
#define RMT_TENS_PHASE_B_GPIO_NUM      4

#define FRAME_DURATION_MS   10
 
static const char *TAG = "example";

enum TENS_state {                              //states of TENS output
    PULSE_HIGH,
    PULSE_LOW_FOR_COMPLEMENTARY_PHASE,
    PULSE_WAIT_ZONE
    };

enum TENS_phase_state{
    PHASE_A_LEADING,
    PHASE_B_LEADING,
    };

typedef struct {
    int payload_when_A_leads;
    int payload_when_B_leads;
    int* current_leading_phase;
}   TENS_sequences;

#define TENS_PULSE_WIDTH_US             150         //150us pulse width per phase
#define TENS_INTERPHASE_DEADTIME_US     0.5         //500ns deadtime between phases to prevent shoot-through
#define TENS_PULSE_PERIOD_US            1000        //Send 1 bi-phasic pulse every 1ms


static const rmt_symbol_word_t TENS_pulse_high = {  //This sends a TENS pulse
    .level0 = 1,
    .duration0 = TENS_PULSE_WIDTH_US * RMT_RESOLUTION_HZ / 1000000, //150 us
    .level1 = 0,
    .duration1 = TENS_INTERPHASE_DEADTIME_US * RMT_RESOLUTION_HZ / 1000000,  //0.5 us
};
    //Added 21.5us trim feature to calibrate offset
static const rmt_symbol_word_t TENS_pulse_low = {   //This is a timeholder to keep the current TENS phase low for the same duration that the other fires
    .level0 = 0,
    .duration0 = ((TENS_PULSE_WIDTH_US) * RMT_RESOLUTION_HZ )/ 1000000, //150 us  Add -21.5 to trim output and sync with next channel
    .level1 = 0,
    .duration1 = TENS_INTERPHASE_DEADTIME_US * RMT_RESOLUTION_HZ / 1000000, //0.5 us
};

static const rmt_symbol_word_t TENS_pulse_interpulse = {    //This controls the inter-pulse timing
    .level0 = 0,
    .duration0 = (TENS_PULSE_PERIOD_US - (TENS_PULSE_WIDTH_US + TENS_INTERPHASE_DEADTIME_US)*2) / 2 * RMT_RESOLUTION_HZ / 1000000, //349 us
    .level1 = 0,
    .duration1 = (TENS_PULSE_PERIOD_US - (TENS_PULSE_WIDTH_US + TENS_INTERPHASE_DEADTIME_US)*2) / 2 * RMT_RESOLUTION_HZ / 1000000, //349 us
};


static size_t encoder_callback(const void *data, size_t data_size,
                               size_t symbols_written, size_t symbols_free,
                               rmt_symbol_word_t *symbols, bool *done, void *arg)
{
    size_t data_pos = symbols_written;     // We can calculate where in the data we are from how many symbols have already been written.

    int *data_array = (int*)data;
    int len_data_array = data_size / sizeof(data_array[0]);     //Calculate how may enums are passed into the data array

    if (data_pos >= len_data_array) { 
        *done = 1; //Indicate end of the transaction.
        return 0; //Return to end function before doing anything else
        }

    size_t symbol_pos = 0;          

    while (data_pos < len_data_array && symbol_pos < symbols_free) {    //While we are still in the data array and have symbols free

        switch (data_array[data_pos]) {                                 //See what the current enum value (TENS state) is and encode accordingly
    
            case PULSE_HIGH:
                symbols[symbol_pos++] = TENS_pulse_high;
                break;

            case PULSE_LOW_FOR_COMPLEMENTARY_PHASE:
                symbols[symbol_pos++] = TENS_pulse_low;  
                break;

            case PULSE_WAIT_ZONE:
                symbols[symbol_pos++] = TENS_pulse_interpulse;
                break;

            default:
                break; // Handle unexpected enum cases if necessary
        }

        data_pos++; // Move to the next element in the data array
    }

    *done = (data_pos >= len_data_array); // Mark the transaction done if we've processed the entire data array
    return symbol_pos;
}


//RMT DEFINITIONS__________________________________________________________________RMT END_______________________RMT END___________________________________

//GPTimer DEFINITIONS__________________________________________________________________GPTimer START_______________________GPTimer END___________________________________

typedef struct {
    uint64_t event_count;
} example_queue_element_t;

typedef struct{
    rmt_channel_handle_t tens_phase_chan;
    rmt_encoder_handle_t tens_phase_encoder;
    int tens_phase_sequence;
    rmt_transmit_config_t *tx_config_ptr;
    int * w;
}   rmt_passthrough_struct;

static bool example_timer_on_alarm_cb(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx)
{
    BaseType_t high_task_awoken = pdFALSE;

    rmt_passthrough_struct* data_ptr = (rmt_passthrough_struct*)user_ctx;

    int *w = data_ptr->w;
    rmt_passthrough_struct data = *data_ptr;

    // ESP_ERROR_CHECK(rmt_transmit((rmt_channel_handle_t)data.tens_phase_chan,
    //                              (rmt_encoder_handle_t)data.tens_phase_encoder,
    //                              (int)data.tens_phase_sequence,
    //                              sizeof((int)data.tens_phase_sequence),
    //                              (rmt_transmit_config_t*)data.tx_config_ptr));


    // int32_t *wave = data->wave_passthrough;


    // int *w = (int *)user_ctx;
    // QueueHandle_t queue = (QueueHandle_t)user_ctx;
    // Retrieve the count value from event data
    // example_queue_element_t ele = {
    //     .event_count = edata->count_value
    // };

    gpio_set_level(GPIO_NUM_17, *w);
    // i2s_write_function(wave);
    *w = (*w == 1) ? 0 : 1;

    // Optional: send the event data to other task by OS queue
    // Do not introduce complex logics in callbacks
    // Suggest dealing with event data in the main loop, instead of in this callback
    // xQueueSendFromISR(queue, &ele, &high_task_awoken);
    // return whether we need to yield at the end of ISR
    return high_task_awoken == pdTRUE;
}
//GPTimer DEFINITIONS__________________________________________________________________GPTimer END_______________________GPTimer END___________________________________



void app_main(void)
{
    //RMT Main Function______________________________________________________RMT MAIN START________________________________RMT MAIN START______________________
    ESP_LOGI(TAG, "Create RMT TX channel");

    //Initialize the rmt channels with null values.  An actual handle can be returned from rmt_new_tx_channel later
    rmt_channel_handle_t tens_phase_A_chan = NULL;
    rmt_channel_handle_t tens_phase_B_chan = NULL;      


    //Create a struct to configure the rmt channel
    rmt_tx_channel_config_t tens_phase_A_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,         //set clock source to default
        .gpio_num = RMT_TENS_PHASE_A_GPIO_NUM,  //set gpio number of this peripheral
        .mem_block_symbols = 64,               //set block size to 256. Can be as low as 64
        .resolution_hz = RMT_RESOLUTION_HZ,     //Set clock resolution to 10MHz
        .trans_queue_depth = 1,                 // set the number of transactions that can be pending in the background
    };

    rmt_tx_channel_config_t tens_phase_B_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,         //set clock source to default
        .gpio_num = RMT_TENS_PHASE_B_GPIO_NUM,  //set gpio number of this peripheral
        .mem_block_symbols = 64,               //set block size to 256. Can be as low as 64
        .resolution_hz = RMT_RESOLUTION_HZ,     //Set clock resolution to 10MHz
        .trans_queue_depth = 1,                 // set the number of transactions that can be pending in the background
    };

    //Create the rmt channel by passing pointers to the configuration structure and the RMT channel
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tens_phase_A_chan_config, &tens_phase_A_chan));
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tens_phase_B_chan_config, &tens_phase_B_chan));

    //Initialize the RMT encoder
    ESP_LOGI(TAG, "Create simple callback-based encoder");
    rmt_encoder_handle_t tens_phase_A_encoder = NULL;
    rmt_encoder_handle_t tens_phase_B_encoder = NULL;

    
    //Configure the RMT encoder by assigning the encoding callback function
    const rmt_simple_encoder_config_t tens_phase_A_encoder_cfg = {
        .callback = encoder_callback                                //this will be re-used for both phases
        //Note we don't set min_chunk_size here as the default of 64 is good enough.
    };

    const rmt_simple_encoder_config_t tens_phase_B_encoder_cfg = {
        .callback = encoder_callback                                //this will be re-used for both phases
        //Note we don't set min_chunk_size here as the default of 64 is good enough.
    };

    //Create the RMT encoder by passing pointers to the encoder configuration struct and encoder handle
    ESP_ERROR_CHECK(rmt_new_simple_encoder(&tens_phase_A_encoder_cfg, &tens_phase_A_encoder));
    ESP_ERROR_CHECK(rmt_new_simple_encoder(&tens_phase_B_encoder_cfg, &tens_phase_B_encoder));

    //Enable the RMT channel
    ESP_LOGI(TAG, "Enable RMT TENS Phase A TX channel");
    ESP_ERROR_CHECK(rmt_enable(tens_phase_A_chan));
    ESP_ERROR_CHECK(rmt_enable(tens_phase_B_chan));


    //Create a structure to configure the RMT output
    ESP_LOGI(TAG, "Start TENS output");
    rmt_transmit_config_t tx_config = {     //This will be re-used for both phases
        .loop_count = 0, // no transfer loop
    };

    rmt_channel_handle_t tens_channels[2] = {
                                            tens_phase_A_chan,
                                            tens_phase_B_chan
                                            };  

    
    //Create new RMT tx channel synchronization manager
    rmt_sync_manager_handle_t synchro = NULL;
    rmt_sync_manager_config_t synchro_config = {
        .tx_channel_array = tens_channels,
        .array_size = sizeof(tens_channels) / sizeof(tens_channels[0]),
    };
    ESP_ERROR_CHECK(rmt_new_sync_manager(&synchro_config, &synchro));


    //Write the payload to be passed to the RMT peripheral

    int tens_phase_A_sequence[8] = {    PULSE_HIGH,
                                        PULSE_LOW_FOR_COMPLEMENTARY_PHASE,
                                        PULSE_WAIT_ZONE,
                                        PULSE_HIGH,
                                        PULSE_LOW_FOR_COMPLEMENTARY_PHASE,
                                        PULSE_WAIT_ZONE,
                                        PULSE_HIGH,
                                        PULSE_LOW_FOR_COMPLEMENTARY_PHASE,
                                    };

    int tens_phase_B_sequence[8] = {    PULSE_LOW_FOR_COMPLEMENTARY_PHASE,
                                        PULSE_HIGH,
                                        PULSE_WAIT_ZONE,
                                        PULSE_LOW_FOR_COMPLEMENTARY_PHASE,
                                        PULSE_HIGH,
                                        PULSE_WAIT_ZONE,
                                        PULSE_LOW_FOR_COMPLEMENTARY_PHASE,
                                        PULSE_HIGH,
                                    };

    //RMT Main Function______________________________________________________RMT MAIN END________________________________RMT MAIN END______________________

    //I2S Main Function______________________________________________________I2S MAIN START________________________________RMT MAIN START______________________

    int64_t start_time = esp_timer_get_time();
    double start_time_ms  = (double)(start_time) / (double)(1000);
    printf("Current Time:  %0.10f ms\n", start_time_ms);


    // esp_task_wdt_config_t watchdog_config = {   //Configure watchdog timer
    //     .timeout_ms = 500000,                      //Set watchdog timeout in ms
    //     .idle_core_mask = 0,                    //Set to 0 to allow "feeding" the watchdog, set to 1 if you enjoy unhappiness
    //     .trigger_panic = false                  //Watchdog timer does not cause panic
    // };

    // // esp_task_wdt_init(&watchdog_config);            //Initialize watchdog using configuration.  May not strictly be necessary and may throw a harmless error
    //                                                     // if the watchdog has already started
    // esp_task_wdt_reconfigure(&watchdog_config);     //Reconfigure task using configuration.  This ~IS~ necessary because the watchdog may have already been started
    //                                                     //with undesireable parameters.
    // esp_task_wdt_add(xTaskGetCurrentTaskHandle());  //Add the current task to the watchdog.  This is necessary for the "feeding" to function
    
    // esp_task_wdt_reset();                           //Feed the watchdog timer by calling esp_task_wdt_reset() periodically
    //                                                     //more frequently than the "timeout_ms" amount of time


    printf("\n1\n");

    int32_t *wave = calloc(WAVEFORM_LEN, sizeof(int32_t));
    double *wave_double = calloc(WAVEFORM_LEN, sizeof(double));
    // Create the sine wave
    printf("\n2\n");

    create_sine_wave(wave, wave_double, FREQUENCY);
    printf("\n3\n");


    i2s_channel_setup();

    //I2S Main Function______________________________________________________I2S MAIN END________________________________I2S MAIN END______________________

    //GPTimer Main Function__________________________________________________GPTimer_____________________________________GPTimer_START
    gptimer_handle_t gptimer = NULL;
    gptimer_config_t timer_config = {
    .clk_src = GPTIMER_CLK_SRC_DEFAULT,
    .direction = GPTIMER_COUNT_UP,
    .resolution_hz = 1 * 1000 * 1000, // 1MHz, 1 tick = 1us
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));

    gptimer_alarm_config_t alarm_config = {
    .reload_count = -1, // counter will reload with 0 on alarm event
    .alarm_count = 200e3, // period = 500ms @resolution 1MHz
    .flags.auto_reload_on_alarm = true, // enable auto-reload
    };

    ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config));

    gptimer_event_callbacks_t cbs = {
        .on_alarm = example_timer_on_alarm_cb, // register user callback
    };

    int w = 0;

    rmt_passthrough_struct rmt_passthrough = {
        .tens_phase_chan = tens_phase_A_chan,
        .tens_phase_encoder = tens_phase_A_encoder,
        .tens_phase_sequence = tens_phase_A_sequence,
        .tx_config_ptr = &tx_config,
        .w = &w,
    };

    ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, &rmt_passthrough));
    ESP_ERROR_CHECK(gptimer_enable(gptimer));
    ESP_ERROR_CHECK(gptimer_start(gptimer));

    //GPTimer Main Function__________________________________________________GPTimer_____________________________________GPTimer_START

    gpio_set_direction(GPIO_NUM_17, GPIO_MODE_OUTPUT);

    int j=0;
    while (j<300) {

        i2s_write_function(wave);

        //Write to the RMT channel for it to begin writing the desired sequence.
        ESP_ERROR_CHECK(rmt_transmit(tens_phase_A_chan, tens_phase_A_encoder, tens_phase_A_sequence, sizeof(tens_phase_A_sequence), &tx_config));
        ESP_ERROR_CHECK(rmt_transmit(tens_phase_B_chan, tens_phase_B_encoder, tens_phase_B_sequence, sizeof(tens_phase_B_sequence), &tx_config));

        //Wait for the RMT channel to finish writing.
        ESP_ERROR_CHECK(rmt_tx_wait_all_done(tens_phase_A_chan, portMAX_DELAY));
        ESP_ERROR_CHECK(rmt_tx_wait_all_done(tens_phase_B_chan, portMAX_DELAY));

        vTaskDelay(pdMS_TO_TICKS(1000));
        j++;
        printf("idx: %i\n", j);
        printf("w: %i\n", w);

    }

}