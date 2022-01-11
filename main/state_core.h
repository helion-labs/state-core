#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "stdbool.h"

/*********************************************************
*                     TYPEDEFS
**********************************************************/
typedef uint32_t state_event_t; // Which event
typedef uint32_t state_t;       // Which state in a state machine

// Individual state functions in a state machine
typedef state_t (*func_ptr)(void);

// Individual state cleanup functions in a state machine
typedef void (*cleanup_ptr)(void);

// Defines the individual states, and if those states are reinterant,
// for example, if a state has loop_timer set to 1 tick, after 1 tick
// of not getting an event, it will run, and so forth.
typedef struct {
    // Function pointer to the state
    func_ptr state_function_pointer;

    // If non-zero, the period of a loop (in ticks)
    uint32_t loop_timer;
    
    // clean up function, NULL if not needed
    cleanup_ptr state_function_cleanup;

} state_array_s;

// Init function, used to set up a state machine
typedef struct {

    // This is the function that calculates the next state, based on input
    // Note that if a state returns a valid state_t, it will force the next
    // state. Otherwise, if a state returns NULL_STATE, a state change will 
    // happen based on input events.
    void (*next_state)(state_t*, state_event_t);

    // This must never be set by the user - internal private variable 
    QueueHandle_t state_queue_input_handle_private;  

    // Translates a event to a string (just for debug)
    char* (*event_print)(state_event_t);

    // Initial state of the state machine
    state_t starting_state;

    // For debug, name of the state
    char* state_name_string;

    // This function filters events so a state machine
    // can decide what events to react too
    bool (*filter_event)(state_event_t);

    // This is a pointer to a state array as such
    // state_array_s func_table[parser_state_len] = { 
    //    { state_function_pointer_a, int ticks_a , cleanup_func_a },
    //    { state_function_pointer_b, int ticks_b , cleanup_func_b },
    //    ...
    // }
    // 
    // Translates a state_e item to a state_array_s object
    state_array_s * translation_table;

    // Total number of states
    int total_states;

} state_init_s;

/**********************************************************
*                   GLOBAL FUNCTIONS
**********************************************************/
void state_post_event(state_event_t event);
void state_core_spawner();
void start_new_state_machine(state_init_s* state_ptr);

/**********************************************************
*                      GLOBALS    
*********************************************************/
extern QueueHandle_t events_net_q;

/**********************************************************
*                      DEFINES
**********************************************************/
#define GENERIC_QUEUE_TIMEOUT (2500 / portTICK_PERIOD_MS)
#define INVALID_EVENT         (0xFFFFFFFF)
#define EVENT_QUEUE_MAX_DEPTH (16)
#define STATE_MUTEX_WAIT      (2500 / portTICK_PERIOD_MS)
#define NULL_STATE            (0xFFFF)
