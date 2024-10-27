/* Blink Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "led_strip.h"
#include "sdkconfig.h"

static const char *TAG = "example";

/* Use project configuration menu (idf.py menuconfig) to choose the GPIO to blink,
   or you can edit the following line and set a number here.
*/
#define BLINK_GPIO 21
#define YELLOW     GPIO_NUM_1
#define GREEN      GPIO_NUM_2
#define BLUE       GPIO_NUM_3


static uint8_t s_led_state = 0;
int i = 0;

static void blink_led(void){
    /* Set the GPIO level according to the state (LOW or HIGH)*/
    gpio_set_level(BLINK_GPIO, s_led_state);
}

static void configure_leds(void){
    ESP_LOGI(TAG, "Example configured to blink GPIO LED!");
    gpio_reset_pin(BLINK_GPIO);
    gpio_reset_pin(YELLOW);
    gpio_reset_pin(GREEN);
    gpio_reset_pin(BLUE);

    /* Set the GPIO as a push/pull output */
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_direction(YELLOW, GPIO_MODE_OUTPUT);
    gpio_set_direction(GREEN, GPIO_MODE_OUTPUT);
    gpio_set_direction(BLUE, GPIO_MODE_OUTPUT);

    gpio_set_level(BLINK_GPIO, 0);
    gpio_set_level(YELLOW, 0);
    gpio_set_level(GREEN, 0);
    gpio_set_level(BLUE, 0);

}


void app_main(void)
{

    /* Configure the peripheral according to the LED type */
    configure_leds();


    while (1) {
        ESP_LOGI(TAG, "Turning the LED %s!", s_led_state == true ? "ON" : "OFF");
        blink_led();
        /* Toggle the LED state */
        s_led_state = !s_led_state;

        switch (i)
        {
        case 0:
            gpio_set_level(BLUE, 0);
            gpio_set_level(YELLOW, 1);
            printf("Yellow\n");

            break;
        case 1:
            gpio_set_level(YELLOW, 0);
            gpio_set_level(GREEN, 1);
            printf("Green\n");

            break;
        case 2:
            gpio_set_level(GREEN, 0);
            gpio_set_level(BLUE, 1);
            printf("Blue\n");

            i = -1;
            break;

        default:
            break;
        }
        i++;
        printf("i = %i\n", i);
        vTaskDelay(pdMS_TO_TICKS(2500));

    }
}