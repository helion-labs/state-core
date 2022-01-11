#pragma once
#include "state_core.h"

/***********************************************************
 *                                                 GLOBALS *
 **********************************************************/
void test_state_spawner();


/***********************************************************
 *                                                   ENUMS *
 **********************************************************/
typedef enum {
  state_a_enum = 0,
  state_b_enum,

  test_state_len //LEAVE AS LAST!
} test_state_e;


typedef enum {
  TEST_EVENT_A = EVENT_START_TEST,
 
  test_event_len //LEAVE AS LAST!
} test_event_e;
