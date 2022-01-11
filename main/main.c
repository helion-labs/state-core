#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"


#include "global_defines.h"
#include "state_test.h"

/**********************************************************
*                                        STATIC VARIABLES *
**********************************************************/
static const char        TAG[] = "MAIN";

void app_main(void) {
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret != ESP_OK) {
      while(true) {
        ESP_LOGE(TAG, "Critical Error - failed to init NVS - not much else can be done... looping. Error %s:", esp_err_to_name(ret));
        vTaskDelay(RTOS_LONG_TIME);
    }
  }

  state_core_spawner();
  test_state_spawner();

  while(true){
    state_post_event(TEST_EVENT_A);      // this will cause us to go from state_a -> state_b
    vTaskDelay(5000/portTICK_PERIOD_MS); // delay for 5 seconds

    // Inside state_b, we will loop 10 times and force entry into state_a on the 10th time, this will be done in 
    // less than 5 seconds.
    // State B is set to loop on an interval of [200 / portTICK_PERIOD_MS], aka 200ms period
  }
}
