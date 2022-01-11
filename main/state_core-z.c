#include <zephyr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "state_core.h"
#include "global_defines.h" 

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
static node_t*           head;
K_MSGQ_DEFINE(incoming_events_q, sizeof(state_event_t), EVENT_QUEUE_MAX_DEPTH, 4);
K_MUTEX_DEFINE(consumer_sem);

/**********************************************************
*                                               FUNCTIONS *
**********************************************************/

void add_event_consumer(state_init_s* thread_info) {
    ESP_LOGI(TAG, "Adding new state machine, name = %s", thread_info->state_name_string);

    if (k_mutex_lock(&consumer_sem, K_MSEC(STATE_MUTEX_WAIT))) {
        ESP_LOGE(TAG, "FAILED TO TAKE consumer_sem!");
        ASSERT(0);
    }

    if (head == NULL) {
        head = k_malloc(sizeof(node_t));
        ASSERT(head);

        head->next        = NULL;
        head->thread_info = thread_info;

        k_mutex_unlock(&consumer_sem);
        return;
    }

    node_t* iter = head;
    node_t* new  = NULL;
    while (iter->next != NULL) {
        iter = iter->next;
    }
    new = k_malloc(sizeof(node_t));
    ASSERT(new);

    iter->next       = new;
    new->thread_info = thread_info;
    new->next        = NULL;

    k_mutex_unlock(&consumer_sem);
}

// Returns the state function, given a state
static state_array_s get_state_table(state_init_s * state_ptr, state_t state) {
    
    if (state >= state_ptr->total_states) {
        ESP_LOGE(TAG, "Current state (%d) out of bounds in %s", state, state_ptr->state_name_string);
        ASSERT(0);
    }
    return state_ptr->translation_table[state];
}

// If the state does not define a get event fucntion, a generic one is provided
static state_event_t get_event_generic(struct k_msgq * q_handle, uint32_t timeout) {
    state_event_t new_event = INVALID_EVENT;

    // If a timeout, return an invalid event, this is used during the looper
    if(!timeout){
      k_msgq_get(q_handle, &new_event, K_FOREVER);
    } else { 
      k_msgq_get(q_handle, &new_event, K_MSEC(timeout));
    }

    return new_event;
}

// If the state does not define a send event fucntion, a generic one is provided
static void send_event_generic(struct k_msgq * q_handle, state_event_t event, char * name) {
    
    // Should never timeout
    if(k_msgq_put(q_handle, &event, K_MSEC(GENERIC_QUEUE_TIMEOUT))){
        ESP_LOGE(TAG, "Failed to send on event queue %s ", name);
        ASSERT(0);
    }
}

// Reads from a global event queue and sends the event
// to all state machines that have registered for the event
static void event_multiplexer(void* unused0, void * unused1, void * unused2) {

    ESP_LOGI(TAG, "Starting event event_multiplexer");
    for (;;) {
        state_event_t event;

        if (k_msgq_get(&incoming_events_q, (void*)&event, K_FOREVER)){
            ESP_LOGE(TAG, "Failed to rx... can't recover..");
            ASSERT(0);
        }

        ESP_LOGI(TAG, "RXed an event! %d", event);

        // Iterate through all the registered event consumers, see if they
        // signed up for an event, and if so, send the event to them
        if (k_mutex_lock(&consumer_sem, K_MSEC(STATE_MUTEX_WAIT))) {
            ESP_LOGE(TAG, "FAILED TO TAKE consumer_sem!");
            ASSERT(0);
        }

        node_t* iter = head;
        while (iter != NULL) {
            ESP_LOGI(TAG, "Checking to see if %s is interested in event...", iter->thread_info->state_name_string);
            if (iter->thread_info->filter_event(event)) {
                ESP_LOGI(TAG, "sending event %d to %s", event, iter->thread_info->state_name_string);
                send_event_generic(iter->thread_info->state_queue_input_handle, event, iter->thread_info->state_name_string);
            }
            iter = iter->next;
        }
        k_mutex_unlock(&consumer_sem);
    }
}

void state_post_event(state_event_t event) {
    if(k_msgq_put(&incoming_events_q, (void*)&event, K_NO_WAIT)){
        ESP_LOGE(TAG, "Failed to enqueue to event event_multiplexer!");
        ASSERT(0);
    }
}

// Not yet used, didn't port from freeRTOS
# if 0 
static void drain_events(state_init_s * state_ptr){
    if (!state_ptr) {
        ESP_LOGE(TAG, "ARG = NULL!");
        ASSERT(0);
    }
    state_event_t event;
    for(;;){
      BaseType_t xStatus = get_event_generic(state_ptr->state_queue_input_handle, 0);
      if (xStatus == pdPASS) continue;
    
      break;
    }
}
#endif 

static void state_machine(void* arg0, void* arg1, void* arg2) {
    if (!arg0) {
        ESP_LOGE(TAG, "ARG0 = NULL!");
        ASSERT(0);
    }

    state_init_s* state_init_ptr = (state_init_s*)(arg0);
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
          new_event = get_event_generic(state_init_ptr->state_queue_input_handle, state_info.loop_timer);

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


// Create the event multiplexer thread
K_THREAD_DEFINE(event_multiplexer_tid, EVENT_MUX_THREAD_SIZE,
                event_multiplexer, NULL, NULL, NULL,
                EVENT_MULTIPLEXER_PRIORITY, 0, 0);


// start a new thread
void start_new_state_machine(state_init_s* state_ptr, struct k_thread * t_data, k_thread_stack_t* t_area, size_t t_size) {
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
    if(!state_ptr->state_queue_input_handle){
       ESP_LOGE(TAG, "User should set state_queue_input_handle!");
       ASSERT(0);
    }

    if(state_ptr->total_states == 0){
       ESP_LOGE(TAG, "Total states len == 0!");
       ASSERT(0);
    }
    
    // Register new state machine with event multiplexer
    add_event_consumer(state_ptr);

    ESP_LOGI(TAG, "Starting new state %s", state_ptr->state_name_string);
    k_tid_t     tid = k_thread_create(t_data, t_area,
                                     t_size,
                                     state_machine,
                                     (void*)state_ptr, NULL, NULL,
                                     STATE_MACHINE_PRIORITY, 0, K_NO_WAIT);
    if(!tid){
        ESP_LOGE(TAG, "Could not create new thread!"); 
        ASSERT(0);
    }
}

#if 0
typedef enum {
    test_state_a = 0,
    test_state_b, 
    
    test_state_len
} cellular_state_e;


static state_t state_a() {
  printf("In state a \n");
  k_sleep(K_MSEC(1000));
  return test_state_b;
}

static state_t state_b() {
  printf("In state b \n");
  return test_state_a;
}

static void exit_func() {
  printf("exiting state a \n");
}

typedef enum {
    EVENT_X = 100,
    EVENT_Y,

    wifi_event_len //LEAVE AS LAST!
} test_event_e;


static void next_state_func(state_t* curr_state, state_event_t event) {
    if (!curr_state) {
        ESP_LOGE(TAG, "ARG= NULL!");
        ASSERT(0);
    }

    if (*curr_state == test_state_a) {
        if (event == EVENT_X) {
            ESP_LOGI(TAG, "Old State: (A) , Next: (B)");
            *curr_state = test_state_b;
            return;
        }
    }

    if (*curr_state == test_state_b) {
        if (event == EVENT_Y) {
            ESP_LOGI(TAG, "Old State: (B) , Next: (A)");
            *curr_state = test_state_a;
            return;
        }
    }
}


static bool event_filter_func(state_event_t event) {
    return true;
}


static state_array_s func_translation_table[test_state_len] = {
      { state_a , 1000 , exit_func},
      { state_b , 0    , NULL     },
};


static char* event_print_func(state_event_t event) {
    return NULL;
}

static state_init_s* get_cellular_state_handle() {
    static char __aligned(4) my_msgq_buffer[10 * sizeof(state_t)];
    static struct k_msgq my_msgq;
    k_msgq_init(&my_msgq, my_msgq_buffer, sizeof(state_t), 10);

    static state_init_s cellular_state = {
        .next_state                       = next_state_func,
        .translation_table                = func_translation_table,
        .starting_state                   = test_state_a,
        .event_print                      = event_print_func,
        .state_name_string                = "test_state",
        .filter_event                     = event_filter_func,
        .total_states                     = test_state_len,
        .state_queue_input_handle = &my_msgq,
    };
    return &(cellular_state);
}

K_THREAD_STACK_DEFINE(my_stack_area, 1024);
int main(){
  static struct k_thread my_thread_data;  

  start_new_state_machine(get_cellular_state_handle(), &my_thread_data, my_stack_area, 1024);
  
  //state_post_event(EVENT_X);
}
#endif
