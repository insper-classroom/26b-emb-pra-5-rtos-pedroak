/*
 * LED blink with FreeRTOS
 */
#include <FreeRTOS.h>
#include <queue.h>
#include <semphr.h>
#include <stdio.h>
#include <task.h>

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"
#include "pins.h"
#include "ssd1306.h"

// === Definições para SSD1306 ===


QueueHandle_t xQueueDist, xQueueTime;
SemaphoreHandle_t xSemaphoreTrigger;

void pin_callback(uint gpio, uint32_t events) {
    if (gpio == ECHO_PIN) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        uint64_t time = time_us_64();

        if (events & (GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL)) {
            xQueueSendFromISR(xQueueTime, &time, &xHigherPriorityTaskWoken);
        }
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

void oled_display_init(ssd1306_t *disp) {
    i2c_init(i2c1, 400000);
    gpio_set_function(2, GPIO_FUNC_I2C);
    gpio_set_function(3, GPIO_FUNC_I2C);
    gpio_pull_up(2);
    gpio_pull_up(3);

    disp->external_vcc = false;
    ssd1306_init(disp, 128, 64, 0x3C, i2c1);
    ssd1306_clear(disp);
    ssd1306_show(disp);

    gpio_init(SSD1306_PIN_LITE);
    gpio_set_dir(SSD1306_PIN_LITE, GPIO_OUT);
    gpio_put(SSD1306_PIN_LITE, 0);
}

void led_rgb_init(void) {
    gpio_init(LED_PIN_R);
    gpio_set_dir(LED_PIN_R, GPIO_OUT);
    gpio_put(LED_PIN_R, 1);

    gpio_init(LED_PIN_G);
    gpio_set_dir(LED_PIN_G, GPIO_OUT);
    gpio_put(LED_PIN_G, 1);

    gpio_init(LED_PIN_B);
    gpio_set_dir(LED_PIN_B, GPIO_OUT);
    gpio_put(LED_PIN_B, 1);
}


void trigger_task(void *p) {
    gpio_init(TRIG_PIN);
    gpio_set_dir(TRIG_PIN, GPIO_OUT);
    gpio_put(TRIG_PIN, 0);

    gpio_init(ECHO_PIN);
    gpio_set_dir(ECHO_PIN, GPIO_IN);
    gpio_pull_down(ECHO_PIN);
    gpio_set_irq_enabled_with_callback(ECHO_PIN,
                                   GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL,
                                   true, &pin_callback);

    while (1) {
        gpio_put(TRIG_PIN, 1);
        sleep_us(10);
        gpio_put(TRIG_PIN, 0);

        xSemaphoreGive(xSemaphoreTrigger);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void echo_task(void *p) {
    uint64_t time = 0;

    while (1) {
        if (xQueueReceive(xQueueTime, &time, portMAX_DELAY) == pdTRUE) {
            uint64_t start = time;

            if (xQueueReceive(xQueueTime, &time, pdMS_TO_TICKS(100)) == pdTRUE) {
                uint64_t delta = time - start;
                float distance = (float)delta * 0.0343f / 2.0f;
                xQueueSend(xQueueDist, &distance, 0);  // <-- FALTAVA ISSO!
            }
        }
    }
}

void oled_task(void *p) {
    ssd1306_t disp;
    oled_display_init(&disp);
    ssd1306_draw_string(&disp, 0, 0, 1, "Aguardando...");
    ssd1306_show(&disp);
    led_rgb_init();

    float distance = 0;

    while (1) {
        if (xSemaphoreTake(xSemaphoreTrigger, portMAX_DELAY) == pdTRUE) {
            ssd1306_clear(&disp);

            if (xQueueReceive(xQueueDist, &distance, pdMS_TO_TICKS(150)) == pdTRUE) {

                if (distance <= 100) {
                    gpio_put(LED_PIN_B, 1);
                    gpio_put(LED_PIN_G, 0);
                    gpio_put(LED_PIN_R, 1);
                } else {
                    gpio_put(LED_PIN_B, 1);
                    gpio_put(LED_PIN_G, 0);
                    gpio_put(LED_PIN_R, 0);
                }
                int w = (int)(distance / 400 * 128);
                if (w > 128) w = 128;
                char buf[16];
                snprintf(buf, sizeof(buf), "%.1f cm", distance);
                ssd1306_draw_string(&disp, 0, 0, 2, buf);
                ssd1306_draw_square(&disp, 0, 30, w, 10);
            } else {
                gpio_put(LED_PIN_B, 1);
                gpio_put(LED_PIN_G, 1);
                gpio_put(LED_PIN_R, 0);
                ssd1306_draw_string(&disp, 0, 0, 2, "ERRO");
                ssd1306_draw_string(&disp, 0, 20, 1, "Sem leitura");
            }
            ssd1306_show(&disp);
        }
    }
}

int main() {
    stdio_init_all();


    xQueueDist = xQueueCreate(32, sizeof(float));
    xQueueTime = xQueueCreate(32, sizeof(uint64_t));
    xSemaphoreTrigger = xSemaphoreCreateBinary();

    xTaskCreate(trigger_task, "Trigger", 4095, NULL, 1, NULL);
    xTaskCreate(echo_task, "Echo", 4095, NULL, 1, NULL);
    xTaskCreate(oled_task, "Oled", 4095, NULL, 1, NULL);

    vTaskStartScheduler();
    while (true);
}
