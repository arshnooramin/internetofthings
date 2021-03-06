/* LEDC (LED Controller) basic example
   This example code is in the Public Domain (or CC0 licensed, at your option.)
   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include <string.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "driver/rmt.h"
#include "ir_tools.h"
#include "led_strip.h"1

#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_OUTPUT_IO          (5) // Define the output GPIO
#define LEDC_CHANNEL            LEDC_CHANNEL_0
#define LEDC_DUTY_RES           LEDC_TIMER_13_BIT // Set duty resolution to 13 bits
#define LEDC_DUTY_MIN               (250) // Set duty to 50%. ((2 ** 13) - 1) * 50% = 4095
#define LEDC_DUTY_MAX               (950)
#define LEDC_FREQUENCY          (50) // Frequency in Hertz.
#define MAX_ANGLE (180)
#define ANGLE_DELTA_INITIAL (10)


#define RMT_TX_CHANNEL RMT_CHANNEL_0

#define EXAMPLE_CHASE_SPEED_MS (1000)

static const char *TAG = "receiver";
static rmt_channel_t example_rx_channel = RMT_CHANNEL_2;

//ir codes
static const int UP = 0xeb14;
static const int DOWN = 0xef10;
static const int QUICK = 0xe817;
static const int SLOW = 0xec13;
static const int RED = 0xa758;
static const int GREEN = 0xa659;
static const int BLUE = 0xba45;
static const int WHITE = 0xbb44;
static const int POWER = 0xbf40;
static const int B_UP = 0xa35c;
static const int B_DOWN = 0xa25d;

static int angle_to_duty(int angle){
    return (int)(LEDC_DUTY_MIN + (float)angle / MAX_ANGLE * (LEDC_DUTY_MAX - LEDC_DUTY_MIN));
}

static void example_ir_rx_task()//void *arg)
{
    rmt_config_t config = RMT_DEFAULT_CONFIG_TX(CONFIG_EXAMPLE_RMT_TX_GPIO, RMT_TX_CHANNEL);
    // set counter clock to 40MHz
    config.clk_div = 2;

    ESP_ERROR_CHECK(rmt_config(&config));
    ESP_ERROR_CHECK(rmt_driver_install(config.channel, 0, 0));

    // install ws2812 driver
    led_strip_config_t strip_config = LED_STRIP_DEFAULT_CONFIG(3, (led_strip_dev_t)config.channel);
    led_strip_t *strip = led_strip_new_rmt_ws2812(&strip_config);
    if (!strip) {
        ESP_LOGE(TAG, "install WS2812 driver failed");
    }
    // Clear LED strip (turn off all LEDs)
    ESP_ERROR_CHECK(strip->clear(strip, 100));

    int angle = 0;
    int angle_delta = ANGLE_DELTA_INITIAL;
    uint32_t addr = 0;
    uint32_t cmd = 0;
    size_t length = 0;
    bool repeat = false;
    RingbufHandle_t rb = NULL;
    rmt_item32_t *items = NULL;

    int rgb_value = 125;
    int rgb_status = RED;
    bool rgb_on = false;

    rmt_config_t rmt_rx_config = RMT_DEFAULT_CONFIG_RX(CONFIG_EXAMPLE_RMT_RX_GPIO, example_rx_channel);
    rmt_config(&rmt_rx_config);
    rmt_driver_install(example_rx_channel, 1000, 0);
    ir_parser_config_t ir_parser_config = IR_PARSER_DEFAULT_CONFIG((ir_dev_t)example_rx_channel);
    ir_parser_config.flags |= IR_TOOLS_FLAGS_PROTO_EXT; // Using extended IR protocols (both NEC and RC5 have extended version)
    ir_parser_t *ir_parser = NULL;
#if CONFIG_EXAMPLE_IR_PROTOCOL_NEC
    ir_parser = ir_parser_rmt_new_nec(&ir_parser_config);
#elif CONFIG_EXAMPLE_IR_PROTOCOL_RC5
    ir_parser = ir_parser_rmt_new_rc5(&ir_parser_config);
#endif

    //get RMT RX ringbuffer
    rmt_get_ringbuf_handle(example_rx_channel, &rb);
    assert(rb != NULL);
    // Start receive
    rmt_rx_start(example_rx_channel, true);
    while (1) {
        items = (rmt_item32_t *) xRingbufferReceive(rb, &length, portMAX_DELAY);
        if (items) {
            length /= 4; // one RMT = 4 Bytes
            if (ir_parser->input(ir_parser, items, length) == ESP_OK) {
                if (ir_parser->get_scan_code(ir_parser, &addr, &cmd, &repeat) == ESP_OK) {
                    ESP_LOGI(TAG, "Scan Code %s --- addr: 0x%04x cmd: 0x%04x", repeat ? "(repeat)" : "", addr, cmd);
                    if (cmd == UP) { //up arrow
                        ESP_LOGI(TAG, "up");
                        if (angle + angle_delta <= MAX_ANGLE) angle += angle_delta;
                        else angle = MAX_ANGLE;
                        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, angle_to_duty(angle));
                        ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
                        vTaskDelay(10);
                    } else if (cmd == DOWN){ // down arrow
                        ESP_LOGI(TAG, "down");
                        if (angle - angle_delta >= 0) angle -= angle_delta;
                        else angle = 0;
                        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, angle_to_duty(angle));
                        ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
                        vTaskDelay(10);
                    } else if (cmd == QUICK){ //quick //0xec13
                        if (angle_delta < 30){
                            ESP_LOGI(TAG, "quick");
                            angle_delta++;
                        } 
                    } else if (cmd == SLOW){ //slow
                        if (angle_delta > 2){
                            ESP_LOGI(TAG, "slow");
                            angle_delta--;
                        } 
                    } else if (cmd == RED){
                        // Write RGB values to strip driver
                        rgb_status = RED;
                        rgb_on = true;
                        ESP_LOGI(TAG, "red");
                        ESP_ERROR_CHECK(strip->set_pixel(strip, 0, rgb_value, 0, 0));
                        ESP_ERROR_CHECK(strip->refresh(strip, 100));
                    } else if (cmd == GREEN){
                        rgb_status = GREEN;
                        rgb_on = true;
                        ESP_LOGI(TAG, "green");
                        ESP_ERROR_CHECK(strip->set_pixel(strip, 0, 0, rgb_value, 0));
                        ESP_ERROR_CHECK(strip->refresh(strip, 100));
                    } else if (cmd == BLUE){
                        rgb_status = BLUE;
                        rgb_on = true;
                        ESP_LOGI(TAG, "blue");
                        ESP_ERROR_CHECK(strip->set_pixel(strip, 0, 0, 0, rgb_value));
                        ESP_ERROR_CHECK(strip->refresh(strip, 100));
                    } else if (cmd == WHITE) {
                        rgb_status = WHITE;
                        rgb_on = true;
                        ESP_LOGI(TAG, "white");
                        ESP_ERROR_CHECK(strip->set_pixel(strip, 0, rgb_value, rgb_value, rgb_value));
                        ESP_ERROR_CHECK(strip->refresh(strip, 100));
                    } else if (cmd == POWER) {
                        if (rgb_on) {
                            ESP_LOGI(TAG, "power");
                            rgb_on = false;
                            strip->clear(strip, 100);
                        } else {
                            rgb_on = true;
                            if (rgb_status == RED) {
                                ESP_ERROR_CHECK(strip->set_pixel(strip, 0, rgb_value, 0, 0));
                                ESP_ERROR_CHECK(strip->refresh(strip, 100));
                            } else if (rgb_status == GREEN) {
                                ESP_ERROR_CHECK(strip->set_pixel(strip, 0, 0, rgb_value, 0));
                                ESP_ERROR_CHECK(strip->refresh(strip, 100));
                            } else if (rgb_status == BLUE) {
                                ESP_ERROR_CHECK(strip->set_pixel(strip, 0, 0, 0, rgb_value));
                                ESP_ERROR_CHECK(strip->refresh(strip, 100));
                            } else if (rgb_status == WHITE) {
                                ESP_ERROR_CHECK(strip->set_pixel(strip, 0, rgb_value, rgb_value, rgb_value));
                                ESP_ERROR_CHECK(strip->refresh(strip, 100));
                            }
                        }
                    } else if (cmd == B_UP) {
                        if (rgb_on && rgb_value <= 245) {
                            rgb_value = rgb_value + 10;
                            if (rgb_status == RED) {
                                ESP_ERROR_CHECK(strip->set_pixel(strip, 0, rgb_value, 0, 0));
                                ESP_ERROR_CHECK(strip->refresh(strip, 100));
                            } else if (rgb_status == GREEN) {
                                ESP_ERROR_CHECK(strip->set_pixel(strip, 0, 0, rgb_value, 0));
                                ESP_ERROR_CHECK(strip->refresh(strip, 100));
                            } else if (rgb_status == BLUE) {
                                ESP_ERROR_CHECK(strip->set_pixel(strip, 0, 0, 0, rgb_value));
                                ESP_ERROR_CHECK(strip->refresh(strip, 100));
                            } else if (rgb_status == WHITE) {
                                ESP_ERROR_CHECK(strip->set_pixel(strip, 0, rgb_value, rgb_value, rgb_value));
                                ESP_ERROR_CHECK(strip->refresh(strip, 100));
                            }
                        }
                    } else if (cmd == B_DOWN) {
                        if (rgb_on && rgb_value >= 10) {
                            rgb_value = rgb_value - 10;
                            if (rgb_status == RED) {
                                ESP_ERROR_CHECK(strip->set_pixel(strip, 0, rgb_value, 0, 0));
                                ESP_ERROR_CHECK(strip->refresh(strip, 100));
                            } else if (rgb_status == GREEN) {
                                ESP_ERROR_CHECK(strip->set_pixel(strip, 0, 0, rgb_value, 0));
                                ESP_ERROR_CHECK(strip->refresh(strip, 100));
                            } else if (rgb_status == BLUE) {
                                ESP_ERROR_CHECK(strip->set_pixel(strip, 0, 0, 0, rgb_value));
                                ESP_ERROR_CHECK(strip->refresh(strip, 100));
                            } else if (rgb_status == WHITE) {
                                ESP_ERROR_CHECK(strip->set_pixel(strip, 0, rgb_value, rgb_value, rgb_value));
                                ESP_ERROR_CHECK(strip->refresh(strip, 100));
                            }
                        }
                    }
                }
            }
            //after parsing the data, return spaces to ringbuffer.
            vRingbufferReturnItem(rb, (void *) items);
        }
    }
    ir_parser->del(ir_parser);
    rmt_driver_uninstall(example_rx_channel);
    vTaskDelete(NULL);
}


static void example_ledc_init(void)
{
    // Prepare and then apply the LEDC PWM timer configuration
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .timer_num        = LEDC_TIMER,
        .duty_resolution  = LEDC_DUTY_RES,
        .freq_hz          = LEDC_FREQUENCY,  // Set output frequency at 5 kHz
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // Prepare and then apply the LEDC PWM channel configuration
    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = LEDC_OUTPUT_IO,
        .duty           = LEDC_DUTY_MIN,//0, // Set duty to 0%
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}



void app_main(void)
{
    // Set the LEDC peripheral configuration
    example_ledc_init();
    vTaskDelay(10);

    // while(1){
    //     ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, angle_to_duty(0));
    //     ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    //     vTaskDelay(100);
    //     ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, angle_to_duty(90)); //819
    //     ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    //     vTaskDelay(100);
    //     ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, angle_to_duty(180)); //819
    //     ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    //     vTaskDelay(100);
    // }
    example_ir_rx_task();

}
