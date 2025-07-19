//This program is written by David Earley (2025) 
//This program borrows heavily from this article throughout:
//https://truelogic.org/wordpress/2015/09/04/parsing-a-wav-file-in-c/


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

FILE *fp;

typedef struct{
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
} wavFile;


int main(int argc, char** argv){
    fp = fopen("SINEA.wav", "rb");
    if (fp==NULL){
        fprintf(stderr, "File not Opened!");
        return 1;
    }
    else{
        printf("File Opened Successfully!\n");
    }

    wavFile wave_file = {0};

    int count = 0;
    //read RIFF data
	count = fread(wave_file.RIFF, sizeof(wave_file.RIFF), 1, fp);
	printf("(1-4): %.*s \n", (int)sizeof(wave_file.RIFF), wave_file.RIFF); 
    if (count != 1){
        fprintf(stderr, "RIFF not read properly!\n");
    }
    if(!memcmp("RIFF", wave_file.RIFF, sizeof(wave_file.RIFF))){
        fprintf(stdout, "RIFF read successfully!\n");
    }
    else{
        fprintf(stderr, "RIFF does not match!\n");
    }
    //read filesize
    count = fread(&wave_file.filesize, sizeof(wave_file.filesize), 1, fp);
	printf("filesize: %i bytes\n", wave_file.filesize); 
    if (count != 1){
        fprintf(stderr, "filesize not read properly!\n");
    }

    //read WAVE marker
    count = fread(&wave_file.WAVE, sizeof(wave_file.WAVE), 1, fp);
    printf("WAVE: %.*s\n", (int)sizeof(wave_file.WAVE), wave_file.WAVE);

    //read fmt marker
    count = fread(wave_file.fmt, sizeof(wave_file.fmt), 1, fp);
    printf("fmt: %.*s\n", (int)sizeof(wave_file.fmt), wave_file.fmt);

    //read length of fmt data
    count = fread(&wave_file.fmt_length, sizeof(wave_file.fmt_length), 1, fp);
    printf("fmt length: %i\n", wave_file.fmt_length);
    
    //read type of fmt data
    count = fread(&wave_file.fmt_type, sizeof(wave_file.fmt_type), 1, fp);
    printf("fmt type: %i\n", wave_file.fmt_type);

    //read number of channels
    count = fread(&wave_file.num_channels, sizeof(wave_file.num_channels), 1, fp);
    printf("Number of Channels: %i\n", wave_file.num_channels);

    //read sample rate
    count = fread(&wave_file.sample_rate, sizeof(wave_file.sample_rate), 1, fp);
    printf("Sample Rate: %i Hz\n", wave_file.sample_rate);

    //read (Sample Rate * BitsPerSample * Channels) / 8
    count = fread(&wave_file.SampleRateBitsPerSampleChannels_8, sizeof(wave_file.SampleRateBitsPerSampleChannels_8), 1, fp);
    printf("(Sample Rate * BitsPerSample * Channels) / 8: %i\n", wave_file.SampleRateBitsPerSampleChannels_8);

    //read (BitsPerSample * Channels) / 8.1
    count = fread(&wave_file.BitsPerSampleChannels_8_1, sizeof(wave_file.BitsPerSampleChannels_8_1), 1, fp);
    printf("(BitsPerSample * Channels) / 8.1: %i\n", wave_file.BitsPerSampleChannels_8_1);

    //read bitness
    count = fread(&wave_file.bitness, sizeof(wave_file.bitness), 1, fp);
    printf("Bits per Sample: %i\n", wave_file.bitness);

    //read data chunk header
    count = fread(wave_file.data_header, sizeof(wave_file.data_header), 1, fp);
    printf("data: %.*s\n", (int)sizeof(wave_file.data_header), wave_file.data_header);

    //read data size
    count = fread(&wave_file.data_size, sizeof(wave_file.data_size), 1, fp);
    printf("Data Size: %i bytes\n", wave_file.data_size);

    //Get location in file where audio data starts, in number of bytes from beginning of file (this works because we open the file in binary mode)
    wave_file.audiofile_data_start_pos = ftell(fp);
    printf("Audio Data Start position: %li bytes\n", wave_file.audiofile_data_start_pos);

    //Calculate location in file of final audio data point, in number of bytes from the beginning of the file
    wave_file.audiofile_data_end_pos = wave_file.audiofile_data_start_pos + wave_file.data_size/sizeof(int32_t) - 1;
    printf("Audio Data End position: %li bytes\n", wave_file.audiofile_data_end_pos);


    //Start reading audio data
    wave_file.audio_len = wave_file.data_size / sizeof(int32_t);
    wave_file.audio_ptr = malloc(wave_file.data_size);
    if (wave_file.audio_ptr==NULL){
        fprintf(stderr, "No more memory!\n");
        return 1;
    }

    count = fread(wave_file.audio_ptr, wave_file.data_size, wave_file.data_size/sizeof(int32_t), fp);

    for (int i=0; i<20; i++){
        printf("Audio Data [%i]: %i\n", i, *(wave_file.audio_ptr+i));
    }

    printf("Last audio data reading: %i\n", *(wave_file.audio_ptr + wave_file.audiofile_data_end_pos-wave_file.audiofile_data_start_pos));


    fclose(fp);

    //use fseek to set the file position indicator to the desired positions for file reading
}