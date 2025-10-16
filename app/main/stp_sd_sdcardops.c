/* SD card_ptr and FAT filesystem example.
   This example uses SPI peripheral to communicate with SD card_ptr.
*/

//This program is written by David Earley (2025) 
//This program borrows heavily from this article and the Espressif examples throughout:
//https://truelogic.org/wordpress/2015/09/04/parsing-a-wav-file-in-c/

#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "sdmmc_cmd.h"
#include "sd_test_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h> 
#include <inttypes.h>
#include "esp_random.h"
#include "bootloader_random.h"
#include "math.h"
#include "esp_vfs_fat.h"

#include "stp_sd_sdcardops.h"
#include <esp_log.h>

#ifndef CONFIG_LOG_MAXIMUM_LEVEL
#define CONFIG_LOG_MAXIMUM_LEVEL 3
#endif

esp_err_t stp_sd__mount_sd_card(stp_sd__spi_config* spi_config_ptr){

    char* TAG = "mount sd";
    esp_err_t ret;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    ESP_LOGI(TAG, "Initializing SD card_ptr");
    ESP_LOGI(TAG, "Using SPI peripheral");

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    spi_config_ptr->host = host;
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = spi_config_ptr->mosi_di_pin,
        .miso_io_num = spi_config_ptr->miso_do_pin,
        .sclk_io_num = spi_config_ptr->clk_pin,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    
    ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        ESP_ERROR_CHECK(spi_bus_free(host.slot));
        ESP_ERROR_CHECK(spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA));
        // return ESP_FAIL;
    }
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = spi_config_ptr->cs_pin;
    slot_config.host_id = (spi_config_ptr->host).slot;

    ESP_LOGI(TAG, "Mounting filesystem");
    ret = esp_vfs_fat_sdspi_mount(spi_config_ptr->mount_point, &(spi_config_ptr->host), &slot_config, &mount_config, &(spi_config_ptr->card_ptr));

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem. "
                     "If you want the card to be formatted, set the CONFIG_EXAMPLE_FORMAT_IF_MOUNT_FAILED menuconfig option.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                     "Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));
        }
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Filesystem mounted");
    // Card has been initialized, print its properties
    sdmmc_card_print_info(stdout, spi_config_ptr->card_ptr);
    //AI-generated segment--scan for available files and print
    DIR *dir = opendir("/sdcard");
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            printf("Found file: %s\n", ent->d_name);
        }
        closedir(dir);
    } else {
        printf("Failed to open directory!");
    }
    spi_config_ptr->open = true;
    return ESP_OK;
}

esp_err_t stp_sd__unmount_sd_card(stp_sd__spi_config* spi_config_ptr){
    // All done, unmount partition and disable SPI peripheral
    char* TAG = "unmount SD";

    if(esp_vfs_fat_sdcard_unmount(spi_config_ptr->mount_point, spi_config_ptr->card_ptr)!=ESP_OK){
        ESP_LOGE(TAG, "Error unmounting SPI Bus!");
        return ESP_FAIL;
    }

    //deinitialize the bus after all devices are removed
    if(spi_bus_free((spi_config_ptr->host).slot) != ESP_OK){
        ESP_LOGE(TAG, "Error freeing SPI bus");
        return ESP_FAIL;
    }
    else{
        spi_config_ptr->open = false;
        memset(spi_config_ptr, 0, sizeof(*spi_config_ptr)); //Reset all members of wave file to 0;
    }

    return ESP_OK;
}

esp_err_t stp_sd__open_audio_file(stp_sd__wavFile* wave_file_ptr)
{
    char* TAG = "open audio file";
    //Open file for reading
    wave_file_ptr->fp = fopen(strcat("/sdcard", wave_file_ptr->filename), "rb");
    printf("here\n");

    if(wave_file_ptr->fp==NULL){
        ESP_LOGE(TAG, "File not opened!");
        return ESP_FAIL;
    }

    if (wave_file_ptr->fp==NULL){
        ESP_LOGE(TAG, "File not Opened!");
        return ESP_FAIL;
    }
    else{
        ESP_LOGI(TAG, "File Opened Successfully!");
    }

    int count = 0;
    int num_objs_to_read= 4;
    //read RIFF data
	count = fread(wave_file_ptr->RIFF, sizeof(*(wave_file_ptr->RIFF)), num_objs_to_read, wave_file_ptr->fp);
	ESP_LOGI(TAG, "RIFF: %.*s", (int)sizeof(*(wave_file_ptr->RIFF))*num_objs_to_read, wave_file_ptr->RIFF);
    
    if(memcmp("RIFF", wave_file_ptr->RIFF, sizeof(*(wave_file_ptr->RIFF)))){
        ESP_LOGE(TAG, "RIFF does not match!");
        printf("Count: %i\n", count);
    }

    //read filesize
    num_objs_to_read = 1;
    count = fread(&(wave_file_ptr->filesize), sizeof(wave_file_ptr->filesize), num_objs_to_read, wave_file_ptr->fp);
	ESP_LOGI(TAG, "filesize: %li bytes", wave_file_ptr->filesize);

    //read WAVE marker and check that it is "WAVE"
    num_objs_to_read = 4;
    count = fread(wave_file_ptr->WAVE, sizeof(*(wave_file_ptr->WAVE)), num_objs_to_read, wave_file_ptr->fp);
    ESP_LOGI(TAG, "WAVE: %.*s", (int)sizeof(*(wave_file_ptr->WAVE))*num_objs_to_read, wave_file_ptr->WAVE);
    if(memcmp("WAVE", wave_file_ptr->WAVE, sizeof(wave_file_ptr->WAVE))){
        ESP_LOGE(TAG, "WAVE not read successfully!");
        return ESP_FAIL;
    }

    //read fmt marker and check that it is "fmt "
    num_objs_to_read = 4;
    count = fread(wave_file_ptr->fmt, sizeof(*(wave_file_ptr->fmt)), num_objs_to_read, wave_file_ptr->fp);
    ESP_LOGI(TAG, "fmt: %.*s", (int)sizeof(*(wave_file_ptr->fmt))*num_objs_to_read, wave_file_ptr->fmt);
    if(memcmp("fmt ", wave_file_ptr->fmt, sizeof(wave_file_ptr->fmt))){
        ESP_LOGE(TAG, "fmt not read successfully!");
        return ESP_FAIL;
    }

    //read length of fmt data
    num_objs_to_read = 1;
    count = fread(&wave_file_ptr->fmt_length, sizeof(wave_file_ptr->fmt_length), num_objs_to_read, wave_file_ptr->fp);
    ESP_LOGI(TAG, "fmt length: %li", wave_file_ptr->fmt_length);
       
    //read type of fmt data
    num_objs_to_read = 1;
    count = fread(&wave_file_ptr->fmt_type, sizeof(wave_file_ptr->fmt_type), num_objs_to_read, wave_file_ptr->fp);
    ESP_LOGI(TAG, "fmt type: %i", wave_file_ptr->fmt_type);
    if(wave_file_ptr->fmt_type != 1){               //1 specifies that file is in PCM format (which is required)
        ESP_LOGE(TAG, "File must be PCM format!");
        return ESP_FAIL;
    }
    //read number of channels
    num_objs_to_read = 1;
    count = fread(&(wave_file_ptr->num_channels), sizeof(wave_file_ptr->num_channels), num_objs_to_read, wave_file_ptr->fp);
    ESP_LOGI(TAG, "Number of Channels: %i", wave_file_ptr->num_channels);
    if(wave_file_ptr->num_channels != 2){       //Must be 2 channels for stereo audio output
        ESP_LOGE(TAG, "Audio must be stereo!");
        return ESP_FAIL;
    }
    //read sample rate
    num_objs_to_read = 1;
    count = fread(&wave_file_ptr->sample_rate, sizeof(wave_file_ptr->sample_rate), num_objs_to_read, wave_file_ptr->fp);
    ESP_LOGI(TAG, "Sample Rate: %li Hz", wave_file_ptr->sample_rate);
    if(wave_file_ptr->sample_rate != 96000){
        ESP_LOGE(TAG, "Sample Rate must be 96000 Hz!");
        return ESP_FAIL;
    }
    //read (Sample Rate * BitsPerSample * Channels) / 8
    num_objs_to_read = 1;
    count = fread(&(wave_file_ptr->SampleRateBitsPerSampleChannels_8), sizeof(wave_file_ptr->SampleRateBitsPerSampleChannels_8), num_objs_to_read, wave_file_ptr->fp);
    ESP_LOGI(TAG, "(Sample Rate * BitsPerSample * Channels) / 8: %li", wave_file_ptr->SampleRateBitsPerSampleChannels_8);

    //read (BitsPerSample * Channels) / 8.1
    num_objs_to_read = 1;
    count = fread(&(wave_file_ptr->BitsPerSampleChannels_8_1), sizeof(wave_file_ptr->BitsPerSampleChannels_8_1), num_objs_to_read, wave_file_ptr->fp);
    ESP_LOGI(TAG, "(BitsPerSample * Channels) / 8.1: %i", wave_file_ptr->BitsPerSampleChannels_8_1);

    //read bitness
    num_objs_to_read = 1;
    count = fread(&(wave_file_ptr->bitness), sizeof(wave_file_ptr->bitness), num_objs_to_read, wave_file_ptr->fp);
    ESP_LOGI(TAG, "Bits per Sample: %i", wave_file_ptr->bitness);
    if(wave_file_ptr->bitness != 32){
        ESP_LOGE(TAG, "Audio must be 32 bit audio!");
        return ESP_FAIL;
    }
    //read data chunk header
    num_objs_to_read = 4;
    count = fread(wave_file_ptr->data_header, sizeof(*(wave_file_ptr->data_header)), num_objs_to_read, wave_file_ptr->fp);
    if(memcmp("data", wave_file_ptr->data_header, sizeof(*(wave_file_ptr->data_header)))){
        ESP_LOGE(TAG, "Data header does not match!");
    }
    ESP_LOGI(TAG, "data: %.*s", (int)sizeof(*(wave_file_ptr->data_header))*num_objs_to_read, wave_file_ptr->data_header);
    //read data size
    num_objs_to_read = 1;
    count = fread(&(wave_file_ptr->data_size), sizeof(wave_file_ptr->data_size), num_objs_to_read, wave_file_ptr->fp);
    ESP_LOGI(TAG, "Data Size: %li bytes", wave_file_ptr->data_size);

    //Get location in file where audio data starts, in number of bytes from beginning of file (this works because we open the file in binary mode)
    wave_file_ptr->audiofile_data_start_pos = ftell(wave_file_ptr->fp);
    ESP_LOGI(TAG, "Audio Data Start position: %li bytes", wave_file_ptr->audiofile_data_start_pos);

    //Calculate location in file of final audio data point, in number of bytes from the beginning of the file
    wave_file_ptr->audiofile_data_end_pos = wave_file_ptr->audiofile_data_start_pos + (wave_file_ptr->data_size) - 1;
    wave_file_ptr->audio_len = wave_file_ptr->data_size / sizeof(int32_t);
    ESP_LOGI(TAG, "Audio Data End position: %li bytes", wave_file_ptr->audiofile_data_end_pos);

    return ESP_OK;
}


// /*The below function selects a "chunk" of specified length from the wave file, beginning at a random start point, with padding after the beginning and before the ending

//     The selection from the file goes as follows:

//                                           M            N                              O                                            P
//                                           |            |                              |                                            |                      EOF                                                                                                                    
//     |<---HEADER-->|<------------------------------------AUDIO DATA IN WAVE FILE--------------------------------------------------------------------------->|
//     |             |<-padding_num_samples->|<-------------------------area of wav file audio eligible for selection---------------->|<-padding_num_samples->|
//     |             |                       |           |<---randomly selected chunk--->|                                            |                       |
//                                           |           |<-----chunk_len_wo_dither----->|                                            |                       |

//     Where the indices are as follows, referenced with respect to the beginning of the total wave file:
//         M: start_file_pos_samples   (units: bytes)
//         N: audio_chunk_ptr->start_idx   (units: int32_t words)
//         O: audio_chunk_ptr->end_idx     (units: int32_t words)
//         P: end_file_pos_samples     (units: bytes)
    

//     The randomly selected chunk is then processed into the following portions:
//         1. Starting dither (Needed to prevent popping with the PCM5102a I2S chip)   (Starting with A_idx)
//         2. Starting cosine ramp (This is the start of the actual audio)             (Starting with B_idx)
//         3. Full scale audio output                                                  (Starting with C_idx)
//         4. Falling cosine ramp                                                      (Starting with D_idx)
//         5. Ending dither                                                            (Starting with E_idx)
    
//     The last sample is at index (F_idx - 1).

//     Where the notes A-F denote indicies in the chunk of the start and end of each segment
//     |-----chunk_data_pos------------------------------------------------------------------------------------------------|            
//                 v
//     |<-----------------------------------------------chunk_len_inc_dither---------------------------------------------->|
//                          |<--------------------------chunk_len_wo_dither---------------------->|                       
//     ____________________________________________________________________________________________________________________
//     A                    B                       C                         D                       E                    F
//     | Start Dither       | Cosine Rise           | Full scale audio output | Cosine Fall           | End Dither         |
//     | dither_num_samples | rise_fall_num_samples |                         | rise_fall_num_samples | dither_num_samples |
                                                    
/*                                                  __________________________
                                                   /                           \
                                           /                                         \
                                   /                                                        \
       _____________________/                                                                      \_____________________
*/

esp_err_t stp_sd__init_audio_chunk(stp_sd__audio_chunk_setup* audio_chunk_setup_ptr, stp_sd__audio_chunk* audio_chunk_ptr){
    char* TAG = "audio_chunk_ptr_const";

    int A_idx = 0;
    int B_idx = A_idx + audio_chunk_setup_ptr->pre_dither_num_samples;
    int C_idx = B_idx + audio_chunk_setup_ptr->rise_fall_num_samples;
    int D_idx = B_idx + audio_chunk_setup_ptr->chunk_len_wo_dither - audio_chunk_setup_ptr->rise_fall_num_samples;
    int E_idx = B_idx + audio_chunk_setup_ptr->chunk_len_wo_dither;
    int F_idx = E_idx + audio_chunk_setup_ptr->post_dither_num_samples;
    
    stp_sd__wavFile* wave_file_ptr = audio_chunk_setup_ptr->wavFile_ptr;
    // printf("A_idx: %i | B_idx: %i | C_idx: %i | D_idx: %i | E_idx: %i | F_idx: %i", A_idx, B_idx, C_idx, D_idx, E_idx, F_idx);

    audio_chunk_ptr->chunk_len_inc_dither = F_idx - A_idx;
    audio_chunk_ptr->chunk_size = (audio_chunk_ptr->chunk_len_inc_dither) * sizeof(*(audio_chunk_ptr->chunk_data_ptr));
    audio_chunk_ptr->A_idx = A_idx;
    audio_chunk_ptr->B_idx = B_idx;
    audio_chunk_ptr->C_idx = C_idx;
    audio_chunk_ptr->D_idx = D_idx;
    audio_chunk_ptr->E_idx = E_idx;
    audio_chunk_ptr->F_idx = F_idx;
    audio_chunk_ptr->capacity                 = audio_chunk_setup_ptr->chunk_buf_size_bytes;
    audio_chunk_ptr->wavFile_ptr              = wave_file_ptr;
    audio_chunk_ptr->reload_audio_buff_Queue  = audio_chunk_setup_ptr->reload_audio_buff_Queue;
    audio_chunk_ptr->reload_memory_struct_ptr = audio_chunk_setup_ptr->reload_memory_struct_ptr;

    /*Get locations of the start- and end- idxs of the allowable space where we can take our audio file from, in no. of bytes from beginning of file, that we can grab our chunk from.*/
    long int start_file_pos_samples = ((wave_file_ptr->audiofile_data_start_pos)/sizeof(int32_t)) + audio_chunk_ptr->padding_num_samples;
    long int end_file_pos_samples   = ((wave_file_ptr->audiofile_data_end_pos)/sizeof(int32_t))   - audio_chunk_ptr->padding_num_samples - audio_chunk_ptr->chunk_len_wo_dither;
    long int delta_file_pos_samples = end_file_pos_samples - start_file_pos_samples;

    if (delta_file_pos_samples < audio_chunk_ptr->chunk_size)
    {
        ESP_LOGE(TAG, "Audio chunk is longer than available audio data!  Increase file length, or decrease chunk length or chunk padding.");
        return ESP_FAIL;
    }

    audio_chunk_ptr->chunk_len_wo_dither     = audio_chunk_setup_ptr->chunk_len_wo_dither;
    audio_chunk_ptr->rise_fall_num_samples   = audio_chunk_setup_ptr->rise_fall_num_samples;
    audio_chunk_ptr->padding_num_samples     = audio_chunk_setup_ptr->padding_num_samples;
    audio_chunk_ptr->pre_dither_num_samples  = audio_chunk_setup_ptr->pre_dither_num_samples;
    audio_chunk_ptr->post_dither_num_samples = audio_chunk_setup_ptr->post_dither_num_samples; 
    audio_chunk_ptr->start_file_pos_samples  = start_file_pos_samples;
    audio_chunk_ptr->end_file_pos_samples    = end_file_pos_samples;
    audio_chunk_ptr->delta_file_pos_samples  = delta_file_pos_samples;
    audio_chunk_ptr->memory_buffer_pos       = 0;

    audio_chunk_ptr->chunk_data_ptr   = malloc(audio_chunk_ptr->capacity);
    assert(audio_chunk_ptr->chunk_data_ptr);
    audio_chunk_ptr->chunk_load_ptr   = malloc(audio_chunk_ptr->capacity);
    assert(audio_chunk_ptr->chunk_load_ptr);
    int max_rise_fall_samples = 1e9;
    if(audio_chunk_setup_ptr->rise_fall_num_samples <= max_rise_fall_samples)
    {
        if (audio_chunk_setup_ptr->rise_fall_num_samples > 0)
        {
            audio_chunk_ptr->cos_ramp_LUT_ptr = malloc(audio_chunk_setup_ptr->rise_fall_num_samples*sizeof(int32_t));
            assert(audio_chunk_ptr->cos_ramp_LUT_ptr);
            for(int i=0; i<audio_chunk_ptr->rise_fall_num_samples; i++)
            {
                float rise_frac    = (float)1 - ((float)i / (float)(audio_chunk_ptr->rise_fall_num_samples));
                float cos_ramp     = ((float)1 + cos(rise_frac * M_PI))/2;
                audio_chunk_ptr->cos_ramp_LUT_ptr[i] = cos_ramp;
            }
        }
    }
    else{
        ESP_LOGE(TAG, "No more than %i samples allowed for cos^2 rise/fall!", max_rise_fall_samples);
        return ESP_FAIL;
    }

    if(wave_file_ptr->fp == NULL)
    {
        ESP_LOGE(TAG, "Wave file not open!");
        return ESP_FAIL;
    }
    return ESP_OK;
}


esp_err_t stp_sd__get_new_audio_chunk(stp_sd__audio_chunk* audio_chunk_ptr, stp_sd__wavFile* wave_file_ptr, bool use_random){
    
    char* TAG = "get_new_audio_chunk";
    int delta_file_pos_samples = audio_chunk_ptr->delta_file_pos_samples;
    int start_file_pos_samples = audio_chunk_ptr->start_file_pos_samples;
   
    double random = 0;
    if(use_random)  //Choose a single seed for the start position of the audio chunk for debugging, or randomly select for actual use
    {
        bootloader_random_enable();
        random = (double)esp_random() / (double)(pow(2,32)-1);  //convert from uint32_t to double between 0 and 1
        bootloader_random_disable();
    }
    else
    {
        random = 0.5;       //Start from halfway through the file
    }

    long int  chunk_start_pos_samples = (round(delta_file_pos_samples * random) + start_file_pos_samples);

    if (chunk_start_pos_samples  % 2 != 0){
        chunk_start_pos_samples += 1;    //We must start with an even index so that our right and left channels always line up properly.  It's okay to eat into 1 sample of our padding.
    }

    long chunk_start_pos_filebytes = (chunk_start_pos_samples*sizeof(int32_t));      //The location in the file where our audio chunk will begin
    
    //Move file position pointer to random start point found with chunk_start_pos_samples
    int ret = fseek(wave_file_ptr->fp, chunk_start_pos_filebytes, SEEK_SET);
    if (ret != ESP_OK){     //Set file position to the one determined by the random selection
        ESP_LOGE(TAG, "Error setting new file position!");
        return ESP_FAIL;
    }

    //removed "+B_idx" from the pointer in audio_chunk_ptr ->chunk_data_ptr.  TODO don't forget about this
    int num_samples_read = fread((audio_chunk_ptr->chunk_data_ptr), sizeof(int32_t), audio_chunk_ptr->capacity/sizeof(int32_t), wave_file_ptr->fp); //Fill center of audio chunk
    if(num_samples_read < 0){
        ESP_LOGE(TAG, "Not enough samples were read from the file!");
        return ESP_FAIL;
    }
    
    int chunk_end_offset                       = num_samples_read - 1;
    audio_chunk_ptr->start_idx                 = chunk_start_pos_samples - start_file_pos_samples;
    audio_chunk_ptr->end_idx                   = audio_chunk_ptr->start_idx + chunk_end_offset;
    audio_chunk_ptr->data_idx                  = audio_chunk_ptr->start_idx;
    audio_chunk_ptr->chunk_start_pos_filebytes = chunk_start_pos_filebytes;
    audio_chunk_ptr->memory_buffer_pos         = 0;

    return ESP_OK;
}

//Advances audio chunk data pos by 1, and returns the next sample in the audio chunk.  Also returns the number of bytes remaining in the array used to store
//audio data
esp_err_t stp_sd__get_next_audio_sample(stp_sd__audio_chunk* audio_chunk_ptr, int32_t* next_sample_ptr, bool* time_to_reload_ptr){

    char* TAG = "get_next_audio_sample";
    int A_idx = audio_chunk_ptr->A_idx;
    int B_idx = audio_chunk_ptr->B_idx;
    int C_idx = audio_chunk_ptr->C_idx;
    int D_idx = audio_chunk_ptr->D_idx;
    int E_idx = audio_chunk_ptr->E_idx;
    int F_idx = audio_chunk_ptr->F_idx;
    int chunk_data_pos = audio_chunk_ptr->chunk_data_pos;

    double next_sample_double = 0;
    int32_t dither_const = 1;

    if (chunk_data_pos >= A_idx && chunk_data_pos<B_idx)
    {
        *next_sample_ptr = dither_const;
        audio_chunk_ptr->chunk_data_pos++;
    }
    else if (chunk_data_pos >= B_idx && chunk_data_pos<C_idx)
    {
        int i = chunk_data_pos - B_idx;
        next_sample_double  = (double)(audio_chunk_ptr->chunk_data_ptr[audio_chunk_ptr->memory_buffer_pos]) * (audio_chunk_ptr->cos_ramp_LUT_ptr[i]);
        *next_sample_ptr    = (int32_t)next_sample_double;
        audio_chunk_ptr->chunk_data_pos++;
        audio_chunk_ptr->memory_buffer_pos++;
        audio_chunk_ptr->data_idx++;
    }
    else if (chunk_data_pos >= C_idx && chunk_data_pos<D_idx)
    {
        *next_sample_ptr = audio_chunk_ptr->chunk_data_ptr[audio_chunk_ptr->memory_buffer_pos];
        audio_chunk_ptr->chunk_data_pos++;
        audio_chunk_ptr->memory_buffer_pos++;
        audio_chunk_ptr->data_idx++;
    }
    else if (chunk_data_pos >= D_idx && chunk_data_pos<E_idx)
    {
        int i_reverse = E_idx - chunk_data_pos;
        next_sample_double  = (double)(audio_chunk_ptr->chunk_data_ptr[audio_chunk_ptr->memory_buffer_pos]) * (audio_chunk_ptr->cos_ramp_LUT_ptr[i_reverse]);
        *next_sample_ptr    = (int32_t)next_sample_double;
        audio_chunk_ptr->chunk_data_pos++;
        audio_chunk_ptr->memory_buffer_pos++;
        audio_chunk_ptr->data_idx++;
    }
    else if (chunk_data_pos >= E_idx && chunk_data_pos<F_idx)
    {
        *next_sample_ptr = dither_const;
        audio_chunk_ptr->chunk_data_pos++;
    }
    else
    {
        ESP_LOGE(TAG, "Unexpected condition, error!");
        return ESP_FAIL;
    }

    if(audio_chunk_ptr->memory_buffer_pos >= audio_chunk_ptr->capacity/sizeof(int32_t))
    {
        *time_to_reload_ptr = true;
    }
    else{
        *time_to_reload_ptr = false;
    }

    if(audio_chunk_ptr->memory_buffer_pos > audio_chunk_ptr->capacity/sizeof(int32_t))
    {
        ESP_LOGE(TAG, "Error, memory buffer overflow!");
        return ESP_FAIL;
    }

    // if(audio_chunk_ptr->data_idx >= audio_chunk_ptr->end_idx)
    // {
    //     ESP_LOGE(TAG, "Reading beyond the wave file's endpoint!");
    // }

    return ESP_OK;
}

//This function reloads the array being used to supply audio data from the SD card wav file.
// This does NOT advance the chunk_data_pos, buffer is reloaded starting at the current sample, even if that sample has already been played.
void stp_sd__threadsafe_reload_chunk_memory_buffer_Task(void* pvParameters){

    char* TAG = "reload_memory_chunk";
    stp_sd__reload_memory_Task_struct* task_data_ptr = (stp_sd__reload_memory_Task_struct*)pvParameters;

    stp_sd__wavFile*                   wave_file_ptr = task_data_ptr->wave_file_ptr;                 //Pointer to wave file object used for audio
    QueueHandle_t                      reload_audio_buff_Queue = task_data_ptr->reload_audio_buff_Queue;
    stp_sd__reload_memory_data_struct  reload_struct = {};

    while(true)
    {

        memset(&reload_struct, 0, sizeof(reload_struct));
        if(xQueueReceive(reload_audio_buff_Queue, &reload_struct, portMAX_DELAY) == pdTRUE)
        {
            int32_t audio_chunk_pos_minus_dither = reload_struct.new_chunk_data_pos - reload_struct.B_idx;
            long current_filebytes = reload_struct.chunk_start_pos_filebytes + audio_chunk_pos_minus_dither*sizeof(int32_t);
            //Set file position in SD card wav file to match the current audio_chunk_data_pos sample
            int ret = fseek(wave_file_ptr->fp, current_filebytes, SEEK_SET);
            if (ret != ESP_OK)
            {     
                ESP_LOGE(TAG, "Error setting new file position!");
            }

            size_t num_samples_to_read;
            int num_non_dither_samples_remaining_in_chunk;

            num_non_dither_samples_remaining_in_chunk = reload_struct.chunk_len_wo_dither - reload_struct.new_chunk_data_pos - reload_struct.B_idx;

            if(num_non_dither_samples_remaining_in_chunk > 0)   //If this is less than 0, it means we already have enough samples loaded to finish the transmission
            {

                num_samples_to_read = reload_struct.chunk_load_ptr_cap/sizeof(int32_t);
                memset(reload_struct.chunk_load_ptr, 0, reload_struct.chunk_load_ptr_cap);
                int num_samples_read = fread(reload_struct.chunk_load_ptr, sizeof(int32_t), num_samples_to_read, wave_file_ptr->fp); //Fill center of audio chunk
                if(num_samples_read != num_samples_to_read)
                {
                    ESP_LOGE(TAG, "Not enough samples were read from the file!");
                    printf("%i samples read / %i samples to read\n", num_samples_read, num_samples_to_read);
                }
                xTaskNotify(reload_struct.task_to_notify, 0, eNoAction);      //If file read comes out okay, notify the calling task to continue execution
            }
            else
            {
                xTaskNotify(reload_struct.task_to_notify, 0, eNoAction);      //If file read comes out okay, notify the calling task to continue execution
            }
        }
    }
}


esp_err_t stp_sd__free_audio_chunk(stp_sd__audio_chunk* audio_chunk_ptr){
    char* TAG = "free_audio_chunk";
    if (audio_chunk_ptr->capacity != 0){
        free(audio_chunk_ptr->chunk_data_ptr);
        free(audio_chunk_ptr->chunk_load_ptr);
        memset(audio_chunk_ptr, 0, sizeof(audio_chunk_ptr));
        return ESP_OK;
    }
    else{
        ESP_LOGE(TAG, "Error freeing audio chunk!");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t stp_sd__close_audio_file(stp_sd__wavFile* wave_file_ptr){
    if (wave_file_ptr->fp != NULL){
        fclose(wave_file_ptr->fp);
    }
    memset(wave_file_ptr, 0, sizeof(wave_file_ptr)); //Reset all members of wave file to 0;
    return ESP_OK;
}



