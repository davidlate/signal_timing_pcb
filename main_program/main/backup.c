// /*
//  * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
//  *
//  * SPDX-License-Identifier: Unlicense OR CC0-1.0
//  */

// //This is a backup before a change was implemented to the code to change the way encoding is performed.

// #include <string.h>
// #include <math.h>
// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"
// #include "esp_log.h"
// #include "driver/rmt_tx.h"

// #define RMT_LED_STRIP_RESOLUTION_HZ 10000000 // 10MHz resolution, 1 tick = 0.1us (led strip needs a high resolution)
// #define RMT_TENS_PHASE_A_GPIO_NUM      2

// #define EXAMPLE_LED_NUMBERS         1

// #define FRAME_DURATION_MS   200
// // #define EXAMPLE_ANGLE_INC_FRAME     0.02
// // #define EXAMPLE_ANGLE_INC_LED       0.3
 
// static const char *TAG = "example";

// static uint8_t tens_phase_A_sequence[EXAMPLE_LED_NUMBERS * 3];

// // static const rmt_symbol_word_t ws2812_zero = {
// //     .level0 = 1,
// //     .duration0 = .1 * RMT_LED_STRIP_RESOLUTION_HZ / 1000000, //0.1 us
// //     .level1 = 0,
// //     .duration1 = .1 * RMT_LED_STRIP_RESOLUTION_HZ / 1000000,
// // };

// // static const rmt_symbol_word_t ws2812_one = {
// //     .level0 = 1,
// //     .duration0 = .1 * RMT_LED_STRIP_RESOLUTION_HZ / 1000000,
// //     .level1 = 0,
// //     .duration1 = .1 * RMT_LED_STRIP_RESOLUTION_HZ / 1000000,
// // };


// static const rmt_symbol_word_t TENS_pulse_high = {
//     .level0 = 1,
//     .duration0 = 150 * RMT_LED_STRIP_RESOLUTION_HZ / 1000000, //150 us
//     .level1 = 0,
//     .duration1 = 150 * RMT_LED_STRIP_RESOLUTION_HZ / 1000000,  //200 us
// };

// static const rmt_symbol_word_t TENS_pulse_low = {
//     .level0 = 0,
//     .duration0 = 175 * RMT_LED_STRIP_RESOLUTION_HZ / 1000000, //175 us
//     .level1 = 0,
//     .duration1 = 175 * RMT_LED_STRIP_RESOLUTION_HZ / 1000000, //175 us
// };


// //reset defaults to 50uS
// static const rmt_symbol_word_t ws2812_reset = {
//     .level0 = 1,
//     .duration0 = RMT_LED_STRIP_RESOLUTION_HZ / 1000000 * 50 / 2, // 25us
//     .level1 = 0,
//     .duration1 = RMT_LED_STRIP_RESOLUTION_HZ / 1000000 * 50 / 2, // 25us
// };

// static size_t encoder_callback(const void *data, size_t data_size,
//                                size_t symbols_written, size_t symbols_free,
//                                rmt_symbol_word_t *symbols, bool *done, void *arg)
// {
//     // We need a minimum of 8 symbol spaces to encode a byte. We only
//     // need one to encode a reset, but it's simpler to simply demand that
//     // there are 8 symbol spaces free to write anything.
//     if (symbols_free < 8) {
//         return 0;
//     }

//     // We can calculate where in the data we are from the symbol pos.
//     // Alternatively, we could use some counter referenced by the arg
//     // parameter to keep track of this.
//     size_t data_pos = symbols_written / 8;  //We write 8 symbols per bytes.  So, we divide symbols_written by 8 to determine how many bytes have already been written
//                                             //This is used in the next steps, after we cast the data to an 8-bit integer. 

//     uint8_t *data_bytes = (uint8_t*)data;  //This casts the integer input to an 8-bit integer format. We then go through each number in the bit to see if it is one or zero
//                                             //This is converted to the integer which, in binary, become a format similar to 11001100 00110011 00110011 11100011, in the following loop, we shift through
//                                             //each of the upcoming 8 bits and check whether the bit is one or zero
//                                             //This one or zero is converted to the corresponding RMT output for a 1 or a 0, in the example, ws2812_one or ws2812_zero
//                                             //This output is added to the 8-symbol long array called "symbols", which is a protected term that the driver uses to load into the 
//                                             //RMT peripheral
//     if (data_pos < data_size) {
//         // Encode a byte
//         size_t symbol_pos = 0;
//         // for (int bitmask = 0x80; bitmask != 0; bitmask >>= 1) {
//         //     if (data_bytes[data_pos]&bitmask) {
//         //         symbols[symbol_pos++] = ws2812_one;
//         //     } else {
//         //         symbols[symbol_pos++] = ws2812_zero;
//         //     }
//                 // }
//         symbols[0] = TENS_pulse_high;
//         symbols[1] = TENS_pulse_low;
//         symbols[2] = TENS_pulse_low;
//         symbols[3] = TENS_pulse_high;
//         symbols[4] = TENS_pulse_low;
//         symbols[5] = TENS_pulse_low;
//         symbols[6] = TENS_pulse_high;
//         symbols[7] = TENS_pulse_low;

//         symbol_pos = 7;
        
//         *done = 1; //Indicate end of the transaction.

//         // We're done; we should have written 8 symbols.
//         return symbol_pos;      //symbol_pos;
//     } else {
//         //All bytes already are encoded.
//         //Encode the reset, and we're done.
//         symbols[0] = ws2812_reset;
//         *done = 1; //Indicate end of the transaction.
//         return 1; //we only wrote one symbol
//     }
// }

// void app_main(void)
// {
//     ESP_LOGI(TAG, "Create RMT TX channel");
//     rmt_channel_handle_t led_chan = NULL;
//     rmt_tx_channel_config_t tx_chan_config = {
//         .clk_src = RMT_CLK_SRC_DEFAULT, // select source clock
//         .gpio_num = RMT_TENS_PHASE_A_GPIO_NUM,
//         .mem_block_symbols = 256, // increase the block size can make the LED less flickering
//         .resolution_hz = RMT_LED_STRIP_RESOLUTION_HZ,
//         .trans_queue_depth = 1, // set the number of transactions that can be pending in the background
//     };
//     ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &led_chan));

//     ESP_LOGI(TAG, "Create simple callback-based encoder");
//     rmt_encoder_handle_t simple_encoder = NULL;
//     const rmt_simple_encoder_config_t simple_encoder_cfg = {
//         .callback = encoder_callback
//         //Note we don't set min_chunk_size here as the default of 64 is good enough.
//     };
//     ESP_ERROR_CHECK(rmt_new_simple_encoder(&simple_encoder_cfg, &simple_encoder));

//     ESP_LOGI(TAG, "Enable RMT TX channel");
//     ESP_ERROR_CHECK(rmt_enable(led_chan));

//     ESP_LOGI(TAG, "Start LED rainbow chase");
//     rmt_transmit_config_t tx_config = {
//         .loop_count = 0, // no transfer loop
//     };
//     float offset = 0;
//     while (1) {
//         for (int led = 0; led < EXAMPLE_LED_NUMBERS; led++) {
//             // Build RGB pixels. Each color is an offset sine, which gives a
//             // hue-like effect.
//             const float color_off = (M_PI * 2) / 3;
//             tens_phase_A_sequence[led * 3 + 0] = sin(color_off * 0) * 127 + 128;
//         }
//         // Flush RGB values to LEDs
//         ESP_ERROR_CHECK(rmt_transmit(led_chan, simple_encoder, tens_phase_A_sequence, sizeof(tens_phase_A_sequence), &tx_config));
//         ESP_ERROR_CHECK(rmt_tx_wait_all_done(led_chan, portMAX_DELAY));
//         vTaskDelay(pdMS_TO_TICKS(FRAME_DURATION_MS));
//         //Increase offset to shift pattern
//         // offset += EXAMPLE_ANGLE_INC_FRAME;
//         if (offset > 2 * M_PI) {
//             offset -= 2 * M_PI;
//         }
//     }
// }