/*
 * ============================================================================
 * HARDWARE INDEPENDENT WATCHDOG (IWDG) SYSTEM GUARD
 * ============================================================================
 * Architecture Overview:
 * This module implements a bare-metal driver for the STM32F407 Independent
 * Watchdog (IWDG). It completely replaces the short-window WWDG implementation
 * to provide a robust, long-timeout safety mechanism tailored for CNC/PnP
 * motion systems.
 *
 * Silicon-Level Reliability:
 * - Clock Isolation: The IWDG runs exclusively off the internal Low-Speed
 *   RC oscillator (LSI) operating at a nominal frequency of 32 kHz. This
 *   ensures the watchdog remains fully operational and capable of resetting
 *   the MCU even if the primary high-speed crystal (HSE) or core PLL fails.
 * - Hardware Lock: Once initialized by the HAL layer, the IWDG cannot be
 *   disabled or halted via software. It enforces a strict runtime requirement
 *   to refresh the downcounter within the user-configured window.
 */

#include "Watchdog.h"
#include "Kernel.h"
#include "stm32f4xx.h"
#include <mri.h>
#include "gpio.h"

//extern GPIO leds[];

// HAL Independent Watchdog peripheral handle descriptor
static IWDG_HandleTypeDef m_iwdg_handle;

Watchdog::Watchdog(uint32_t timeout_us, WDT_ACTION action)
{
    // 1. Bind the driver instance directly to the physical IWDG peripheral
    m_iwdg_handle.Instance = IWDG;

    // 2. Set the maximum prescaler divider to 256.
    // LSI frequency calculation: 32000 Hz / 256 = 125 downcounter ticks per second.
    m_iwdg_handle.Init.Prescaler = IWDG_PRESCALER_256;

    // 3. Compute the hardware reload value based on the requested microsecond timeout.
    // Formula: Reload Value = (Timeout in Seconds) * 125 ticks.
    float seconds = (float)timeout_us / 1000000.0F;
    uint32_t reload_value = (uint32_t)(seconds * 125.0F);

    // Enforce 12-bit hardware register boundaries (values must stay within 0 to 4095, max ~32.7s)
    if (reload_value > 4095) reload_value = 4095;
    if (reload_value < 1)    reload_value = 1;

    m_iwdg_handle.Init.Reload = reload_value;

    // 4. Arm the peripheral clock and execute the hardware initialization sequence.
    // Note: Once armed on STM32, this subsystem cannot be stopped by software.
    HAL_IWDG_Init(&m_iwdg_handle);

    // Perform initial hardware counter refresh
    feed();
}

void Watchdog::feed()
{
    // Reload the downcounter to prevent an unplanned hardware MCU reset
    HAL_IWDG_Refresh(&m_iwdg_handle);
}

void Watchdog::on_module_loaded()
{
    register_for_event(ON_IDLE);
    feed();
}

void Watchdog::on_idle(void*)
{
    feed();
}
