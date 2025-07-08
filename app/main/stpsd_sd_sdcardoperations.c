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
    bool      open;
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
    int32_t*  audio_ptr;
    int32_t   audio_len;
    long int  audiofile_data_start_pos;
    long int  audiofile_data_end_pos;
} stp_sd__wavFile;

typedef struct{
    bool     loaded;
    int      len_samples;
    int      start_idx;
    int      end_idx;
    int      rise_fall_samples;
    int32_t* data;              //array of int32_t samples
    int      data_pos;
    int      data_size;
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
    esp_err_t ret = ESP_OK;
    ret = (esp_vfs_fat_sdcard_unmount(spi_config_ptr->mount_point, spi_config_ptr->card_ptr)==ESP_OK);
    
    if (ret != ESP_OK){
        ESP_LOGI(TAG, "Card unmounted");
    }
    else{
        ESP_LOGE(TAG, "Error unmounting SD card");
        ret = ESP_FAIL;
    }
    //deinitialize the bus after all devices are removed
    if(spi_bus_free((spi_config_ptr->host_ptr)->slot) == ESP_OK){
        ESP_LOGI(TAG, "SPI Bus freed");
    }
    else{
        ESP_LOGE(TAG, "Error freeing SPI bus");
        ret = ESP_FAIL;
    }
    if (ret ==ESP_OK){
        spi_config_ptr->open = false;
        memset(spi_config_ptr, 0, sizeof(*spi_config_ptr)); //Reset all members of wave file to 0;
    }

    return ret;
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
    //read RIFF data
	count = fread(wave_file_ptr->RIFF, sizeof(wave_file_ptr->RIFF), 1, wave_file_ptr->fp);
	ESP_LOGI(TAG, "RIFF: %.*s", (int)sizeof(wave_file_ptr->RIFF), wave_file_ptr->RIFF); 
    if (count != 1){
        ESP_LOGE(TAG, "RIFF not read properly!");
    }

    if(memcmp("RIFF", wave_file_ptr->RIFF, sizeof(wave_file_ptr->RIFF))){
        ESP_LOGE(TAG, "RIFF does not match!");
    }

    //read filesize
    count = fread(&(wave_file_ptr->filesize), sizeof(wave_file_ptr->filesize), 1, wave_file_ptr->fp);
	ESP_LOGI(TAG, "filesize: %li bytes", wave_file_ptr->filesize); 

    //read WAVE marker
    count = fread(wave_file_ptr->WAVE, sizeof(wave_file_ptr->WAVE), 1, wave_file_ptr->fp);
    ESP_LOGI(TAG, "WAVE: %.*s", (int)sizeof(wave_file_ptr->WAVE), wave_file_ptr->WAVE);
    if(memcmp("WAVE", wave_file_ptr->WAVE, sizeof(wave_file_ptr->WAVE))){
        ESP_LOGE(TAG, "WAVE not read successfully!");
        return ESP_FAIL;
    }

    //read fmt marker
    count = fread(wave_file_ptr->fmt, sizeof(wave_file_ptr->fmt), 1, wave_file_ptr->fp);
    ESP_LOGI(TAG, "fmt: %.*s", (int)sizeof(wave_file_ptr->fmt), wave_file_ptr->fmt);

    if(memcmp("fmt ", wave_file_ptr->fmt, sizeof(wave_file_ptr->fmt))){
        ESP_LOGE(TAG, "fmt not read successfully!");
        return ESP_FAIL;
    }

    //read length of fmt data
    count = fread(&wave_file_ptr->fmt_length, sizeof(wave_file_ptr->fmt_length), 1, wave_file_ptr->fp);
    ESP_LOGI(TAG, "fmt length: %li", wave_file_ptr->fmt_length);
    
    //read type of fmt data
    count = fread(&wave_file_ptr->fmt_type, sizeof(wave_file_ptr->fmt_type), 1, wave_file_ptr->fp);
    ESP_LOGI(TAG, "fmt type: %i", wave_file_ptr->fmt_type);

    if(wave_file_ptr->fmt_type != 1){
        ESP_LOGE(TAG, "File must be PCM format!");
        return ESP_FAIL;
    }
    //read number of channels
    count = fread(&(wave_file_ptr->num_channels), sizeof(wave_file_ptr->num_channels), 1, wave_file_ptr->fp);
    ESP_LOGI(TAG, "Number of Channels: %i", wave_file_ptr->num_channels);
    if(wave_file_ptr->num_channels != 2){
        ESP_LOGE(TAG, "Audio must be stereo!");
        return ESP_FAIL;
    }
    //read sample rate
    count = fread(&wave_file_ptr->sample_rate, sizeof(wave_file_ptr->sample_rate), 1, wave_file_ptr->fp);
    ESP_LOGI(TAG, "Sample Rate: %li Hz", wave_file_ptr->sample_rate);
    if(wave_file_ptr->sample_rate != 96000){
        ESP_LOGE(TAG, "Sample Rate must be 96000 Hz!");
        return ESP_FAIL;
    }

    //read (Sample Rate * BitsPerSample * Channels) / 8
    count = fread(&(wave_file_ptr->SampleRateBitsPerSampleChannels_8), sizeof(wave_file_ptr->SampleRateBitsPerSampleChannels_8), 1, wave_file_ptr->fp);
    ESP_LOGI(TAG, "(Sample Rate * BitsPerSample * Channels) / 8: %li", wave_file_ptr->SampleRateBitsPerSampleChannels_8);

    //read (BitsPerSample * Channels) / 8.1
    count = fread(&(wave_file_ptr->BitsPerSampleChannels_8_1), sizeof(wave_file_ptr->BitsPerSampleChannels_8_1), 1, wave_file_ptr->fp);
    ESP_LOGI(TAG, "(BitsPerSample * Channels) / 8.1: %i", wave_file_ptr->BitsPerSampleChannels_8_1);

    //read bitness
    count = fread(&(wave_file_ptr->bitness), sizeof(wave_file_ptr->bitness), 1, wave_file_ptr->fp);
    ESP_LOGI(TAG, "Bits per Sample: %i", wave_file_ptr->bitness);
    if(wave_file_ptr->bitness != 32){
        ESP_LOGE(TAG, "Audio must be 32 bit audio!");
        return ESP_FAIL;
    }

    //read data chunk header
    count = fread(wave_file_ptr->data_header, sizeof(wave_file_ptr->data_header), 1, wave_file_ptr->fp);
    ESP_LOGI(TAG, "data: %.*s", (int)sizeof(wave_file_ptr->data_header), wave_file_ptr->data_header);

    //read data size
    count = fread(&(wave_file_ptr->data_size), sizeof(wave_file_ptr->data_size), 1, wave_file_ptr->fp);
    ESP_LOGI(TAG, "Data Size: %li bytes", wave_file_ptr->data_size);

    //Get location in file where audio data starts, in number of bytes from beginning of file (this works because we open the file in binary mode)
    wave_file_ptr->audiofile_data_start_pos = ftell(wave_file_ptr->fp);
    ESP_LOGI(TAG, "Audio Data Start position: %li bytes", wave_file_ptr->audiofile_data_start_pos);

    //Calculate location in file of final audio data point, in number of bytes from the beginning of the file
    wave_file_ptr->audiofile_data_end_pos = wave_file_ptr->audiofile_data_start_pos + (wave_file_ptr->data_size) - 1;
    ESP_LOGI(TAG, "Audio Data End position: %li bytes", wave_file_ptr->audiofile_data_end_pos);


    //Start reading audio data
    wave_file_ptr->audio_len = wave_file_ptr->data_size / sizeof(int32_t);
    wave_file_ptr->audio_ptr = malloc(wave_file_ptr->data_size);
    if (wave_file_ptr->audio_ptr==NULL){
        ESP_LOGE(TAG, "No more memory!");
        return ESP_FAIL;
    }

    count = fread(wave_file_ptr->audio_ptr, wave_file_ptr->data_size, wave_file_ptr->data_size/sizeof(int32_t), wave_file_ptr->fp);

    for (int i=0; i<20; i++){
        printf("Audio Data [%i]: %li\n", i, *(wave_file_ptr->audio_ptr+i));
    }
    printf("Last audio data reading: %li\n", *(wave_file_ptr->audio_ptr + (wave_file_ptr->audiofile_data_end_pos-wave_file_ptr->audiofile_data_start_pos)/sizeof(int32_t)));

    wave_file_ptr->open = true;
    return ESP_OK;
}

//TODO
static esp_err_t sd_stp__construct_audio_chunk(stp_sd__audio_chunk* audio_chunk, stp_sd__wavFile* wave_file_ptr){

    if(audio_chunk->data != NULL){
        free(audio_chunk->data);
    }
    audio_chunk->data = malloc(audio_chunk->len_samples * sizeof(int32_t));
    bootloader_random_enable();
    double random = (double)esp_random() / (double)(pow(2,32)-1);
    printf("Random Number: %.3f\n", random);
    bootloader_random_disable();
    return ESP_OK;
}

static esp_err_t sd_stp__destruct_audio_chunk(stp_sd__audio_chunk* audio_chunk){

    if(audio_chunk->data != NULL){
        free(audio_chunk->data);
    }
    return ESP_OK;
}


static esp_err_t sd_stp__close_audio_file(stp_sd__wavFile* wave_file_ptr){
    if (wave_file_ptr->fp != NULL){
        fclose(wave_file_ptr->fp);
    }
    memset(wave_file_ptr, 0, sizeof(*wave_file_ptr)); //Reset all members of wave file to 0;
    wave_file_ptr->open = false;
    return ESP_OK;
}


void app_main(void)
{
    char* TAG = "main";

    stp_sd__spi_config spi_config ={
        .mosi_di_pin = PIN_NUM_MOSI,
        .miso_do_pin = PIN_NUM_MISO,
        .clk_pin     = PIN_NUM_CLK,
        .cs_pin      = PIN_NUM_CS,
        .mount_point = MOUNT_POINT,
    };

    stp_sd__mount_sd_card(&spi_config);

    stp_sd__wavFile wave_file = {0};  

    int ret = sd_stp__open_audio_file(&wave_file);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error opening audio file");
        return;
    }

    stp_sd__audio_chunk audio_chunk;
    sd_stp__load_random_portion_of_audio_file(&audio_chunk, &wave_file);

    ret = sd_stp__close_audio_file(&wave_file);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error closing audio file");
        return;
    }

    ret = stp_sd__unmount_sd_card(&spi_config);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error unmounting SD card");
        return;
    }


}
