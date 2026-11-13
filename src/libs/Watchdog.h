/*
 * Watchdog.h [Refactored for IWDG Long Timeouts]
 * Path: src/libs/Watchdog.h
 */

#ifndef _WATCHDOG_H
#define _WATCHDOG_H

#include <stdint.h>
#include "Module.h"

typedef enum
{
    WDT_MRI,
    WDT_RESET,
} WDT_ACTION;

class Watchdog : public Module
{
public:
    Watchdog(uint32_t timeout_us, WDT_ACTION action);
    void feed();

    void on_module_loaded();
    void on_idle(void*);
};

#endif /* _WATCHDOG_H */
