#ifndef STP_I2S_AUDIO_OPS_H
#define STP_I2S_AUDIO_OPS_H

#include "stp_sd_sdcardops.h"

typedef struct
{
    i2s_chan_handle_t    tx_chan;                           //Channel handle for current I2S channel
    int32_t*             buf_ptr;
    int buf_capacity;
    int buf_len;
    int num_dma_buf;
    int size_dma_buf;
    int ms_delay_between_writes;
    int bclk_pin;
    int ws_pin;
    int dout_pin;
    int sample_rate_Hz;
    double max_vol_dBFS;
    double min_vol_dBFS;
    double set_vol_percent;
    double vol_scale_factor;
    double min_vol_percent;
    double actual_dbFS;
    bool preloaded;
} stp_audio__i2s_config;

typedef struct {
    i2s_chan_handle_t tx_chan;
    TaskHandle_t Task_To_Notify;
} Audio_GPTimer_Args_Struct;



esp_err_t stp_audio__i2s_channel_setup(stp_audio__i2s_config* i2s_config_ptr);

esp_err_t stp_audio__i2s_channel_ISR_enable(stp_audio__i2s_config* i2s_config_ptr);          //Begin I2S transmission using custom driver function.  See update i2s_common.c and .h in this repo.  
esp_err_t stp_audio__i2s_channel_ISR_enable_finish(stp_audio__i2s_config* i2s_config_ptr);   //Keep freeRTOS happy and finish what i2s_channel_ISR_enable started in the ISR

esp_err_t stp_audio__play_audio_chunk(stp_audio__i2s_config* i2s_config_ptr, stp_sd__audio_chunk* audio_chunk_ptr, double set_vol_perc);
esp_err_t stp_audio__preload_buffer(stp_audio__i2s_config* i2s_config_ptr, stp_sd__audio_chunk* audio_chunk_ptr, double set_vol_perc);
esp_err_t stp_audio__i2s_channel_enable(stp_audio__i2s_config* i2s_config_ptr);

esp_err_t stp_audio__i2s_channel_disable(stp_audio__i2s_config* i2s_config_ptr);

#endif