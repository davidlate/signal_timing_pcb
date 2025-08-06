#ifndef STP_ADC_H
#define STP_ADC_H

#include "hal/adc_types.h"
#include "esp_adc/adc_oneshot.h"

typedef struct  {
    int vol_adc_unit;
    int vol_adc_chan;
    int ch1_adc_unit;
    int ch1_adc_chan;
    int ch2_adc_unit;
    int ch2_adc_chan;
    int batt_adc_chan;
    int batt_adc_unit;
    int hv_adc_unit;
    int hv_adc_chan;
} stp_adc__adc_setup_struct;

typedef struct {
    adc_oneshot_chan_cfg_t adc_config;
    adc_oneshot_unit_handle_t vol_adc_handle;
    int vol_adc_chan;
    adc_oneshot_unit_handle_t ch1_adc_handle;
    int ch1_adc_chan;
    adc_oneshot_unit_handle_t ch2_adc_handle;
    int ch2_adc_chan;
    adc_oneshot_unit_handle_t batt_adc_handle;
    int batt_adc_chan;
    adc_oneshot_unit_handle_t hv_adc_handle;
    int hv_adc_chan;
} stp_adc__adc_chan_struct;

typedef struct {
    double vol_percent;
    double ch1_percent;
    double ch2_percent;
    double batt_voltage;
    double hv_voltage;
} stp_adc__adc_chan_results;

esp_err_t stp_adc__setup_adc_chans(stp_adc__adc_setup_struct, stp_adc__adc_chan_struct*);
esp_err_t stp_adc__read_all_adc_chans(stp_adc__adc_chan_struct*, stp_adc__adc_chan_results*);
esp_err_t stp_adc__read_vol_adc_chan(stp_adc__adc_chan_struct, double*);
















#endif