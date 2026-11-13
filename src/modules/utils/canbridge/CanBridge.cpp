/*
 * HIGH-SPEED REAL-TIME CAN-BUS BRIDGE & PERIPHERAL CONTROLLER
 * This module extends the Smoothieware core with low-level bxCAN hardware support,
 * purpose-built for ultra-low latency actuation of head-mounted valves and smart feeders.
 *
 * Key Architecture Highlights:
 * 1. Zero-Latency Hardware Endstop Mapping:
 *    Parses physical endstop configuration strings at boot time and pre-compiles
 *    GPIO memory maps (direct port pointer, BSRR Set and Reset masks) based on pin inversion state.
 *    This enables instantaneous, bitwise hardware port toggling upon incoming CAN endstop events
 *    with absolutely zero runtime instruction or logic overhead.
 *
 * 2. Synchronized G-code Execution Loop (M777):
 *    Intercepts custom  peripheral control commands within the synchronized
 *    motion queue. Uses pre-cached, integer-optimized O(1) G-code parameter lookups (P, N, S)
 *    and immediately dispatches payload frames to the STM32F4 bxCAN transmit mailboxes via direct
 *    hardware register writes, ensuring precise coordination with machine movement.
 */




#include "CanBridge.h"

#ifndef NO_CAN_BRIDGE

#include "can_api.h"
#include "libs/Kernel.h"
#include "Config.h"
#include "checksumm.h"
#include "ConfigValue.h"
#include "Gcode.h"
#include "Pin.h" // Native Pin implementation used here
#include <string.h>
#include "modules/robot/Conveyor.h"

#define can_bridge_module_checksum       CHECKSUM("can_bridge")
#define can_bridge_enable_checksum       CHECKSUM("enable")
#define can_bridge_baudrate_checksum     CHECKSUM("baudrate")
#define rx_endstops_enable_checksum      CHECKSUM("rx_endstops_enable")
#define x_endstop_pin_checksum           CHECKSUM("x_endstop_pin")
#define z_endstop_pin_checksum           CHECKSUM("z_endstop_pin")
#define can_bridge_coordinate_checksum   CHECKSUM("coordinate")


volatile uint32_t CanBridge::last_x_msg_ms = 0;
volatile uint32_t CanBridge::last_z_msg_ms = 0;

CanBridge::CanBridge() {
    this->can_enable = false;
    this->can_baudrate = 1000000;
    this->x_is_failed = false;
    this->z_is_failed = false;
}

void CanBridge::on_module_loaded() {
    // Read parameters from config file
    this->can_enable   = THEKERNEL->config->value(can_bridge_module_checksum, can_bridge_enable_checksum)  ->by_default(false)  ->as_bool();
    this->can_baudrate = THEKERNEL->config->value(can_bridge_module_checksum, can_bridge_baudrate_checksum)->by_default(1000000)->as_number();

   // can_bridge_enabled = this->can_enable;

    if (!this->can_enable) {
        return; // Module stays inactive if disabled in config
    }

    // 1. Fetch the master toggle for CAN execution of endstops from config
    this->can_coordinate   = THEKERNEL->config->value(can_bridge_module_checksum, can_bridge_coordinate_checksum)->by_default(true)->as_bool();
    can_rx_endstops_enable = THEKERNEL->config->value(can_bridge_module_checksum, rx_endstops_enable_checksum)->by_default(false)->as_bool();

    if (can_rx_endstops_enable) {
        // 2. Parse X Endstop pin string and extract hardware memory maps
        Pin x_pin;
        x_pin.from_string(THEKERNEL->config->value(can_bridge_module_checksum, x_endstop_pin_checksum)->by_default("nc")->as_string());

        // Native check if the pin is connected using valid flag
        if (x_pin.connected()) {
            x_pin.as_output(); // Hardware initialize the pin as a clean Push-Pull output
            x_pin.set(0);      // Force initial LOW state

            // Extract pure hardware properties from the parsed Pin object
            uint8_t pin_idx  = x_pin.pin;         // Pin index (0 to 15)

            // Assign the raw memory pointer to the GPIO port base address
            can_endstop_x.gpio_port = x_pin.port;

            // Apply inversion logic at startup (Zero runtime latency)
            if (x_pin.is_inverting()) {
                can_endstop_x.set_mask  = (1UL << (pin_idx + 16)); // CAN 0x11 drives output LOW (BSRR Reset)
                can_endstop_x.rst_mask  = (1UL << pin_idx);        // CAN 0x10 drives output HIGH (BSRR Set)
            } else {
                can_endstop_x.set_mask  = (1UL << pin_idx);        // CAN 0x11 drives output HIGH (BSRR Set)
                can_endstop_x.rst_mask  = (1UL << (pin_idx + 16)); // CAN 0x10 drives output LOW (BSRR Reset)
            }
        }

        // 3. Parse Z Endstop pin string and extract hardware memory maps
        Pin z_pin;
        z_pin.from_string(THEKERNEL->config->value(can_bridge_module_checksum, z_endstop_pin_checksum)->by_default("nc")->as_string());

        if (z_pin.connected()) {
            z_pin.as_output();
            z_pin.set(0);

            uint8_t pin_idx  = z_pin.pin;

            can_endstop_z.gpio_port = z_pin.port;

            if (z_pin.is_inverting()) {
                can_endstop_z.set_mask  = (1UL << (pin_idx + 16));
                can_endstop_z.rst_mask  = (1UL << pin_idx);
            } else {
                can_endstop_z.set_mask  = (1UL << pin_idx);
                can_endstop_z.rst_mask  = (1UL << (pin_idx + 16));
            }
        }
    }

    // Initialize low-level bxCAN hardware via our API
    if (can_hardware_init(this->can_baudrate)) {
        // Subscribe to incoming G-code events
        this->register_for_event(ON_GCODE_RECEIVED);

        if (can_rx_endstops_enable)
        {
          this->register_for_event(ON_SECOND_TICK);
        }
    }
}


void CanBridge::on_gcode_received(void* argument) {
    Gcode* gcode = static_cast<Gcode*>(argument);

    // Intercept custom  G-code: M777
    if (gcode->has_m && gcode->m == 777) {

    	if (this->can_coordinate)
    	{
    	  THEKERNEL->conveyor->wait_for_idle();
    	}

        can_bridge_msg_t msg;
        memset(&msg, 0, sizeof(can_bridge_msg_t));

        // Target CAN ID from 'P' parameter, defaults to 0 if omitted
        msg.id = gcode->has_letter('P') ? gcode->get_uint('P') : 0;

        // Fixed frame payload size: strictly 2 bytes
        msg.len = 2;

        // Byte 0: Actuator/Device index from 'N' parameter (supports 0-255)
        if (gcode->has_letter('N')) {
            msg.data[0] = (uint8_t)gcode->get_uint('N');
        }

        // Byte 1: Raw command value or state from 'S' parameter (supports 0-255)
        if (gcode->has_letter('S')) {
            msg.data[1] = (uint8_t)gcode->get_uint('S');
        }

        // Asynchronously push the frame into the CAN hardware transmit mailbox
        can_hardware_write(&msg);
    }
}


/*
 * ============================================================================
 * CAN-BUS ENDSTOP WATCHDOG & FAILSAFE ENGINE
 * ============================================================================
 * Purpose:
 * Implements a strict, hardware-grounded O(1) timeout monitor for remote CAN
 * endstops. Runs within the low-priority system ticker to evaluate bus health
 * without injecting runtime latency into the core motion planner.
 *
 * Failsafe Mechanics:
 * - Isolated Monitoring: Independently tracks X and Z axes communication lag.
 * - Atomic State Injection: Immediately forces an atomic BSRR bitmask update
 *   to simulate a closed/triggered endstop status if the bus link is severed.
 * - Reversible Error State: Automatically releases the forced safety state
 *   and restores seamless hardware ISR processing as soon as a single live
 *   CAN frame hits the mailbox.
 */

// Maximum allowable communication lag before triggering a hardware link fault [ms]
#define CAN_TIMEOUT_MS                   1500



void CanBridge::on_second_tick(void* argument) {
    if (!can_rx_endstops_enable) return;

    // Increment communication lag counters (this event triggers strictly once per 1000ms)
    last_x_msg_ms += 1000;
    last_z_msg_ms += 1000;

    // === X-AXIS LINK MONITORING ===
    if (last_x_msg_ms >= CAN_TIMEOUT_MS) {
        // Assert hardware failsafe exactly once upon detecting link loss
        if (!x_is_failed) {
            if (can_endstop_x.gpio_port != nullptr) {
                // Force target GPIO pin to CLOSED/TRIGGERED state via atomic register write
                can_endstop_x.gpio_port->BSRR = can_endstop_x.set_mask;
            }
            x_is_failed = true; // Lock the axis into local fault state
        }
        last_x_msg_ms = CAN_TIMEOUT_MS; // Clamp counter to prevent arithmetic overflow
    }
    else {
        // Bus is alive (RX ISR is active and clearing the lag counter below 1500ms)
        // If the fault flag is still active, it means the connection has JUST recovered
        if (x_is_failed) {
            if (can_endstop_x.gpio_port != nullptr) {
                // Safely release the forced endstop state back to normal operating mode
                can_endstop_x.gpio_port->BSRR = can_endstop_x.rst_mask;
            }
            x_is_failed = false; // Clear fault flag and return control to the raw RX ISR
        }
    }

    // === Z-AXIS LINK MONITORING ===
    if (last_z_msg_ms >= CAN_TIMEOUT_MS) {
        if (!z_is_failed) {
            if (can_endstop_z.gpio_port != nullptr) {
                can_endstop_z.gpio_port->BSRR = can_endstop_z.set_mask;
            }
            z_is_failed = true;
        }
        last_z_msg_ms = CAN_TIMEOUT_MS;
    }
    else {
        if (z_is_failed) {
            if (can_endstop_z.gpio_port != nullptr) {
                can_endstop_z.gpio_port->BSRR = can_endstop_z.rst_mask;
            }
            z_is_failed = false;
        }
    }
}

#endif
