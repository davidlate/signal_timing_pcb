#include <stdint.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "soc/esp32/rtc.h"
#include "esp_system.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "sdkconfig.h"
#include "driver/gptimer.h"

#define P1 GPIO_NUM_2

typedef struct{
    int j;
    int P1;
    QueueHandle_t queue;
    BaseType_t queue_result;
} test_struct;


static bool example_timer_on_alarm_cb(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_data)
{
    test_struct *data = (test_struct *)user_data;

    int pin_status = data->j;
    if (pin_status == 1){
        data->j = 0;
    }
    else{
        data->j = 1;
    }

    gpio_set_level(P1, data->j);

    //get time in ticks when electrical pulse is needed and write to pointer.
    //Read ticks in main loop and set RMT peripheral to start in time.
    return false;
}


void app_main(void)
{
gpio_set_direction(P1, GPIO_MODE_OUTPUT);
printf("Hello world\n");



test_struct tester = {
    .j = 0,
    .P1 = 2
};

gptimer_handle_t gptimer = NULL;
gptimer_config_t timer_config = {
    .clk_src = GPTIMER_CLK_SRC_DEFAULT,
    .direction = GPTIMER_COUNT_UP,
    .resolution_hz = 1e6, // 1MHz, 1 tick = 1us
};
ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));


gptimer_alarm_config_t alarm_config = {
    .reload_count = 0, // counter will reload with 0 on alarm event
    .alarm_count = 1e6, // period = 1s @resolution 1MHz
    .flags.auto_reload_on_alarm = true, // enable auto-reload
};
ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config));

gptimer_event_callbacks_t cbs = {
    .on_alarm = example_timer_on_alarm_cb, // register user callback
};
ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, &tester));
ESP_ERROR_CHECK(gptimer_enable(gptimer));
ESP_ERROR_CHECK(gptimer_start(gptimer));

while(1){

    printf("J = %i  || ",tester.j);

    if(tester.queue_result == pdTRUE){
        printf("pdTRUE \n");
    }
    else if (tester.queue_result == pdFALSE){
        printf("pdFALSE \n");
    }

    vTaskDelay(pdMS_TO_TICKS(100));
}


}