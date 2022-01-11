#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "global_defines.h"
#include "state_core.h"
#include "state_test.h"

/*********************************************************
*                                       STATIC VARIABLES *
*********************************************************/
static const char        TAG[] = "TEST_STATE";

/**********************************************************
*                                         STATE FUNCTIONS *
**********************************************************/

// Do nothing state
// we will transition on an event (TEST_EVENT_A)
// this will cause us to go from state_a to state_b
static state_t state_a() {
  ESP_LOGI(TAG, "Entering state A");
  
  return NULL_STATE;
}

// clean up function
// called every time we leave state_a
static void cleanup_state_a() {
  ESP_LOGI(TAG, "Entering state_a Cleanup");
}  

// This event is set to auto loop every N ticks
// We wil wait for 10 ticks and force us to enter state b
static state_t state_b() {
  static int entry_count_b = 0;
  ESP_LOGI(TAG, "Entering state B for the %d time", entry_count_b++);
  
  if (entry_count_b == 10){
    entry_count_b = 0;
    return state_a_enum;
  }
  return NULL_STATE;
}

// Returns the next state
static void next_state_func(state_t* curr_state, state_event_t event) {
    if (!curr_state) {
        ESP_LOGE(TAG, "ARG==NULL!");
        ASSERT(0);
    }

    if (*curr_state == state_a_enum) {
        if (event == TEST_EVENT_A) {
            ESP_LOGI(TAG, "Old State: test_state_a, Next: test_state_b");
            *curr_state = state_b_enum;
            return;
        }
    }
    // Stay in the same state
}

// These need to sync up to the enum in state_test.h (test_state_e)
static state_array_s func_translation_table[test_state_len] = {
   { state_a      ,  portMAX_DELAY                             , cleanup_state_a },
   { state_b      ,  250/portTICK_PERIOD_MS                    , NULL },
};


// if this state machine is interested in an event
// returns true
static bool event_filter_func(state_event_t event) {
    if (event == TEST_EVENT_A){
      return true;
    }
    return false;
}


static char* event_print_func(state_event_t event) {
  static char event_a_st[] = "TEST_EVENT_A";
  if(event == TEST_EVENT_A){
    return event_a_st;
  }

  // If no matching event was found, return NULL
  // (probably an error)
  return NULL;
}


static state_init_s* get_test_state_handle() {
    static state_init_s test_state = {
        .next_state        = next_state_func,
        .translation_table = func_translation_table,
        .event_print       = event_print_func,
        .starting_state    = state_a_enum,
        .state_name_string = "test_state",
        .filter_event      = event_filter_func,
        .total_states      = test_state_len,
    };
    return &(test_state);
}

void test_state_spawner() {
    // State the state machine
    start_new_state_machine(get_test_state_handle());
}
