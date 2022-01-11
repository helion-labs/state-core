#pragma once

#define ASSERT(x)                                                       \
    do {                                                                \
        if (!(x)) {                                                     \
            printf( "ASSERT! error %s %u\n", __FILE__, __LINE__);       \
            for (;;) {                                                  \
               vTaskDelay(10);                                          \
            }                                                           \
        }                                                               \
    } while (0)

// Random "long" period
#define RTOS_LONG_TIME (0xFFFF00)
#define RTOS_DONT_WAIT (0)


#define EVENT_START_TEST (100)
