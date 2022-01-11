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

/**********************************************************
*                                        GLOBAL VARIABLES *
**********************************************************/

/**********************************************************
*                                                TYPEDEFS *
**********************************************************/
typedef struct node {
    struct node*  next;
    state_init_s* thread_info;
} node_t;

/**********************************************************
*                                        STATIC VARIABLES *
**********************************************************/
static const char        TAG[] = "STATE_CORE";
static QueueSetHandle_t  incoming_events_q;
static node_t*           head;
static SemaphoreHandle_t consumer_sem;

/**********************************************************
*                                               FUNCTIONS *
**********************************************************/

void add_event_consumer(state_init_s* thread_info) {
    ESP_LOGI(TAG, "Adding new state machine, name = %s", thread_info->state_name_string);

    if (pdTRUE != xSemaphoreTake(consumer_sem, STATE_MUTEX_WAIT)) {
        ESP_LOGE(TAG, "FAILED TO TAKE consumer_sem!");
        ASSERT(0);
    }

    if (head == NULL) {
        head = malloc(sizeof(node_t));
        ASSERT(head);

        head->next        = NULL;
        head->thread_info = thread_info;

        xSemaphoreGive(consumer_sem);
        return;
    }

    node_t* iter = head;
    node_t* new  = NULL;
    while (iter->next != NULL) {
        iter = iter->next;
    }
    new = malloc(sizeof(node_t));
    ASSERT(new);

    iter->next       = new;
    new->thread_info = thread_info;
    new->next        = NULL;
    xSemaphoreGive(consumer_sem);
}

// Returns the state function, given a state
static state_array_s get_state_table(state_init_s * state_ptr, state_t state) {
    
    if (state >= state_ptr->total_states) {
        ESP_LOGE(TAG, "Current state (%d) out of bounds in %s", state, state_ptr->state_name_string);
        ASSERT(0);
    }
    return state_ptr->translation_table[state];
}


// used to read from events sent to this state machine
static state_event_t get_event_generic(QueueHandle_t q_handle, uint32_t timeout) {
    if (!q_handle){
      ESP_LOGE(TAG, "NULL HANDLE!");
      ASSERT(0);
    }
    state_event_t new_event = INVALID_EVENT;
    xQueueReceive(q_handle, &new_event, timeout);
    return new_event;
}

static void send_event_generic(QueueHandle_t q_handle, state_event_t event, char * name) {
    if (!q_handle){
      ESP_LOGE(TAG, "NULL HANDLE!");
      ASSERT(0);
    }

    // Should never timeout
    BaseType_t xStatus = xQueueSendToBack(q_handle, &event, GENERIC_QUEUE_TIMEOUT);
    if (xStatus != pdTRUE) {
        ESP_LOGE(TAG, "Failed to send on event queue %s ", name);
        ASSERT(0);
    }
}

// Reads from a global event queue and sends the event
// to all state machines that have registered for the event
static void event_multiplexer(void* v) {
    ESP_LOGI(TAG, "Starting event event_multiplexer");
    for (;;) {
        state_event_t event;
        BaseType_t    xStatus;

        xStatus = xQueueReceive(incoming_events_q, (void*)&event, portMAX_DELAY);
        if (xStatus != pdTRUE) {
            ESP_LOGE(TAG, "Failed to rx... can't recover..");
            ASSERT(0);
        }

        ESP_LOGI(TAG, "RXed an event! %d", event);

        // Iterate through all the registered event consumers, see if they
        // signed up for an event, and if so, send the event to them
        if (pdTRUE != xSemaphoreTake(consumer_sem, STATE_MUTEX_WAIT)) {
            ESP_LOGE(TAG, "FAILED TO TAKE consumer_sem!");
            ASSERT(0);
        }

        node_t* iter = head;
        while (iter != NULL) {
            ESP_LOGI(TAG, "Checking to see if %s is interested in event...", iter->thread_info->state_name_string);
            if (iter->thread_info->filter_event(event)) {
                ESP_LOGI(TAG, "sending event %d to %s", event, iter->thread_info->state_name_string);
                send_event_generic(iter->thread_info->state_queue_input_handle_private, event, iter->thread_info->state_name_string);
            }
            iter = iter->next;
        }
        xSemaphoreGive(consumer_sem);
    }
}

static void state_core_init_freertos_objects() {
    //Reads and Pushes events from state-machines
    incoming_events_q = xQueueCreate(EVENT_QUEUE_MAX_DEPTH, sizeof(state_event_t)); // state-machines -> state-core
    consumer_sem      = xSemaphoreCreateMutex();

    // make sure nothing is NULL!
    ASSERT(incoming_events_q);
    ASSERT(consumer_sem);
}

void state_post_event(state_event_t event) {
    BaseType_t xStatus = xQueueSendToBack(incoming_events_q, (void*)&event, RTOS_DONT_WAIT);
    if (xStatus != pdTRUE) {
        ESP_LOGE(TAG, "Failed to enqueue to event event_multiplexer!");
        ASSERT(0);
    }
}

static void state_machine(void* arg) {
    if (!arg) {
        ESP_LOGE(TAG, "ARG = NULL!");
        ASSERT(0);
    }

    state_init_s* state_init_ptr = (state_init_s*)(arg);
    state_t       state          = state_init_ptr->starting_state;
    state_t       forced_state   = NULL_STATE;
    state_event_t new_event;
    
    for (;;) {
        // Get the current state information
        state_array_s state_info = get_state_table(state_init_ptr, state);
        func_ptr      state_func = state_info.state_function_pointer;
        cleanup_ptr   clean_func = state_info.state_function_cleanup;

        // Run the current state
        forced_state = state_func();

        if (forced_state != NULL_STATE){
          // Previous state is forcing next state, don't read from queue
          ESP_LOGI(TAG, "State %s is forcing next state (%d)", state_init_ptr->state_name_string, forced_state );
          state = forced_state;

          // do cleanup function
          if(clean_func){
            clean_func();
          }
          continue;
        }
        
        state_t curr_state = state;
        for(;;){
          // Wait until a new event comes
          new_event = get_event_generic(state_init_ptr->state_queue_input_handle_private, state_info.loop_timer);

          // Recieved an event, see if we need to change state
          // Don't run if we had a timeout (looping)
          if (new_event != INVALID_EVENT){
            ESP_LOGI(TAG, "(%s) In state %d, got event %d", state_init_ptr->state_name_string, state, new_event );
            state_init_ptr->next_state(&state, new_event);
          } else {
            // loop
            break; 
          }
          
          // check to see if there was a state change
          // only run the state machine in that case
          if (curr_state != state){
            if(clean_func){
              clean_func();
            }
            break;
          }
        }
    }
}


void start_new_state_machine(state_init_s* state_ptr) {
    if (!state_ptr) {
        ESP_LOGE(TAG, "ARG==NULL!");
        ASSERT(0);
    }

    // Sanity check(s)
    if (state_ptr->event_print == NULL || state_ptr->next_state == NULL) {
        ESP_LOGE(TAG, "ERROR! event_print / next_state func was NULL!");
        ASSERT(0);
    }

    // Sanity check(s)
    if (state_ptr->translation_table == NULL || state_ptr->state_name_string == NULL) {
        ESP_LOGE(TAG, "ERROR! translation_table / state_name_string  was NULL!");
        ASSERT(0);
    }

    // user has set state_queue_input_handle?
    if(state_ptr->state_queue_input_handle_private){
       ESP_LOGE(TAG, "User should not set state_queue_input_handle_private!");
       ASSERT(0);
    }

    if(state_ptr->total_states == 0){
       ESP_LOGE(TAG, "Total states len == 0!");
       ASSERT(0);
    }
      
    state_ptr->state_queue_input_handle_private = xQueueCreate(EVENT_QUEUE_MAX_DEPTH, sizeof(state_event_t)); 

    // make sure we init all the rtos objects
    ASSERT(state_ptr->state_queue_input_handle_private);

    // Register new state machine with event multiplexer
    add_event_consumer(state_ptr);

    ESP_LOGI(TAG, "Starting new state %s", state_ptr->state_name_string);
    BaseType_t rc = xTaskCreate(state_machine,
                                state_ptr->state_name_string,
                                4096,
                                (void*)state_ptr,
                                4,
                                NULL);

    if (rc != pdPASS) {
        ASSERT(0);
    }
}

void state_core_spawner() {
    BaseType_t rc;

    state_core_init_freertos_objects();
    rc = xTaskCreate(event_multiplexer,
                     "event_multiplexer",
                     4096,
                     NULL,
                     4,
                     NULL);

    if (rc != pdPASS) {
        ASSERT(0);
    }
}
