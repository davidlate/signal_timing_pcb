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
#include "bootloader_random.h"
#include "math.h"

#define MOUNT_POINT     "/sdcard"
#define AUDIO_FILE_PATH "/AUDIO.wav"

// Pin assignments can be set in menuconfig, see "SD SPI Example Configuration" menu.
// You can also change the pin assignments here by changing the following 4 lines.
#define PIN_NUM_MISO  GPIO_NUM_13   //DO
#define PIN_NUM_MOSI  GPIO_NUM_11   //DI
#define PIN_NUM_CLK   GPIO_NUM_12   //CLK
#define PIN_NUM_CS    GPIO_NUM_10   //CS

typedef struct {
    bool open;
    int miso_do_pin;
    int mosi_di_pin;
    int clk_pin;
    int cs_pin;
    char* mount_point;
    sdmmc_card_t* card_ptr;
    sdmmc_host_t* host_ptr;
} stp_sd__spi_config;


typedef struct{
    int       capacity;
    FILE*     fp;
    char      RIFF[4];
    int32_t   filesize;
    char      WAVE[4];
    char      fmt[4];
    int32_t   fmt_length;
    int16_t   fmt_type;
    int16_t   num_channels;
    int32_t   sample_rate;
    int32_t   SampleRateBitsPerSampleChannels_8;
    int16_t   BitsPerSampleChannels_8_1;
    int16_t   bitness;
    char      data_header[4];
    int32_t   data_size;
    int32_t   audio_len;
    long int  audiofile_data_start_pos;
    long int  audiofile_data_end_pos;
} stp_sd__wavFile;


typedef struct{
    int      chunk_len;                   //REQUIRED INPUT: length of chunk in number of samples
    int      rise_fall_num_samples;       //REQUIRED INPUT: Number of samples to apply rise/fall scaling to (nominally 96 [1ms @ 96000Hz]) at the beginning and end of the chunk
    int      padding_num_samples;         //REQUIRED INPUT: Number of samples to offset from the beginning and end of the audio data
    int      capacity;                    //memory capacity of chunk_data_ptr
    int      chunk_size;                  //size of chunk in bytes
    int      start_idx;                   //starting index of chunk relative to audio data
    int      end_idx;                     //ending index of chunk relative to audio data
    int32_t* chunk_data_ptr;              //array of int32_t audio samples
    int      chunk_data_idx;              //index in chunk where we are currently
} stp_sd__audio_chunk;

static esp_err_t stp_sd__mount_sd_card(stp_sd__spi_config* spi_config_ptr){

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
    spi_config_ptr->host_ptr = &host;
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
        ESP_LOGE(TAG, "Failed to initialize bus.");
        return ESP_FAIL;
    }
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = spi_config_ptr->cs_pin;
    slot_config.host_id = (spi_config_ptr->host_ptr)->slot;

    ESP_LOGI(TAG, "Mounting filesystem");
    ret = esp_vfs_fat_sdspi_mount(spi_config_ptr->mount_point, spi_config_ptr->host_ptr, &slot_config, &mount_config, &(spi_config_ptr->card_ptr));

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

static esp_err_t stp_sd__unmount_sd_card(stp_sd__spi_config* spi_config_ptr){
    // All done, unmount partition and disable SPI peripheral
    char* TAG = "unmount SD";

    if(esp_vfs_fat_sdcard_unmount(spi_config_ptr->mount_point, spi_config_ptr->card_ptr)!=ESP_OK){
        ESP_LOGE(TAG, "Error unmounting SPI Bus!");
        return ESP_FAIL;
    }

    //deinitialize the bus after all devices are removed
    if(spi_bus_free((spi_config_ptr->host_ptr)->slot) != ESP_OK){
        ESP_LOGE(TAG, "Error freeing SPI bus");
        return ESP_FAIL;
    }
    else{
        spi_config_ptr->open = false;
        memset(spi_config_ptr, 0, sizeof(*spi_config_ptr)); //Reset all members of wave file to 0;
    }

    return ESP_OK;
}

static esp_err_t sd_stp__open_audio_file(stp_sd__wavFile* wave_file_ptr)
{
    char* TAG = "open audio file";
    //Open file for reading
    wave_file_ptr->fp = fopen(MOUNT_POINT AUDIO_FILE_PATH, "rb");
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

//This function selects a "chunk" of specified length from the wave file, beginning at a random start point, with padding after the beginning and before the ending
static esp_err_t sd_stp__get_audio_chunk(stp_sd__audio_chunk* audio_chunk, stp_sd__wavFile* wave_file_ptr){
    char* TAG = "audio_chunk_cont";
    audio_chunk->chunk_size = audio_chunk->chunk_len * sizeof(*(audio_chunk->chunk_data_ptr));
    //allocate memory for the audio chunk, if needed
    if(audio_chunk->capacity <= audio_chunk->chunk_size){
        if(audio_chunk->chunk_data_ptr != NULL){
            audio_chunk->capacity = 2*audio_chunk->chunk_size;
            audio_chunk->chunk_data_ptr = realloc(audio_chunk->chunk_data_ptr, 2*audio_chunk->chunk_size);
        }
        else{
            audio_chunk->chunk_data_ptr = malloc(2*audio_chunk->chunk_size);
            if (audio_chunk->chunk_data_ptr==NULL){
                ESP_LOGE(TAG, "No memory available for audio chunk!");
                return ESP_FAIL;
            }
        }
    }

    if(wave_file_ptr->fp == NULL){
        ESP_LOGE(TAG, "Wave file not open!");
        return ESP_FAIL;
    }
    /*Get locations of the start- and end- idxs of the allowable space where we can take our audio file from, in no. of bytes from beginning of file, that we can grab our chunk from.*/
    double start_file_pos_samples = (double)((wave_file_ptr->audiofile_data_start_pos)/sizeof(int32_t)) + (double)audio_chunk->padding_num_samples;
    double end_file_pos_samples   = (double)((wave_file_ptr->audiofile_data_end_pos)/sizeof(int32_t))   - (double)audio_chunk->padding_num_samples - (double)audio_chunk->chunk_len;
    double delta_file_pos_samples = end_file_pos_samples - start_file_pos_samples;

    if ((double)start_file_pos_samples != (int)start_file_pos_samples || (double)end_file_pos_samples != (int)end_file_pos_samples){
        ESP_LOGE(TAG, "Implicit conversion error with audio chunk!");
        return ESP_FAIL;
    }
    if (delta_file_pos_samples < audio_chunk->chunk_size){
        ESP_LOGE(TAG, "Audio chunk is longer than available audio data!  Increase file length, or decrease chunk length or chunk padding.");
        return ESP_FAIL;
    }
    
    bootloader_random_enable();

    double random = (double)esp_random() / (double)(pow(2,32)-1);  //convert from uint32_t to double between 0 and 1
    double chunk_start_pos_samples = round(delta_file_pos_samples * random) + start_file_pos_samples;
    int chunk_start_pos_samples_int = chunk_start_pos_samples;

    if ((double)chunk_start_pos_samples != (double)chunk_start_pos_samples){
        ESP_LOGE(TAG, "Implicit conversion error 2 with audio chunk!");
    }
    if (chunk_start_pos_samples_int  % 2 == 0){
        ESP_LOGI(TAG, "ODD");
        chunk_start_pos_samples_int += 1;    //We must start with an even index so that our right and left channels always line up properly.  It's okay to eat into 1 sample of our padding.
    }

    int chunk_start_pos_filebytes = (chunk_start_pos_samples_int*sizeof(int32_t));   //The location in the file where our audio chunk will begin
                                                                            
    if(fseek(wave_file_ptr->fp, chunk_start_pos_filebytes, SEEK_SET) != ESP_OK){     //Set file position to the one determined by the random selection
        ESP_LOGE(TAG, "Error setting new file position!");
        bootloader_random_disable();
        return ESP_FAIL;
    }
    int num_samples_read = fread(audio_chunk->chunk_data_ptr, sizeof(int32_t), audio_chunk->chunk_len, wave_file_ptr->fp);

    if(num_samples_read != audio_chunk->chunk_len){
        ESP_LOGE(TAG, "Not enough samples were read from the file!");
        bootloader_random_disable();
        return ESP_FAIL;
    }
    
    int chunk_end_offset = num_samples_read -1;
    audio_chunk->start_idx = chunk_start_pos_samples_int - (int)start_file_pos_samples + audio_chunk->padding_num_samples;
    audio_chunk->end_idx   = audio_chunk->start_idx + chunk_end_offset;

    printf("Start idx: %i | Start Data: %li\nEnd idx: %i | End Data: %li\nChunk Length: %i\nSample Count: %i\n",
            audio_chunk->start_idx,
            audio_chunk->chunk_data_ptr[0],
            audio_chunk->end_idx,
            audio_chunk->chunk_data_ptr[chunk_end_offset],
            audio_chunk->chunk_len,
            num_samples_read);
    printf("Random Number: %.3f\n", random);

    bootloader_random_disable();
    ESP_LOGI(TAG, "Successfully loaded new audio chunk");

    return ESP_OK;
}

static esp_err_t sd_stp__destruct_audio_chunk(stp_sd__audio_chunk* audio_chunk){

    if(audio_chunk->chunk_data_ptr != NULL){
        free(audio_chunk->chunk_data_ptr);
    }
    audio_chunk->capacity = 0;
    return ESP_OK;
}

static esp_err_t sd_stp__close_audio_file(stp_sd__wavFile* wave_file_ptr){
    if (wave_file_ptr->fp != NULL){
        fclose(wave_file_ptr->fp);
    }
    memset(wave_file_ptr, 0, sizeof(wave_file_ptr)); //Reset all members of wave file to 0;
    return ESP_OK;
}


void app_main(void)
{
    char* TAG = "main";


    stp_sd__spi_config spi_config ={
        .open        = false,
        .mosi_di_pin = PIN_NUM_MOSI,
        .miso_do_pin = PIN_NUM_MISO,
        .clk_pin     = PIN_NUM_CLK,
        .cs_pin      = PIN_NUM_CS,
        .mount_point = MOUNT_POINT,
        .card_ptr    = NULL,
        .host_ptr    = NULL,
    };

    if(stp_sd__mount_sd_card(&spi_config) != ESP_OK){
        ESP_LOGE(TAG, "Error Mounting SD Card!");
        return;
    }

    stp_sd__wavFile wave_file = {0};  

    if(sd_stp__open_audio_file(&wave_file) != ESP_OK){
        ESP_LOGE(TAG, "Error Opening Audio File!");
        return;
    }

    stp_sd__audio_chunk audio_chunk = {     //All zeroed or NULL parameters are set by the sd_stp__get_audio_chunk function
        .chunk_len               = 100,
        .rise_fall_num_samples   = 96,
        .padding_num_samples     = 10,
        .capacity                = 0,
        .start_idx               = 0,
        .end_idx                 = 0,     
        .chunk_data_ptr          = NULL,     
        .chunk_data_idx          = 0,
    };

    for (int j = 0; j<20; j++){

        if(sd_stp__get_audio_chunk(&audio_chunk, &wave_file) != ESP_OK){
            ESP_LOGE(TAG, "Error getting audio chunk!");
            return;
        };
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    if(sd_stp__destruct_audio_chunk(&audio_chunk) != ESP_OK){
        ESP_LOGE(TAG, "Error destructing audio chunk!");
        return;
    }

    if(sd_stp__close_audio_file(&wave_file) != ESP_OK){
        ESP_LOGE(TAG, "Error closing audio file!");
        return;
    }

    if(stp_sd__unmount_sd_card(&spi_config) != ESP_OK){
        ESP_LOGE(TAG, "Error unmounting SD card");
        return;
    }

    return;
}
