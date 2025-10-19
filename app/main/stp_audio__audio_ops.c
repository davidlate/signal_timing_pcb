#include "stp_audio__audio_ops.h"
#include "math.h"
#include "esp_err.h"

esp_err_t stp_audio__i2s_channel_setup(stp_audio__i2s_config* i2s_config_ptr)
{
    char* TAG = "i2s chan setup";

    if (i2s_config_ptr->max_vol_dBFS > 0)
    {
        printf("Max volume dBFS must be a negative value!");
        return ESP_FAIL;
    }
    if (i2s_config_ptr->min_vol_dBFS > 0)
    {
        ESP_LOGE(TAG, "Min volume dB relative to max must be a negative value!");
        return ESP_FAIL;
    }
    //TODO check out 4
    int write_buffer_size_bytes = i2s_config_ptr->num_dma_buf*i2s_config_ptr->size_dma_buf * 2;

    i2s_config_ptr->buf_len = write_buffer_size_bytes / sizeof(int32_t);

    i2s_config_ptr->buf_ptr = malloc(write_buffer_size_bytes);

    if (i2s_config_ptr->buf_ptr == NULL)
    {
        ESP_LOGE(TAG, "Error allocating memory for i2s write buffer");
        return ESP_FAIL;
    }
    i2s_config_ptr->buf_capacity = write_buffer_size_bytes;

    i2s_chan_config_t tx_chan_cfg = {
        .id = I2S_NUM_AUTO,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = i2s_config_ptr->num_dma_buf,
        .dma_frame_num = i2s_config_ptr->size_dma_buf,
        .auto_clear = true,
    };

    if(i2s_new_channel(&tx_chan_cfg, &(i2s_config_ptr->tx_chan), NULL) != ESP_OK)
    {
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
    if(i2s_channel_init_std_mode(i2s_config_ptr->tx_chan, &tx_std_cfg) != ESP_OK)
    {
        ESP_LOGE(TAG, "Error initializing i2s channel!");
        return ESP_FAIL;
    }
    return ESP_OK;
}

// esp_err_t stp_audio__i2s_channel_ISR_enable(stp_audio__i2s_config* i2s_config_ptr);          //Begin I2S transmission using custom driver function.  See update i2s_common.c and .h in this repo.  
// esp_err_t stp_audio__i2s_channel_ISR_enable_finish(stp_audio__i2s_config* i2s_config_ptr);   //Keep freeRTOS happy and finish what i2s_channel_ISR_enable started in the ISR

// esp_err_t i2s_channel_disable(i2s_chan);                                                 //Audio file has been fully transmitted.  Disable the channel to allow new data to be preloaded

esp_err_t stp_i2s__set_vol_scale_factor(stp_audio__i2s_config* i2s_config_ptr, double set_vol_perc){ //TODO fix this
    char* TAG = "get_vol_scale";

    if (set_vol_perc < 0 || set_vol_perc > 100){
        ESP_LOGE(TAG, "Set Volume percent must be between 0 and 100!");
        return ESP_FAIL;
    }
    i2s_config_ptr->set_vol_percent = set_vol_perc;

    double min_dBFS = i2s_config_ptr->min_vol_dBFS;     
    double max_dBFS = i2s_config_ptr->max_vol_dBFS;

    double set_vol_dBFS = set_vol_perc/100 * (max_dBFS - min_dBFS) + min_dBFS;      

    double vol_scale_factor = pow(10, set_vol_dBFS/20);                                     //Setting the upper end of the dynamic range of the volume control

    if (i2s_config_ptr->set_vol_percent > i2s_config_ptr->min_vol_percent){
        i2s_config_ptr->vol_scale_factor = vol_scale_factor;
        i2s_config_ptr->actual_dbFS = set_vol_dBFS;
    }
    else{
        i2s_config_ptr->vol_scale_factor = 0;                                   //Set volume to 0 if below the defined minimum
        i2s_config_ptr->actual_dbFS = -999;
    }
    return ESP_OK;
};

esp_err_t stp_audio__i2s_channel_enable(stp_audio__i2s_config* i2s_config_ptr){
    char* TAG = "i2s_enable";
    if(i2s_channel_enable(i2s_config_ptr->tx_chan) != ESP_OK){
        ESP_LOGE(TAG, "Error enabling channel");
        return ESP_FAIL;
    }
    return ESP_OK;
};

esp_err_t stp_audio__i2s_channel_disable(stp_audio__i2s_config* i2s_config_ptr){
    char* TAG = "i2s_enable";
    if(i2s_channel_disable(i2s_config_ptr->tx_chan) != ESP_OK){
        ESP_LOGE(TAG, "Error disabling channel");
        return ESP_FAIL;
    }
    return ESP_OK;
};


esp_err_t stp_audio__play_audio_chunk(stp_audio__i2s_config* i2s_config_ptr, stp_sd__audio_chunk* audio_chunk_ptr, double set_vol_perc){
    char* TAG = "i2s_play";

    
    if (audio_chunk_ptr->capacity < i2s_config_ptr->buf_len * sizeof(int32_t))
    {
        ESP_LOGE(TAG, "Audio Chunk Memory Buffer smaller than i2s memory buffer!");
    }

    int i = 0;

    stp_sd__reload_memory_data_struct* reload_memory_struct_ptr = audio_chunk_ptr->reload_memory_struct_ptr;

    reload_memory_struct_ptr->B_idx                     = audio_chunk_ptr->B_idx;
    reload_memory_struct_ptr->new_chunk_data_pos        = audio_chunk_ptr->chunk_data_pos + audio_chunk_ptr->capacity/sizeof(int32_t) + audio_chunk_ptr->B_idx;
    reload_memory_struct_ptr->chunk_len_wo_dither       = audio_chunk_ptr->chunk_len_wo_dither;
    reload_memory_struct_ptr->chunk_load_ptr            = audio_chunk_ptr->chunk_load_ptr;
    reload_memory_struct_ptr->chunk_start_pos_filebytes = audio_chunk_ptr->chunk_start_pos_filebytes;
    reload_memory_struct_ptr->task_to_notify            = xTaskGetCurrentTaskHandle();
    reload_memory_struct_ptr->chunk_load_ptr_cap        = audio_chunk_ptr->capacity;

    if(i2s_config_ptr->preloaded == false)
    {     //Set volume scaling factor, only if not already preloaded.
        if(stp_i2s__set_vol_scale_factor(i2s_config_ptr, set_vol_perc) != ESP_OK)
        {
            ESP_LOGE(TAG, "Error setting volume!");
        }
        if(xQueueSend(audio_chunk_ptr->reload_audio_buff_Queue, reload_memory_struct_ptr, 0) != pdTRUE) ESP_LOGE(TAG, "Failed to send to Queue!");
    }
    
    size_t bytes_written = 0;
    int num_reloads = 0;

    //TODO keep going here
    while(audio_chunk_ptr->chunk_data_pos < audio_chunk_ptr->chunk_len_inc_dither)
    {
        int buf_pos = 0;
        int chunk_samples_loaded = 0;

        for(buf_pos = 0; buf_pos < i2s_config_ptr->buf_len; buf_pos++)
        {
            if(audio_chunk_ptr->chunk_data_pos < audio_chunk_ptr->chunk_len_inc_dither)
            {
                int32_t next_sample;
                bool time_to_reload = false;
                stp_sd__get_next_audio_sample(audio_chunk_ptr, &next_sample, &time_to_reload); //set the next audio data point to the next_sample variable
                if(time_to_reload == true)
                {
                    uint32_t load_result;
                    if(xTaskNotifyWait(ULONG_MAX, ULONG_MAX, &load_result, 10) != pdFALSE)      //Wait for reload memory task to finish loading SD card data onto buffer
                    {
                        memcpy(audio_chunk_ptr->chunk_data_ptr, reload_memory_struct_ptr->chunk_load_ptr, reload_memory_struct_ptr->chunk_load_ptr_cap);
                        audio_chunk_ptr->memory_buffer_pos = 0;
                        reload_memory_struct_ptr->new_chunk_data_pos = audio_chunk_ptr->chunk_data_pos + audio_chunk_ptr->capacity / sizeof(int32_t);
                        if(xQueueSend(audio_chunk_ptr->reload_audio_buff_Queue, reload_memory_struct_ptr, 0) != pdTRUE) ESP_LOGE(TAG, "Failed to send to Queue!");
                        num_reloads++;
                    }
                    else
                    {
                        ESP_LOGE(TAG, "Reloaded memory chunk not loaded yet!");
                    }
                }
                double double_scaled_sample = (double)next_sample * i2s_config_ptr->vol_scale_factor;// * i2s_config_ptr->vol_scale_factor;// * i2s_config_ptr->vol_scale_factor;
                int32_t scaled_sample = (int32_t)(double_scaled_sample);//i2s_config_ptr->vol_scale_factor);
                if (scaled_sample == 0) scaled_sample = 1;
                i2s_config_ptr->buf_ptr[buf_pos] = scaled_sample;

                //audio_chunk_ptr->chunk_data_pos is incremented within the stp_sd_get_next_audio_sample function
                chunk_samples_loaded++;
                if(audio_chunk_ptr->memory_buffer_pos > audio_chunk_ptr->capacity/sizeof(int32_t))
                {
                    ESP_LOGE(TAG, "Error, memory buffer overflow!");
                }
            }
            else
            {
                i2s_config_ptr->buf_ptr[buf_pos] = 1;
            }
        }
        size_t bytes_to_write = buf_pos * sizeof(*(audio_chunk_ptr->chunk_data_ptr));
        bytes_written = 0;

        // printf("postload samples to write: %i\n", bytes_to_write/sizeof(int32_t));
        esp_err_t ret = i2s_channel_write(i2s_config_ptr->tx_chan,
                                        i2s_config_ptr->buf_ptr,
                                        bytes_to_write,
                                        &bytes_written,
                                        portMAX_DELAY);

        // if(bytes_written < bytes_to_write) ESP_LOGE(TAG, "Not all bytes written!");
        audio_chunk_ptr->chunk_data_pos = audio_chunk_ptr->chunk_data_pos - chunk_samples_loaded + (bytes_written / sizeof(int32_t)); //
            
        if(ret != ESP_OK){
            ESP_LOGE(TAG, "i2s write failed!");
            // return ESP_FAIL;
        }

        vTaskDelay(pdMS_TO_TICKS(i2s_config_ptr->ms_delay_between_writes));
        i++;
    }

    for (int j=0; j<i2s_config_ptr->buf_len; j++)
        {
            i2s_config_ptr->buf_ptr[j] = 1;
        }
        /*DMA Buffers _must_ be zeroed when call is finished - setting all values to 1 to prevent PCM5102a from popping*/
    bytes_written = 0;
    i2s_channel_write(i2s_config_ptr->tx_chan,
                        i2s_config_ptr->buf_ptr,
                        i2s_config_ptr->buf_len*sizeof(int32_t) ,
                        &bytes_written,
                        portMAX_DELAY);

    i2s_config_ptr->preloaded = false;
    audio_chunk_ptr->chunk_data_pos = 0;

    return ESP_OK;
}
    

esp_err_t stp_audio__preload_buffer(stp_audio__i2s_config* i2s_config_ptr, stp_sd__audio_chunk* audio_chunk_ptr, double set_vol_perc){
    
    char* TAG = "i2s_preload";

    if(i2s_config_ptr->preloaded){     //Set volume scaling factor, only if not already preloaded.
        ESP_LOGE(TAG, "I2S buffer already preloaded!");
        return ESP_FAIL;
    };

    audio_chunk_ptr->chunk_data_pos = 0;
    audio_chunk_ptr->memory_buffer_pos = 0;
    audio_chunk_ptr->data_idx = audio_chunk_ptr->start_idx;
    stp_sd__wavFile* wave_file_ptr = audio_chunk_ptr->wavFile_ptr;

    if(stp_i2s__set_vol_scale_factor(i2s_config_ptr, set_vol_perc) != ESP_OK){
        ESP_LOGE(TAG, "Error setting volume!");
        return ESP_FAIL;
    }

    stp_sd__reload_memory_data_struct* reload_memory_struct_ptr = audio_chunk_ptr->reload_memory_struct_ptr;

    reload_memory_struct_ptr->B_idx                     = audio_chunk_ptr->B_idx;
    reload_memory_struct_ptr->new_chunk_data_pos        = audio_chunk_ptr->chunk_data_pos + audio_chunk_ptr->capacity/sizeof(int32_t);
    reload_memory_struct_ptr->chunk_len_wo_dither       = audio_chunk_ptr->chunk_len_wo_dither;
    reload_memory_struct_ptr->chunk_load_ptr            = audio_chunk_ptr->chunk_load_ptr;
    reload_memory_struct_ptr->chunk_start_pos_filebytes = audio_chunk_ptr->chunk_start_pos_filebytes;
    reload_memory_struct_ptr->task_to_notify            = xTaskGetCurrentTaskHandle();
    reload_memory_struct_ptr->chunk_load_ptr_cap        = audio_chunk_ptr->capacity;
    
    if(xQueueSend(audio_chunk_ptr->reload_audio_buff_Queue, reload_memory_struct_ptr, 0) != pdTRUE) ESP_LOGE(TAG, "Failed to send to Queue!");

    int buf_pos = 0;
    int chunk_samples_loaded = 0;
    for(buf_pos = 0; buf_pos < i2s_config_ptr->buf_len; buf_pos++)
    {
        if(audio_chunk_ptr->chunk_data_pos < audio_chunk_ptr->chunk_len_inc_dither)
        {
            int32_t next_sample;
            bool time_to_reload = false;
            stp_sd__get_next_audio_sample(audio_chunk_ptr, &next_sample, &time_to_reload); //set the next audio data point to the next_sample variable
            if(time_to_reload==true)
            {
                ESP_LOGE(TAG, "PRELOAD ERROR!");
            }
            double double_scaled_sample = (double)next_sample * (double)(i2s_config_ptr->vol_scale_factor);
            int32_t scaled_sample = (int32_t)(double_scaled_sample);//i2s_config_ptr->vol_scale_factor);
            if (scaled_sample == 0) scaled_sample = 1;
            i2s_config_ptr->buf_ptr[buf_pos] = scaled_sample;
            chunk_samples_loaded++;
        }
        else
        {
            // i2s_config_ptr->buf_ptr[buf_pos] = dither_const;
            printf("Audio Data is longer than DMA Buffer!\n");
            printf("audio_chunk_ptr->chunk_data_pos: %i\n", audio_chunk_ptr->chunk_data_pos);
            printf("audio_chunk_ptr->chunk_len_inc_dither: %i\n", audio_chunk_ptr->chunk_len_inc_dither);
            break;
        }
    }
    size_t bytes_to_write = buf_pos * sizeof(*(audio_chunk_ptr->chunk_data_ptr));
    size_t bytes_written = 0;
    esp_err_t ret;

    ret = i2s_channel_preload_data(i2s_config_ptr->tx_chan,
                                    i2s_config_ptr->buf_ptr,
                                    bytes_to_write,
                                    &bytes_written);
    
    if(ret != ESP_OK){
        ESP_LOGE(TAG, "i2s preload failed!");
        return ESP_FAIL;
    }

    audio_chunk_ptr->chunk_data_pos = audio_chunk_ptr->chunk_data_pos - chunk_samples_loaded + bytes_written / sizeof(int32_t);

    i2s_config_ptr->preloaded = true;
    return ESP_OK;
}
