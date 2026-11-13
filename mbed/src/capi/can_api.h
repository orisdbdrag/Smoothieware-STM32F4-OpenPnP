/* mbed Microcontroller Library
 * Copyright (c) 2006-2013 ARM Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://apache.org
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef MBED_CAN_API_H
#define MBED_CAN_API_H

#include "PinNames.h"
#include "PeripheralNames.h"
#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t id;
    uint8_t  data[8];
    uint8_t  len;
} can_bridge_msg_t;

// Fast GPIO configuration structure for ultra-speed ISR routing
typedef struct {
    GPIO_TypeDef* gpio_port; // Memory pointer to hardware port (e.g. GPIOA)
    uint32_t      set_mask;  // Pre-calculated BSRR mask to turn pin HIGH
    uint32_t      rst_mask;  // Pre-calculated BSRR mask to turn pin LOW
} can_fast_pin_t;

#ifdef __cplusplus
extern "C" {
#endif
    uint8_t can_hardware_init(uint32_t baudrate);
    uint8_t can_hardware_write(can_bridge_msg_t *msg);
    void    can_hardware_free(void);
    void CAN1_RX0_IRQHandler(void);

    // Global configuration variables shared with CanBridge.cpp
    extern can_fast_pin_t can_endstop_x;
    extern can_fast_pin_t can_endstop_z;
    extern bool           can_rx_endstops_enable;
#ifdef __cplusplus
}
#endif

#endif // MBED_CAN_API_H
