#include "stp_adc.h"
#include "math.h"
#include "esp_err.h"


esp_err_t stp_adc__setup_adc_chans(stp_adc__adc_setup_struct adc_chans, stp_adc__adc_chan_struct* adc_chan_handle_struct_ptr){
    
    char* TAG = "adc_setup";

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12,       //Standard 0-3.3V output
    };  
    
    adc_oneshot_unit_handle_t vol_handle;
    adc_oneshot_unit_init_cfg_t vol_config = {
        .unit_id = adc_chans.vol_adc_unit,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    int adc_raw_result;
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&vol_config, &vol_handle));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(vol_handle, adc_chans.vol_adc_chan, &config));



    adc_oneshot_unit_handle_t ch1_handle;
    adc_oneshot_unit_init_cfg_t ch1_config = {
        .unit_id = adc_chans.ch1_adc_unit,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&ch1_config, &ch1_handle));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(ch1_handle, adc_chans.ch1_adc_chan, &config));


    // adc_oneshot_unit_handle_t ch2_handle;
    // adc_oneshot_unit_init_cfg_t ch2_config = {
    //     .unit_id = adc_chans.ch2_adc_unit,
    //     .ulp_mode = ADC_ULP_MODE_DISABLE,
    // };
    // // ESP_ERROR_CHECK(adc_oneshot_new_unit(&ch2_config, &ch2_handle));
    // ESP_ERROR_CHECK(adc_oneshot_config_channel(ch2_handle, adc_chans.ch2_adc_chan, &config));


    // adc_oneshot_unit_handle_t batt_handle;
    // adc_oneshot_unit_init_cfg_t batt_config = {
    //     .unit_id = adc_chans.batt_adc_unit,
    //     .ulp_mode = ADC_ULP_MODE_DISABLE,
    // };
    // // ESP_ERROR_CHECK(adc_oneshot_new_unit(&batt_config, &batt_handle));
    // ESP_ERROR_CHECK(adc_oneshot_config_channel(batt_handle, adc_chans.batt_adc_chan, &config));
    

    // adc_oneshot_unit_handle_t hv_handle;
    // adc_oneshot_unit_init_cfg_t hv_config = {
    //     .unit_id = adc_chans.hv_adc_unit,
    //     .ulp_mode = ADC_ULP_MODE_DISABLE,
    // };
    // // ESP_ERROR_CHECK(adc_oneshot_new_unit(&hv_config, &hv_handle));
    // ESP_ERROR_CHECK(adc_oneshot_config_channel(hv_handle, adc_chans.hv_adc_chan, &config));

    adc_chan_handle_struct_ptr->adc_config      = config;
    adc_chan_handle_struct_ptr->vol_adc_handle  = vol_handle;
    adc_chan_handle_struct_ptr->ch1_adc_handle  = ch1_handle;
    // adc_chan_handle_struct_ptr->ch2_adc_handle  = ch2_handle;
    // adc_chan_handle_struct_ptr->batt_adc_handle = batt_handle;
    // adc_chan_handle_struct_ptr->hv_adc_handle   = hv_handle;

    adc_chan_handle_struct_ptr->vol_adc_chan  = adc_chans.vol_adc_chan;
    adc_chan_handle_struct_ptr->ch1_adc_chan  = adc_chans.ch1_adc_chan;
    adc_chan_handle_struct_ptr->ch2_adc_chan  = adc_chans.ch2_adc_chan;
    adc_chan_handle_struct_ptr->batt_adc_chan = adc_chans.batt_adc_chan;
    adc_chan_handle_struct_ptr->hv_adc_chan   = adc_chans.hv_adc_chan;


    printf("ADC channels set up!");
    return ESP_OK;
}

esp_err_t stp_adc__read_all_adc_chans(stp_adc__adc_chan_struct* adc_chan_handle_struct_ptr, stp_adc__adc_chan_results* results_ptr){

    char* TAG = "adc_read_all";

    // //ADC
    int adc_raw_result;
    int max_adc_reading = pow(2, 12);
    
    ESP_ERROR_CHECK(adc_oneshot_read(adc_chan_handle_struct_ptr->vol_adc_handle,
                                     adc_chan_handle_struct_ptr->vol_adc_chan, 
                                     &adc_raw_result));


    results_ptr->vol_percent = (double)adc_raw_result / (double)max_adc_reading * 100.0;

    ESP_ERROR_CHECK(adc_oneshot_read(adc_chan_handle_struct_ptr->ch1_adc_handle,
                                     adc_chan_handle_struct_ptr->ch1_adc_chan, 
                                     &adc_raw_result));

    results_ptr->ch1_percent = (double)adc_raw_result / (double)max_adc_reading * 100.0;

    // ESP_ERROR_CHECK(adc_oneshot_read(adc_chan_handle_struct_ptr->ch2_adc_handle,
    //                                 adc_chan_handle_struct_ptr->ch2_adc_chan, 
    //                                 &adc_raw_result));

    // results_ptr->ch2_percent = adc_raw_result / max_adc_reading * 100.0;

    // ESP_ERROR_CHECK(adc_oneshot_read(adc_chan_handle_struct_ptr->batt_adc_handle,
    //                             adc_chan_handle_struct_ptr->batt_adc_chan, 
    //                             &adc_raw_result));

    // results_ptr->batt_voltage = 4.3 * 3.3 * adc_raw_result / max_adc_reading * 100.0;   //4.3 factor accounts for voltage divider on circuit board. 3.3V is top end of voltage input reading.

    // ESP_ERROR_CHECK(adc_oneshot_read(adc_chan_handle_struct_ptr->hv_adc_handle,
    //                             adc_chan_handle_struct_ptr->hv_adc_chan, 
    //                             &adc_raw_result));

    // results_ptr->hv_voltage = 11 * 3.3 * adc_raw_result / max_adc_reading * 100.0;    //11 factor accounts for voltage divider on circuit board.  3.3V is top end of voltage input reading.

    // printf("Vol: %.3f%% | Ch1: %.3f%% | Ch2: %.3f%% | Batt: %.1fV | HV: %.1f V\n",
    printf("Vol: %.1f%% | Ch1: %0.1f%% | Ch2:  | Batt: V | HV: V\n",
                results_ptr->vol_percent,
                results_ptr->ch1_percent);
                // results_ptr->ch2_percent,
                // results_ptr->batt_voltage,
                // results_ptr->hv_voltage);
    
    return ESP_OK;
}




