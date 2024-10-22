#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/rmt_tx.h"

#define RMT_RESOLUTION_HZ 10000000              //10MHz resolution, 1 tick = 0.1us (led strip needs a high resolution)
#define RMT_TENS_PHASE_A_GPIO_NUM      2
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

static const rmt_symbol_word_t TENS_pulse_low = {   //This is a timeholder to keep the current TENS phase low for the same duration that the other fires
    .level0 = 0,
    .duration0 = TENS_PULSE_WIDTH_US * RMT_RESOLUTION_HZ / 1000000, //150 us
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


void app_main(void)
{
    ESP_LOGI(TAG, "Create RMT TX channel");

    //Initialize the rmt channels with null values.  An actual handle can be returned from rmt_new_tx_channel later
    rmt_channel_handle_t tens_phase_A_chan = NULL;
    rmt_channel_handle_t tens_phase_B_chan = NULL;      
    rmt_channel_handle_t tens_channels[2] = {NULL};  


    //Create a struct to configure the rmt channel
    rmt_tx_channel_config_t tens_phase_A_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,         //set clock source to default
        .gpio_num = RMT_TENS_PHASE_A_GPIO_NUM,  //set gpio number of this peripheral
        .mem_block_symbols = 256,               //set block size to 256. Can be as low as 64
        .resolution_hz = RMT_RESOLUTION_HZ,     //Set clock resolution to 10MHz
        .trans_queue_depth = 1,                 // set the number of transactions that can be pending in the background
    };

    rmt_tx_channel_config_t tens_phase_B_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,         //set clock source to default
        .gpio_num = RMT_TENS_PHASE_B_GPIO_NUM,  //set gpio number of this peripheral
        .mem_block_symbols = 256,               //set block size to 256. Can be as low as 64
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
    
    //Create new RMT tx channel synchronization manager
    rmt_sync_manager_handle_t synchro = NULL;
    rmt_sync_manager_config_t synchro_config = {
        .tx_channel_array = tens_channels,
        .array_size = sizeof(tens_channels) / sizeof(tens_channels[0]),
    };
    // ESP_ERROR_CHECK(rmt_new_sync_manager(&synchro_config, &synchro));


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

    while (1) {
        
        //Write to the RMT channel for it to begin writing the desired sequence.
        ESP_ERROR_CHECK(rmt_transmit(tens_phase_A_chan, tens_phase_A_encoder, tens_phase_A_sequence, sizeof(tens_phase_A_sequence), &tx_config));
        ESP_ERROR_CHECK(rmt_transmit(tens_phase_B_chan, tens_phase_B_encoder, tens_phase_B_sequence, sizeof(tens_phase_B_sequence), &tx_config));


        //Wait for the RMT channel to finish writing.
        ESP_ERROR_CHECK(rmt_tx_wait_all_done(tens_phase_A_chan, portMAX_DELAY));
        ESP_ERROR_CHECK(rmt_tx_wait_all_done(tens_phase_B_chan, portMAX_DELAY));


        vTaskDelay(pdMS_TO_TICKS(FRAME_DURATION_MS));

    }
}