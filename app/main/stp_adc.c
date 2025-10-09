#include "stp_adc.h"
#include "math.h"
#include "esp_err.h"


esp_err_t stp_adc__setup_adc_chans(stp_adc__adc_setup_struct adc_chans, stp_adc__adc_chan_struct* adc_chan_handle_struct_ptr){
    
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12,       //Standard 0-3.3V output
    };
    adc_chan_handle_struct_ptr->adc_config      = config;

    adc_oneshot_unit_init_cfg_t unit1_oneshot_config = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };

    adc_oneshot_unit_init_cfg_t unit2_oneshot_config = {
        .unit_id = ADC_UNIT_2,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };

    adc_oneshot_unit_handle_t unit1_handle;
    adc_oneshot_unit_handle_t unit2_handle;
    adc_oneshot_unit_handle_t swap_unit_handle;


    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit1_oneshot_config, &unit1_handle));
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit2_oneshot_config, &unit2_handle));
    //Volume Knob percent
    (adc_chans.vol_adc_unit == ADC_UNIT_1) ? (swap_unit_handle = unit1_handle) : (swap_unit_handle = unit2_handle);
    ESP_ERROR_CHECK(adc_oneshot_config_channel(swap_unit_handle, adc_chans.vol_adc_chan, &config));
    adc_chan_handle_struct_ptr->vol_adc_handle = swap_unit_handle;
    //Ch1 Knob position percent
    (adc_chans.ch1_adc_unit == ADC_UNIT_1) ? (swap_unit_handle = unit1_handle) : (swap_unit_handle = unit2_handle);
    ESP_ERROR_CHECK(adc_oneshot_config_channel(swap_unit_handle, adc_chans.ch1_adc_chan, &config));
    adc_chan_handle_struct_ptr->ch1_adc_handle = swap_unit_handle;
    //Ch2 Knob position percent
    (adc_chans.ch2_adc_unit == ADC_UNIT_1) ? (swap_unit_handle = unit1_handle) : (swap_unit_handle = unit2_handle);
    ESP_ERROR_CHECK(adc_oneshot_config_channel(swap_unit_handle, adc_chans.ch2_adc_chan, &config));
    adc_chan_handle_struct_ptr->ch2_adc_handle = swap_unit_handle;
    //9V Battery Voltage (voltage divider)
    (adc_chans.batt_adc_unit == ADC_UNIT_1) ? (swap_unit_handle = unit1_handle) : (swap_unit_handle = unit2_handle);
    ESP_ERROR_CHECK(adc_oneshot_config_channel(swap_unit_handle, adc_chans.batt_adc_chan, &config));
    adc_chan_handle_struct_ptr->batt_adc_handle = swap_unit_handle;
    //28V High Voltage Bus Voltage (voltage divider)
    (adc_chans.hv_adc_unit == ADC_UNIT_1) ? (swap_unit_handle = unit1_handle) : (swap_unit_handle = unit2_handle);
    ESP_ERROR_CHECK(adc_oneshot_config_channel(swap_unit_handle, adc_chans.hv_adc_chan, &config));
    adc_chan_handle_struct_ptr->hv_adc_handle = swap_unit_handle;


    adc_chan_handle_struct_ptr->vol_adc_chan  = adc_chans.vol_adc_chan;
    adc_chan_handle_struct_ptr->ch1_adc_chan  = adc_chans.ch1_adc_chan;
    adc_chan_handle_struct_ptr->ch2_adc_chan  = adc_chans.ch2_adc_chan;
    adc_chan_handle_struct_ptr->batt_adc_chan = adc_chans.batt_adc_chan;
    adc_chan_handle_struct_ptr->hv_adc_chan   = adc_chans.hv_adc_chan;

    printf("ADC channels set up!");
    return ESP_OK;
}

esp_err_t stp_adc__read_all_adc_chans(stp_adc__adc_chan_struct* adc_chan_handle_struct_ptr, stp_adc__adc_chan_results* results_ptr){

    // //ADC
    int adc_raw_result;
    int max_adc_reading = pow(2, 12);
    
    ESP_ERROR_CHECK(adc_oneshot_read(adc_chan_handle_struct_ptr->vol_adc_handle,
                                     adc_chan_handle_struct_ptr->vol_adc_chan, 
                                     &adc_raw_result));

    results_ptr->vol_percent = (1 - (double)adc_raw_result / (double)max_adc_reading) * 100.0;

    ESP_ERROR_CHECK(adc_oneshot_read(adc_chan_handle_struct_ptr->ch1_adc_handle,
                                     adc_chan_handle_struct_ptr->ch1_adc_chan, 
                                     &adc_raw_result));

    results_ptr->ch1_percent = (double)adc_raw_result / (double)max_adc_reading * 100.0;

    ESP_ERROR_CHECK(adc_oneshot_read(adc_chan_handle_struct_ptr->vol_adc_handle,
                                    adc_chan_handle_struct_ptr->ch2_adc_chan, 
                                    &adc_raw_result));

    results_ptr->ch2_percent = (double)adc_raw_result / (double)max_adc_reading * 100.0;

    ESP_ERROR_CHECK(adc_oneshot_read(adc_chan_handle_struct_ptr->batt_adc_handle,
                                adc_chan_handle_struct_ptr->batt_adc_chan, 
                                &adc_raw_result));

    results_ptr->batt_voltage = 4.3 * 3.3 * (double)adc_raw_result / (double)max_adc_reading;   //4.3 factor accounts for voltage divider on circuit board. 3.3V is top end of voltage input reading.

    ESP_ERROR_CHECK(adc_oneshot_read(adc_chan_handle_struct_ptr->hv_adc_handle,
                                adc_chan_handle_struct_ptr->hv_adc_chan, 
                                &adc_raw_result));

    results_ptr->hv_voltage = 11 * 3.3 * (double)adc_raw_result / (double)max_adc_reading;    //11 factor accounts for voltage divider on circuit board.  3.3V is top end of voltage input reading.
    
    return ESP_OK;
}
