#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gptimer.h"
#include <stdint.h>
#include <math.h>
#include "esp_mac.h"
#include "driver/i2s_std.h"
#include "soc/esp32/rtc.h"
#include "esp_timer.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "sdkconfig.h"
#include <stdio.h>
#include "esp_system.h" 
#include "esp_task_wdt.h"
#include "esp_sntp.h"
#include <rom/ets_sys.h>
#include "freertos/queue.h"

#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "hal/adc_types.h"





//I2S DEFINITIONS__________________________________________________________________I2S START_______________________I2S START___________________________________


#define _USE_MATH_DEFINES

/* Set 1 to allocate rx & tx channels in duplex mode on a same I2S controller, they will share 
the BCLK and WS signal
 * Set 0 to allocate rx & tx channels in simplex mode, these two channels will be totally separated,
 * Specifically, due to the hardware limitation, the simplex rx & tx channels can't be registered on the same controllers on ESP32 and ESP32-S2,
 * and ESP32-S2 has only one I2S controller, so it can't allocate two simplex channels */
#define EXAMPLE_I2S_DUPLEX_MODE         CONFIG_USE_DUPLEX

#define VOL_PIN                     GPIO_NUM_9

#define EXAMPLE_STD_WS_IO1          1      //LCK, LRC, 13 I2S word select io number
#define EXAMPLE_STD_DOUT_IO1        2     //DIN, 12 I2S data out io number
#define EXAMPLE_STD_BCLK_IO1        3      //BCK 11  I2S bit clock io number

#define BITS_IN_32BIT               2147483647       //2^31 - 1
#define VOL_PERCENT                 10

#define SAMPLE_RATE                 96000
#define DURATION_MS                 50
#define AUDIO_RISE_TIME_MS          1
#define AUDIO_FALL_TIME_MS          1
#define AUDIO_PERIOD_MS             200        //Period that audio plays

#define WAVEFORM_LEN                SAMPLE_RATE/1000*(DURATION_MS+AUDIO_RISE_TIME_MS+AUDIO_FALL_TIME_MS)*2
#define NUM_DMA_BUFF                5
#define SIZE_DMA_BUFF               2000
#define I2S_BUFF_LEN                NUM_DMA_BUFF * SIZE_DMA_BUFF

#define MAX_VOLUME_LINEAR_PERCENT   100
#define MIN_VOLUME_dBFS             -60

static i2s_chan_handle_t                tx_chan;        // I2S tx channel handler



int R_FREQUENCY_1          = 2000;
int R_FREQUENCY_2          = 0;

int R_VOL_DBFS_2           = -120;


int L_FREQUENCY_1          = 3000;
int L_FREQUENCY_2          = 0;

int L_VOL_DBFS_1           = 0;
int L_VOL_DBFS_2           = -120;


double dBFS_to_linear(int dBFS){ 
    double linear = pow(10, (double)dBFS/20);
    return linear;
}

void create_sine_wave(int32_t * waveform, int L_FREQUENCY, int R_FREQUENCY) {

    // Populate the waveform array with sine values
    int t = 0;
    double timestep = 0;
    double fall_start_time_ms = AUDIO_RISE_TIME_MS+WAVEFORM_LEN+AUDIO_FALL_TIME_MS;
    double cos2_amplitude_multiplier;
    double R_amplitude_1 = 1;
    double R_amplitude_2 = dBFS_to_linear(R_VOL_DBFS_2);
    double L_amplitude_1 = dBFS_to_linear(L_VOL_DBFS_1);
    double L_amplitude_2 = dBFS_to_linear(L_VOL_DBFS_2);

    double sine_point_R_1;
    double sine_point_R_2;
    double sine_point_R_tot;

    double sine_point_L_1;
    double sine_point_L_2;
    double sine_point_L_tot;


    //cos^2 rise fall

    for (int i = 0; i < WAVEFORM_LEN; i+=2) {
        //Define timestep
        timestep = (double)(t) / (double)SAMPLE_RATE;
        
        cos2_amplitude_multiplier = 1;
        //Setup cos2_amp_factor multipliers for cos^2 ramp
        if (timestep < AUDIO_RISE_TIME_MS){
            cos2_amplitude_multiplier = pow( cos( (M_PI/2) * (timestep / AUDIO_RISE_TIME_MS) ) , 2);
        }
        else if (timestep < AUDIO_RISE_TIME_MS+WAVEFORM_LEN){
            cos2_amplitude_multiplier = 1;
        }
        else if (timestep < fall_start_time_ms){
            cos2_amplitude_multiplier = pow( cos( (M_PI/2) * (AUDIO_FALL_TIME_MS-(timestep-fall_start_time_ms) / AUDIO_FALL_TIME_MS) ) , 2);
        }
        else{
            cos2_amplitude_multiplier = 0;
        }


        //Create right-side sine wave
        sine_point_R_1   = cos2_amplitude_multiplier * R_amplitude_1 * sin(2 * M_PI * R_FREQUENCY_1 * timestep);
        sine_point_R_2   = cos2_amplitude_multiplier * R_amplitude_2 * sin(2 * M_PI * R_FREQUENCY_2 * timestep);
        
        sine_point_R_tot = (sine_point_R_1 + sine_point_R_2) / (cos2_amplitude_multiplier * (R_amplitude_1 + R_amplitude_2));

        if(sine_point_R_tot >= BITS_IN_32BIT) printf("Error with Right side sine");


        //Create left-side sine wave
        sine_point_L_1   = cos2_amplitude_multiplier * L_amplitude_1 * sin(2 * M_PI * L_FREQUENCY_1 * timestep);  // Use 2 * PI for full sine wave cycle
        sine_point_L_2   = cos2_amplitude_multiplier * L_amplitude_2 * sin(2 * M_PI * L_FREQUENCY_2 * timestep);
    
        sine_point_L_tot = (sine_point_L_1 + sine_point_L_2) / (cos2_amplitude_multiplier * (L_amplitude_1 + L_amplitude_2));

        if(sine_point_L_tot >= BITS_IN_32BIT) printf("Error with Left side sine");

        int32_t int_point_L = (int32_t)(sine_point_L_tot*BITS_IN_32BIT);
        int32_t int_point_R = (int32_t)(sine_point_R_tot*BITS_IN_32BIT);

        waveform[i]   = int_point_L;
        waveform[i+1] = int_point_R;
        t++;
    }
    printf("Made it through sine wave function\n");
}





static void i2s_channel_setup(i2s_chan_handle_t * tx_chan_ptr)
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

    ESP_ERROR_CHECK(i2s_new_channel(&tx_chan_cfg, tx_chan_ptr, NULL));

    /* Step 2: Setting the configurations of standard mode and initialize each channels one by one
     * The slot configuration and clock configuration can be generated by the macros
     * These two helper macros is defined in 'i2s_std.h' which can only be used in STD mode.
     * They can help to specify the slot and clock configurations for initialization or re-configuring */
    i2s_std_config_t tx_std_cfg = {
    .clk_cfg    = { 
                    .sample_rate_hz = SAMPLE_RATE,
                    .clk_src = I2S_CLK_SRC_PLL_160M,
                    .mclk_multiple = I2S_MCLK_MULTIPLE_256,
                },
    .slot_cfg   = { 
                .data_bit_width = I2S_DATA_BIT_WIDTH_32BIT,
                .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
                .slot_mode = I2S_SLOT_MODE_STEREO,
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
    printf("in setup function\n");
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(*tx_chan_ptr, &tx_std_cfg));
    printf("In between setup function\n");
    // ESP_ERROR_CHECK(i2s_channel_enable(*tx_chan_ptr)); 
    printf("after setup function\n");
      

}



static void i2s_write_function(void *waveform, int32_t * w_buf, int32_t *write_time_us, int32_t start_time_us, double * volume_frac)
{    

    int32_t *audio_waveform = (int32_t*)waveform;           //Cast the waveform argumen to a 32-bit int pointer

    size_t WAVEFORM_SIZE = (int32_t)WAVEFORM_LEN * sizeof(int32_t);

    size_t w_bytes = I2S_BUFF_LEN;                         //Create variable to track how many bytes are written to the I2S DMA buffer
    size_t audio_samples_pos = 0;                           // Keep track of where we are in the audio data

    double dBFS = -(*volume_frac-1) * MIN_VOLUME_dBFS;
    double audio_vol_linear = pow(10.0, dBFS / 20.0);

    /*Here we iterate through each index in the audio waveform, and assign the value to the wbuf*/
    while (audio_samples_pos<WAVEFORM_LEN) {
        w_buf[audio_samples_pos] = audio_vol_linear*(audio_waveform[audio_samples_pos]);
        audio_samples_pos++;
        }

    /*Iterate through and write wbuf to I2S DMA buffer.  If len(wbuf) were > than I2S buff size, 
    we would use the wbytes variable to move along wbuf and start a new write at the position where the 
    last one left off.  That's not the case here, though*/
    // for (int tot_bytes = 0; tot_bytes < WAVEFORM_SIZE; tot_bytes += w_bytes){
    *write_time_us = esp_timer_get_time() - start_time_us;
    i2s_channel_write(tx_chan, w_buf, WAVEFORM_SIZE, &w_bytes, DURATION_MS);

    // };    

}




//GPTimer DEFINITIONS__________________________________________________________________GPTimer START_______________________GPTimer END___________________________________


typedef struct{                                         // This is a structure used to hold all relevant data needed for writing to the I2S DMA buffers
    int32_t*            audio_waveform_data_ptr;        //Pointer to array holding audio data
    int32_t*            audio_write_buffer_ptr;         //Pointer to array actign as intermediate write buffer
    double*             audio_volume_linear_ptr;        //Pointer to audio volume in linear percent (direct multiplication, already converted from dB)
    int*                signal_idx_ptr;                 //Index used for debugging
    size_t              audio_samples_position;         //Current position in audio data array
    size_t              buffer_position;                //Current position in buffer array
    size_t              words_written_to_buffer;        //Placeholder for how many bytes were actually written by i2s functions
    size_t              length_of_audio_write_buffer;    
    size_t              length_of_audio_waveform;
    i2s_chan_handle_t   chan;                           //Channel handle for current I2S channel
    QueueHandle_t       i2s_queue;                      //Handle for queue used to communicate between GPTimer callback and i2s_play_task
    gptimer_handle_t    gptimer_handle;
}   sound_struct;

typedef struct{                                         //This structure is used to hold data communicated between the gptimer i2s callback and the i2s_play_task
    int32_t send_time_us;
}   i2s_passthrough_struct;


/*This performs the real-time initiation of audio transmission upon triggering of a GPTimer callback
This uses a custom, ISR-safe I2s_channel enable function to start the audio transmission instantly.
In order to work, the DMA buffers must have already been pre-loaded with audio data from the i2s_play_task.
At the end of the function, we send information to a queue which will unblock the i2s_play_task and allow it to run again.
The i2s_play_task must run directly after to finish up the audio file (This only starts the transmission is good for one DMA-buffer-length
worth of audio data) and to preload audio data for the next burst*/
static bool IRAM_ATTR i2s_enable_gptimer_callback(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx)
{

    sound_struct *i2s_def_ptr = (sound_struct*)user_ctx;    //Cast the argument pointer into a sound_struct type

    i2s_channel_ISR_enable(i2s_def_ptr->chan);              //Begin I2S transmission using custom driver function.  See update i2s_common.c and .h in this repo.  

    BaseType_t         high_task_awoken  = pdFALSE;
    QueueHandle_t      i2s_gptimer_queue = i2s_def_ptr->i2s_queue;

    i2s_passthrough_struct passthrough_data = {             //Get information to send to i2s_play_task
        .send_time_us = esp_timer_get_time()
    };

    xQueueSendFromISR(i2s_gptimer_queue, &passthrough_data, &high_task_awoken);
    return high_task_awoken == pdTRUE;
}


/*This is the main audio handling task function, which runs right after the i2s_enable_gptimer_callback
  and performs the less real time aspects of the audio transmission.  Including continuing to play audio if the 
  file length exceeds that of the DMA buffers and preloading data into the buffers for the next audio burst*/
static void i2s_play_task(void * user_ctx){     

    sound_struct * i2s_def_ptr    = (sound_struct*)user_ctx;

    int32_t*           write_buff_ptr   = i2s_def_ptr->audio_write_buffer_ptr;
    int32_t*           audio_data_ptr   = i2s_def_ptr->audio_waveform_data_ptr;
    double*            volume_lin_ptr   = i2s_def_ptr->audio_volume_linear_ptr;

    size_t             audio_pos        = i2s_def_ptr->audio_samples_position;
    size_t             buff_pos         = i2s_def_ptr->buffer_position;
    size_t             buff_len         = i2s_def_ptr->length_of_audio_write_buffer;
    size_t             words_written    = i2s_def_ptr->words_written_to_buffer;
    size_t             audio_len        = i2s_def_ptr->length_of_audio_waveform;
    i2s_chan_handle_t  i2s_chan         = i2s_def_ptr->chan;
    QueueHandle_t      i2s_queue        = i2s_def_ptr->i2s_queue;
    size_t             words_to_write   = 0;
    gptimer_handle_t   gptimer_handle   = i2s_def_ptr->gptimer_handle;

    i2s_passthrough_struct passthrough_data;

    audio_pos     = 0;                                                  //Prepare for next ISR to trigger by resetting audio_pos to 0
    words_written = 0;                                                  //words_written to 0
    buff_pos      = 0;           

    while (buff_pos < buff_len && audio_pos < audio_len){        //Filling up a new buffer
        write_buff_ptr[audio_pos] = *volume_lin_ptr * audio_data_ptr[audio_pos];
        audio_pos++;
        buff_pos++;
        words_to_write++;
    }
    buff_pos = 0;                       //Set to 0 now in case I forget to this this later:)

    do{
        ESP_ERROR_CHECK(i2s_channel_preload_data(i2s_chan, write_buff_ptr, words_to_write, &words_written)); //And pre-loading the buffer to transmit instantly upon the next ISR call
    }
    while(words_written == words_to_write);


    /*This is the main part of the task that will run continuously, blocking until the ISR is triggered*/

    while (true){
        if(xQueueReceive(i2s_queue, &passthrough_data, portMAX_DELAY)){     //Wait forever until audio ISR triggers

            // printf("\nISR triggered and task now running\n");
        

            //THE ISSUE IS WITH THIS FUNCTION!!
            i2s_channel_ISR_enable_finish(i2s_chan);                            //Keep freeRTOS happy and finish what i2s_channel_ISR_enable started in the ISR
    
            while(audio_pos < audio_len){                                       //While the audio has not been fully transmitted (position in the audio file is less than the audio file's length)
                buff_pos = 0;
                words_to_write = 0;
            printf("\nHERE | Audio pos: %i | Audio len: %i |Words Written Last time: %i \n", audio_pos, audio_len, words_written);
                while (buff_pos < buff_len && audio_pos < audio_len){                                    //overwrite to create a new buffer of the next buff_len number of audio samples
                    write_buff_ptr[buff_pos] = *volume_lin_ptr * audio_data_ptr[audio_pos];
                    audio_pos++;
                    buff_pos++;
                    words_to_write++;
                }
                buff_pos = 0;                                                   //Set to 0 now in case I forget to this this later:)            
                                                                                //Write the new buffer to the i2s bus
                // i2s_channel_write(i2s_chan, write_buff_ptr, words_to_write*sizeof(int32_t), &words_written, portMAX_DELAY); 
            }
            printf("Audio pos: %i | Audio len: %i\n | Words Written Last time: %i \n", audio_pos, audio_len, words_written);


            i2s_channel_disable(i2s_chan);                                      //Audio file has been fully transmitted.  Disable the channel to allow new data to be preloaded

            audio_pos     = 0;                                                  //Prepare for next ISR to trigger by resetting audio_pos to 0
            words_written = 0;                                                  //words_written to 0

            buff_pos = 0;           
            while (buff_pos < buff_len && audio_pos < audio_len){        //Filling up a new buffer
                write_buff_ptr[audio_pos] = *volume_lin_ptr * audio_data_ptr[audio_pos];
                audio_pos++;
                buff_pos++;
                words_to_write++;
            }
            buff_pos = 0;                       //Set to 0 now in case I forget to this this later:)

            do{     //change the siz of the words_to_write arg
                ESP_ERROR_CHECK(i2s_channel_preload_data(i2s_chan, write_buff_ptr, words_to_write*sizeof(int32_t), &words_written)); //And pre-loading the buffer to transmit instantly upon the next ISR call
                printf("Words written: %i | words_to_write: %i\n", words_written, words_to_write);
            }
            while(words_written == words_to_write);
        }
    }
}



//GPTimer DEFINITIONS__________________________________________________________________GPTimer END_______________________GPTimer END___________________________________


//DAC Read Task

void dac_read_vol_battery_task(void * audio_volume1){
    //Setup Analog read of pot on GPIO9
    uint32_t volt_array[30];
    double prev_rounded_avg_volt = 0;
    int interval_in_mV = 10;
    int num_samples = 30;
    int max_rounded_voltage = 3080;
    double * audio_volume = (double*)audio_volume1;

    adc1_config_channel_atten(ADC1_CHANNEL_7, ADC_ATTEN_DB_12);
    esp_adc_cal_characteristics_t adc1_chars;
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_12, ADC_WIDTH_BIT_DEFAULT, 0, &adc1_chars);       
    adc1_config_width(ADC_WIDTH_BIT_DEFAULT);

    while(true){
        uint32_t sum_volt_array = 0;
        for (int i=0; i<num_samples; i++){
            volt_array[i] = esp_adc_cal_raw_to_voltage(adc1_get_raw(ADC1_CHANNEL_7), &adc1_chars);
            sum_volt_array += volt_array[i];
        }
        double avg_volt = (double)sum_volt_array / (double)num_samples;
        double rounded_avg_volt = round(avg_volt/interval_in_mV)*interval_in_mV;
        if (fabs(rounded_avg_volt - prev_rounded_avg_volt)<=interval_in_mV && rounded_avg_volt!=max_rounded_voltage){
            rounded_avg_volt = prev_rounded_avg_volt;
        }
        prev_rounded_avg_volt = rounded_avg_volt;

        double voltage_fraction = rounded_avg_volt / max_rounded_voltage;

        if (voltage_fraction==0) *audio_volume = 0;
        // else if (voltage_fraction >=.9) *audio_volume = 100;
        else{
        *audio_volume = (voltage_fraction);
        }

        double dBFS = -(voltage_fraction-1) * MIN_VOLUME_dBFS;
        double audio_vol_linear = pow(10.0, dBFS / 20.0);
        printf("dBFS: %.1f dB \n", dBFS);
        printf("Audio Linear Percent: %.1f%%", audio_vol_linear*100);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}



void app_main(void)
{

    //I2S Main Function______________________________________________________I2S MAIN START________________________________RMT MAIN START______________________

    int64_t start_time = esp_timer_get_time();
    double start_time_ms  = (double)(start_time) / (double)(1000);
    printf("Current Time:  %0.10f ms\n", start_time_ms);


    int32_t *wave = calloc(WAVEFORM_LEN, sizeof(int32_t));
    int32_t *w_buf = calloc(I2S_BUFF_LEN, sizeof(int32_t));   //Allocate memory for the I2S write buffer

    // Create the sine wave
    create_sine_wave(wave, L_FREQUENCY_1, R_FREQUENCY_1);

    i2s_channel_setup(&tx_chan);

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
    .reload_count = -1, // alarm will reload indefinitely
    .alarm_count = AUDIO_PERIOD_MS * 1e3, // period = 2s @resolution 1MHz
    .flags.auto_reload_on_alarm = true, // enable auto-reload
    };

    ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config));

    gptimer_event_callbacks_t cbs = {
        .on_alarm = i2s_enable_gptimer_callback, // register user callback
    };

    size_t WAVEFORM_SIZE = (int32_t)WAVEFORM_LEN * sizeof(int32_t);

    double volume_frac = 0;

    QueueHandle_t i2s_gptimer_queue = NULL;

    i2s_gptimer_queue = xQueueCreate(1, sizeof( i2s_passthrough_struct* ) );

    sound_struct i2s_waveform_definition_struct = {
        .audio_waveform_data_ptr        = wave,
        .length_of_audio_waveform       = WAVEFORM_SIZE,
        .length_of_audio_write_buffer   = I2S_BUFF_LEN,
        .audio_write_buffer_ptr         = w_buf,
        .audio_volume_linear_ptr        = &volume_frac,
        .audio_samples_position         = 0,
        .buffer_position                = 0,
        .words_written_to_buffer        = 0,
        .signal_idx_ptr                 = 0,
        .chan                           = tx_chan,
        .i2s_queue                      = i2s_gptimer_queue,
        .gptimer_handle                 = gptimer
    };


    ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, &i2s_waveform_definition_struct));
    ESP_ERROR_CHECK(gptimer_enable(gptimer));
    ESP_ERROR_CHECK(gptimer_start(gptimer));

    //GPTimer Main Function__________________________________________________GPTimer_____________________________________GPTimer_START
    TaskHandle_t voltage_task_handle = NULL;
    TaskHandle_t i2s_play_task_handle = NULL;

    uint32_t voltage_task_stack_depth = 4096;
    uint32_t i2s_play_task_stack_depth = 4096;



    xTaskCreate(dac_read_vol_battery_task,
                "Volume_Knob_Read",
                voltage_task_stack_depth,
                &volume_frac,
                1,
                &voltage_task_handle);

    xTaskCreate(i2s_play_task,
                "I2S_Play_Task:",
                i2s_play_task_stack_depth,
                &i2s_waveform_definition_struct,
                5,
                &i2s_play_task_handle);

    while(true){
        printf("Running...\n");
        // printf("Number of gpTimer Calls: %i\n", *(i2s_waveform_definition_struct.signal_idx_ptr));
        vTaskDelay(pdMS_TO_TICKS(1000));
    }


    //     /*Find current time and period of last loop*/
    // int32_t start_time_us = esp_timer_get_time();
    // int32_t curr_time_us = start_time_us;
    // int32_t curr_time1_us = start_time_us;
    // int32_t curr_time2_us = start_time_us;
    // int32_t period_us = 0;
    // int32_t last_time_us = start_time_us;
    // int32_t write_time_us_main = start_time_us;
    // int32_t WRITE_TRIM_US = 7.5e3;
    // float period_ms;
    // int j=0;
    // size_t bytes_loaded;
    // UBaseType_t uxHighWaterMark;

    // printf("Pre disabling channel\n");


    // while (j<300) {
    //     curr_time_us = (esp_timer_get_time()) - start_time_us;   
    //     period_us = curr_time_us - last_time_us;
    //     period_ms = (float)period_us / (float)1000;
    //     last_time_us = curr_time_us;
    //     curr_time1_us = (esp_timer_get_time()) - start_time_us;

    //     // i2s_write_function(wave, w_buf, &write_time_us_main, start_time_us, &volume_frac);


    //     int32_t *audio_waveform = (int32_t*)wave;           //Cast the waveform argumen to a 32-bit int pointer


    //     size_t w_bytes = I2S_BUFF_LEN;                         //Create variable to track how many bytes are written to the I2S DMA buffer
    //     size_t audio_samples_pos = 0;                           // Keep track of where we are in the audio data

    //     double dBFS = -(volume_frac-1) * MIN_VOLUME_dBFS;
    //     double audio_vol_linear = pow(10.0, dBFS / 20.0);

    //     /*Here we iterate through each index in the audio waveform, and assign the value to the wbuf*/
    //     while (audio_samples_pos<WAVEFORM_LEN) {
    //         w_buf[audio_samples_pos] = audio_vol_linear*(audio_waveform[audio_samples_pos]);
    //         audio_samples_pos++;
    //         }

    //     /*Iterate through and write wbuf to I2S DMA buffer.  If len(wbuf) were > than I2S buff size, 
    //     we would use the wbytes variable to move along wbuf and start a new write at the position where the 
    //     last one left off.  That's not the case here, though*/
    //     // for (int tot_bytes = 0; tot_bytes < WAVEFORM_SIZE; tot_bytes += w_bytes){
    //     write_time_us_main = esp_timer_get_time() - start_time_us;

    //     ESP_ERROR_CHECK(i2s_channel_disable(tx_chan));
    //     bytes_loaded = 0;      
    //     printf("Pre pre-loading channel\n");

    //     do{
    //        ESP_ERROR_CHECK(i2s_channel_preload_data(tx_chan, w_buf, WAVEFORM_SIZE, &bytes_loaded));
    //     }
    //     while(bytes_loaded == I2S_BUFF_LEN);

    //     printf("Pre enabling channel\n");


    //     // ESP_ERROR_CHECK(i2s_channel_write(tx_chan, w_buf, WAVEFORM_SIZE, &w_bytes, DURATION_MS));
    //     printf("Audio should be playing\n");
    //     ets_delay_us(500e3);
    //     printf("Audio should have played\n");
    //     ets_delay_us(1000e3);
    //     printf("Now, channel is enabled\n");




    //     // ets_delay_us(DURATION_MS*1000+5e3+WRITE_TRIM_US-(esp_timer_get_time()-write_time_us_main-start_time_us));

    //     uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL );

    //     j++;
    //     printf("Bytes Loaded: %i\n", bytes_loaded);
    //     printf("Task Stack Usage: %i\n", uxHighWaterMark);
    //     printf("Period: %0.3f ms\n",period_ms);
    //     printf("Volume: %0.2f%%\n\n", volume_frac*100);
    //     curr_time2_us = (esp_timer_get_time()) - start_time_us; 
    //     i2s_channel_ISR_enable_finish(tx_chan);

    //     ets_delay_us(5000e3);
    //     // ets_delay_us(200e3 - (curr_time2_us - curr_time_us));
    // }

}