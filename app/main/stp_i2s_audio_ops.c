#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include "esp_mac.h"
#include "soc/esp32/rtc.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "sdkconfig.h"
#include "esp_system.h" 
#include <rom/ets_sys.h>
#include <math.h>

#include "stp_sd_sdcardops.h"
#include "stp_i2s_audio_ops.h"

// const int    I2S_WS_PIN         = GPIO_NUM_4;      //LCK, LRC, 13 I2S word select io number
// const int    I2S_DOUT_PIN       = GPIO_NUM_5;      //DIN, 12 I2S data out io number
// const int    I2S_BCK_PIN        = GPIO_NUM_6;      //BCK 11  I2S bit clock io number
// const float  VOL_PERCENT        = 100.00;
// const double SAMPLE_RATE        = 96000.00;
// const double DURATION_MS        = 10.00;
// const double AUDIO_RISE_TIME_MS = 1.0;


// const double NUM_DMA_BUFF  = 5;
// const double SIZE_DMA_BUFF = 500;



esp_err_t stp_i2s__i2s_channel_setup(stp_i2s__i2s_config* i2s_config_ptr)
{
    char* TAG = "i2s chan setup";

    if (i2s_config_ptr->max_vol_dBFS > 0){
        ESP_LOGE(TAG, "Max volume dBFS must be a negative value!");
        return ESP_FAIL;
    }
    if (i2s_config_ptr->min_vol_dB_rel_to_max > 0){
        ESP_LOGE(TAG, "Min volume dB relative to max must be a negative value!");
        return ESP_FAIL;
    }

    int write_buffer_size_bytes = i2s_config_ptr->num_dma_buf*i2s_config_ptr->size_dma_buf * sizeof(int32_t) * 2;
    i2s_config_ptr->buf_len = write_buffer_size_bytes / sizeof(int32_t);

    i2s_config_ptr->buf_ptr = malloc(write_buffer_size_bytes*2);
    if (i2s_config_ptr->buf_ptr == NULL){
        ESP_LOGE(TAG, "Error allocating memory for i2s write buffer");
        return ESP_FAIL;
    }
    assert(i2s_config_ptr->buf_ptr);
    i2s_config_ptr->buf_capacity = write_buffer_size_bytes*2;

    i2s_chan_config_t tx_chan_cfg = {
        .id = I2S_NUM_AUTO,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = i2s_config_ptr->num_dma_buf,
        .dma_frame_num = i2s_config_ptr->size_dma_buf,
        .auto_clear_before_cb = true,
    };


    if(i2s_new_channel(&tx_chan_cfg, &(i2s_config_ptr->tx_chan), NULL) != ESP_OK){
        ESP_LOGE(TAG, "Error creating I2S channel!");
        return ESP_FAIL;
    }
    
    i2s_std_config_t tx_std_cfg = {
    .clk_cfg    = { 
                    .sample_rate_hz = i2s_config_ptr->sample_rate_Hz,
                    .clk_src        = I2S_CLK_SRC_PLL_160M,
                    .mclk_multiple  = I2S_MCLK_MULTIPLE_256,
                },
    .slot_cfg   = { 
                .data_bit_width = I2S_DATA_BIT_WIDTH_32BIT,
                .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
                .slot_mode      = I2S_SLOT_MODE_STEREO,
                .slot_mask      = I2S_STD_SLOT_BOTH,
                .ws_width       = I2S_DATA_BIT_WIDTH_32BIT,
                .ws_pol         = 0,
                .bit_shift      = 0,
                .left_align     = 1,
                .big_endian     = 0,
                .bit_order_lsb  = 0 
                },
    .gpio_cfg = {
        .mclk   = I2S_GPIO_UNUSED,
        .bclk   = i2s_config_ptr->bclk_pin,
        .ws     = i2s_config_ptr->ws_pin,
        .dout   = i2s_config_ptr->dout_pin,
        .din    = I2S_GPIO_UNUSED,
        .invert_flags = {
            .mclk_inv = false,
            .bclk_inv = false,
            .ws_inv = false,
            },
        }
    };
    if(i2s_channel_init_std_mode(i2s_config_ptr->tx_chan, &tx_std_cfg) != ESP_OK){
        ESP_LOGE(TAG, "Error initializing i2s channel!");
        return ESP_FAIL;
    }
    return ESP_OK;
}

// esp_err_t stp_i2s__i2s_channel_ISR_enable(stp_i2s__i2s_config* i2s_config_ptr);          //Begin I2S transmission using custom driver function.  See update i2s_common.c and .h in this repo.  
// esp_err_t stp_i2s__i2s_channel_ISR_enable_finish(stp_i2s__i2s_config* i2s_config_ptr);   //Keep freeRTOS happy and finish what i2s_channel_ISR_enable started in the ISR

// esp_err_t i2s_channel_disable(i2s_chan);                                                 //Audio file has been fully transmitted.  Disable the channel to allow new data to be preloaded

esp_err_t stp_i2s__set_vol_scale_factor(stp_i2s__i2s_config* i2s_config_ptr, double set_vol_perc){ //TODO fix this
    char* TAG = "get_vol_scale";

    if (set_vol_perc < 0 || set_vol_perc > 100){
        ESP_LOGE(TAG, "Set Volume percent must be between 0 and 100!");
        return ESP_FAIL;
    }
    i2s_config_ptr->set_vol_percent = set_vol_perc;
    double scale_factor_1 = pow(10, i2s_config_ptr->max_vol_dBFS/20);                                     //Setting the upper end of the dynamic range of the volume control
    double db_rel = (1 - (i2s_config_ptr->set_vol_percent/100)) * i2s_config_ptr->min_vol_dB_rel_to_max;  //min_vol_dB_rel_to_max is negative, so we invert it by using 1 - its value
    double scale_factor_2 = pow(10, db_rel/20);                                                           //if min_vol_dB_rel_to_max = -60 dB, at 10% this evaluates to -54 dB

    if (i2s_config_ptr->set_vol_percent > i2s_config_ptr->min_vol_percent){
        i2s_config_ptr->vol_scale_factor = scale_factor_1 * scale_factor_2;
        i2s_config_ptr->actual_dbFS = 20 * log(i2s_config_ptr->vol_scale_factor / 1);
    }
    else{
        i2s_config_ptr->vol_scale_factor = 0;                                   //Set volume to 0 if below the defined minimum
        i2s_config_ptr->actual_dbFS = -999;
    }
    return ESP_OK;
};

esp_err_t stp_i2s__i2s_channel_enable(stp_i2s__i2s_config* i2s_config_ptr){
    char* TAG = "i2s_enable";
    if(i2s_channel_enable(i2s_config_ptr->tx_chan) != ESP_OK){
        ESP_LOGE(TAG, "Error enabling channel");
        return ESP_FAIL;
    }
    return ESP_OK;
};

esp_err_t stp_i2s__i2s_channel_disable(stp_i2s__i2s_config* i2s_config_ptr){
    char* TAG = "i2s_enable";
    if(i2s_channel_disable(i2s_config_ptr->tx_chan) != ESP_OK){
        ESP_LOGE(TAG, "Error disabling channel");
        return ESP_FAIL;
    }
    return ESP_OK;
};


esp_err_t stp_i2s__play_audio_chunk(stp_i2s__i2s_config* i2s_config_ptr, stp_sd__audio_chunk* audio_chunk_ptr, double set_vol_perc){
    char* TAG = "i2s_play";

    int32_t dither_const = 0;
    dither_const |= 1;          //Flip LSB positive to add dither for PCM5102a chip

    if(!i2s_config_ptr->preloaded){     //Set volume scaling factor, only if not already preloaded.
        if(stp_i2s__set_vol_scale_factor(i2s_config_ptr, set_vol_perc) != ESP_OK){
            ESP_LOGE(TAG, "Error setting volume!");
            return ESP_FAIL;
        }
    }
    printf("Postload audio pos: %i\n", audio_chunk_ptr->chunk_data_pos);

    while(audio_chunk_ptr->chunk_data_pos < audio_chunk_ptr->chunk_len_inc_dither)
    {
        int buf_pos = 0;
        for(buf_pos = 0; buf_pos < i2s_config_ptr->buf_len; buf_pos++)
        {
            if(audio_chunk_ptr->chunk_data_pos < audio_chunk_ptr->chunk_len_inc_dither)
            {
                double sample = (double)(audio_chunk_ptr->chunk_data_ptr[audio_chunk_ptr->chunk_data_pos]);
                double test_scaled_sample = sample * i2s_config_ptr->vol_scale_factor;
                int32_t scaled_sample = (int32_t)(sample * .1);//i2s_config_ptr->vol_scale_factor);
                i2s_config_ptr->buf_ptr[buf_pos] = scaled_sample;
                if(buf_pos == 10){
                    printf("Postload Buf pos: %i | audio_pos: %i | audio idx: %i | audio value %li\n", buf_pos, audio_chunk_ptr->chunk_data_pos, audio_chunk_ptr->data_idx, audio_chunk_ptr->chunk_data_ptr[audio_chunk_ptr->chunk_data_pos]);
                }
                audio_chunk_ptr->chunk_data_pos++;
                audio_chunk_ptr->data_idx++;
                // printf("sample: %.3f, Test_scaled: %.3f, Scaled_sample: %li, volume: %.3f\n", sample, test_scaled_sample, scaled_sample, i2s_config_ptr->vol_scale_factor);
            }
            else
            {
                i2s_config_ptr->buf_ptr[buf_pos] = dither_const;
                // printf("Audio at end of chunk\n");
                // break;
            }
        }
        // printf("Buffer capacity: %i samples\n", i2s_config_ptr->buf_capacity);
        size_t bytes_to_write = buf_pos * sizeof(*(audio_chunk_ptr->chunk_data_ptr));
        size_t bytes_written = 0;
        printf("postload samples to write: %i\n", bytes_to_write/sizeof(int32_t));
        esp_err_t ret = i2s_channel_write(i2s_config_ptr->tx_chan,
                                        i2s_config_ptr->buf_ptr,
                                        bytes_to_write,
                                        &bytes_written,
                                        portMAX_DELAY);
        printf("Bytes written: %i\n", bytes_written);

        if(ret != ESP_OK){
            ESP_LOGE(TAG, "i2s write failed!");
            return ESP_FAIL;
        }
        if(bytes_written != bytes_to_write){
            ESP_LOGE(TAG, "Not enough bytes written to i2s bus!");
            return ESP_FAIL;
        }
        printf("Bytes to write: %i\n", bytes_to_write);
        // Let ESP do something else in between writes
        vTaskDelay(pdMS_TO_TICKS(i2s_config_ptr->ms_delay_between_writes));
    }
    i2s_config_ptr->preloaded = false;
    audio_chunk_ptr->chunk_data_pos = 0;
    audio_chunk_ptr->data_idx = audio_chunk_ptr->start_idx;

    return ESP_OK;
}


esp_err_t stp_i2s__preload_buffer(stp_i2s__i2s_config* i2s_config_ptr, stp_sd__audio_chunk* audio_chunk_ptr, double set_vol_perc){
    
    char* TAG = "i2s_preload";
    int32_t dither_const = 0;
    dither_const |= 1;          //Flip LSB positive to add dither for PCM5102a chip

    if(i2s_config_ptr->preloaded){     //Set volume scaling factor, only if not already preloaded.
        ESP_LOGE(TAG, "I2S buffer already preloaded!");
        return ESP_FAIL;
    };

    audio_chunk_ptr->chunk_data_pos = 0;
    audio_chunk_ptr->data_idx = audio_chunk_ptr->start_idx;

    if(stp_i2s__set_vol_scale_factor(i2s_config_ptr, set_vol_perc) != ESP_OK){
        ESP_LOGE(TAG, "Error setting volume!");
        return ESP_FAIL;
    }
    int buf_pos = 0;
    for(buf_pos = 0; buf_pos < i2s_config_ptr->buf_len; buf_pos++)
    {
        if(audio_chunk_ptr->chunk_data_pos < audio_chunk_ptr->chunk_len_inc_dither)
        {
            double sample = (double)(audio_chunk_ptr->chunk_data_ptr[audio_chunk_ptr->chunk_data_pos]);
            double test_scaled_sample = sample * i2s_config_ptr->vol_scale_factor;
            int32_t scaled_sample = (int32_t)(sample * .1);//i2s_config_ptr->vol_scale_factor);
            i2s_config_ptr->buf_ptr[buf_pos] = scaled_sample;
            if(buf_pos == 960){
                printf("Preload Buf pos: %i | audio_pos: %i | audio idx: %i | audio value %li\n", buf_pos, audio_chunk_ptr->chunk_data_pos, audio_chunk_ptr->data_idx, audio_chunk_ptr->chunk_data_ptr[audio_chunk_ptr->chunk_data_pos]);
            }
            audio_chunk_ptr->chunk_data_pos++;
            audio_chunk_ptr->data_idx++;
            // printf("sample: %.3f, Test_scaled: %.3f, Scaled_sample: %li, volume: %.3f\n", sample, test_scaled_sample, scaled_sample, i2s_config_ptr->vol_scale_factor);
        }
        else
        {
            i2s_config_ptr->buf_ptr[buf_pos] = dither_const;
            printf("Check Preload Function!!!\n");
            printf("audio_chunk_ptr->chunk_data_pos: %i\n", audio_chunk_ptr->chunk_data_pos);
            printf("audio_chunk_ptr->chunk_len_inc_dither: %i\n", audio_chunk_ptr->chunk_len_inc_dither);

            // break;
        }
    }
    size_t bytes_to_write = buf_pos * sizeof(*(audio_chunk_ptr->chunk_data_ptr));
    size_t bytes_written = 0;
    size_t buffer_bytes = 0;
    esp_err_t ret = i2s_channel_preload_data(i2s_config_ptr->tx_chan,
                                             i2s_config_ptr->buf_ptr,
                                             bytes_to_write,
                                             &bytes_written);
    buffer_bytes += bytes_written;
    if(ret != ESP_OK){
        ESP_LOGE(TAG, "i2s preload failed!");
        return ESP_FAIL;
    }

    printf("Buffer bytes: %i\n", buffer_bytes);
    //Let ESP do something else in between writes
    i2s_config_ptr->preloaded = true;
    printf("Preload audio pos: %i\n", audio_chunk_ptr->chunk_data_pos);
    return ESP_OK;
}
