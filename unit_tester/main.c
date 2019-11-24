#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"
#include "test_utils.h"
#include "unity.h"

static void unity_task( void *pvParameters )
{
    vTaskDelay( 2 ); /* Delay a bit to let the main task be deleted */
    esp_task_wdt_delete( xTaskGetIdleTaskHandle() );
    unity_run_menu(); /* Doesn't return */
}

void app_main( void )
{
    xTaskCreatePinnedToCore( unity_task, "unityTask", UNITY_FREERTOS_STACK_SIZE, NULL, UNITY_FREERTOS_PRIORITY, NULL,
                             UNITY_FREERTOS_CPU );
}
