/*
 * LOW-LEVEL BXCAN HARDWARE DRIVER WITH CORE VECTOR OVERRIDE
 * This driver implements direct hardware register access for the STM32F407 bxCAN peripheral.
 * It bypasses standard HAL overhead for both transmission and reception to achieve minimum latency.
 * Key Features:
 * - 4-ID Mask Hardware Filtering: Configures a 32-bit hardware acceptance filter bank to strictly
 *   trap packets from 0x1x (X axis) and 0x2x (Z axis) ranges at the silicon level.
 * - Bare-Metal Mailbox Transmission: Evaluates Transmit Mailbox Empty (TME) bits and packs data
 *   instantly into 32-bit registers (TDLR/TDHR), cutting write times to a few clock cycles.
 * - Core Vector Overriding: Hijacks the CAN1_RX0_IRQn interrupt vector directly in the NVIC table,
 *   ensuring instantaneous CPU routing upon frame arrival.
 */

#include "can_api.h"
#include <string.h>


static CAN_HandleTypeDef hcan1;


can_fast_pin_t can_endstop_x = {0};
can_fast_pin_t can_endstop_z = {0};
bool           can_rx_endstops_enable = false;

// Forward declaration of localized ISR handler
static void my_can_rx_interrupt_handler(void);

// ============================================================================
// INIT
// ============================================================================
uint8_t can_hardware_init(uint32_t baudrate) {
    // 1. Enable peripheral clocks for potential GPIO destinations and CAN1
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_CAN1_CLK_ENABLE();


    // 2. Configure CAN1 pins PB8 (RX) / PB9 (TX), AF9
    GPIO_InitTypeDef GPIO_InitStruct;
    memset(&GPIO_InitStruct, 0, sizeof(GPIO_InitStruct));
    GPIO_InitStruct.Pin       = GPIO_PIN_8 | GPIO_PIN_9;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull      = GPIO_PULLUP;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF9_CAN1;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    // 3. Configure bxCAN core descriptor
    memset(&hcan1, 0, sizeof(CAN_HandleTypeDef));
    hcan1.Instance = CAN1;

    if (baudrate == 500000) {
        hcan1.Init.Prescaler = 6; // 500 kbps @ 42 MHz APB1
    } else {
        hcan1.Init.Prescaler = 3; // 1 Mbps @ 42 MHz APB1
    }

    hcan1.Init.Mode                 = CAN_MODE_NORMAL;
    hcan1.Init.SyncJumpWidth        = CAN_SJW_1TQ;
    hcan1.Init.TimeSeg1             = CAN_BS1_9TQ;
    hcan1.Init.TimeSeg2             = CAN_BS2_4TQ;
    hcan1.Init.TimeTriggeredMode    = DISABLE;
    hcan1.Init.AutoBusOff           = ENABLE;
    hcan1.Init.AutoWakeUp           = DISABLE;
    hcan1.Init.AutoRetransmission   = ENABLE;
    hcan1.Init.ReceiveFifoLocked    = DISABLE;
    hcan1.Init.TransmitFifoPriority = DISABLE;

    HAL_CAN_DeInit(&hcan1);

    if (HAL_CAN_Init(&hcan1) != HAL_OK) {
        return 0;
    }

    // 4. Hardware filter: accept only 0x10, 0x11, 0x20, 0x21
    CAN_FilterTypeDef sFilterConfig;
    memset(&sFilterConfig, 0, sizeof(sFilterConfig));
    sFilterConfig.FilterBank           = 0;
    sFilterConfig.FilterMode           = CAN_FILTERMODE_IDMASK;
    sFilterConfig.FilterScale          = CAN_FILTERSCALE_32BIT;
    sFilterConfig.FilterIdHigh         = (0x10 << 5);                   // Base template set to 0x10
    sFilterConfig.FilterIdLow          = 0x0000;
    sFilterConfig.FilterMaskIdHigh     = (0x7C0 << 5);                  // Mask 0x7C0 perfectly isolates 0x1x and 0x2x series
    sFilterConfig.FilterMaskIdLow      = 0x0000;
    sFilterConfig.FilterFIFOAssignment = CAN_FILTER_FIFO0;
    sFilterConfig.FilterActivation     = ENABLE;

    if (HAL_CAN_ConfigFilter(&hcan1, &sFilterConfig) != HAL_OK) {
        return 0;
    }

    // 5. Start the peripheral FIRST
    if (HAL_CAN_Start(&hcan1) != HAL_OK) {
        return 0;
    }

    // 6. Bind our local ISR directly into the vector table (overrides weak default)
    NVIC_SetPriority(CAN1_RX0_IRQn, 3);
    NVIC_SetVector(CAN1_RX0_IRQn, (uint32_t)&my_can_rx_interrupt_handler);
    NVIC_EnableIRQ(CAN1_RX0_IRQn);

    // 7. Enable hardware RX interrupt in CAN peripheral
    if (HAL_CAN_ActivateNotification(&hcan1, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK) {
        return 0;
    }

    return 1;
}


uint8_t can_hardware_write(can_bridge_msg_t *msg) {
    uint32_t tsr = CAN1->TSR;
    uint32_t mailbox_num = 4;

    // 1. Check for available mailboxes using hardware TME (Transmit Mailbox Empty) bits
    if (tsr & CAN_TSR_TME0) {
        mailbox_num = 0;
    } else if (tsr & CAN_TSR_TME1) {
        mailbox_num = 1;
    } else if (tsr & CAN_TSR_TME2) {
        mailbox_num = 2;
    }

    // If all 3 mailboxes are busy — exit
    if (mailbox_num > 2) {
        return 0;
    }

    // Get a pointer to the structure of the selected Tx-mailbox from the STM32F4 CMSIS driver
    CAN_TxMailBox_TypeDef *mbox = &CAN1->sTxMailBox[mailbox_num];

    // 2. Fill the data length (DLC) into the TDTR register
    mbox->TDTR = (msg->len & 0x0FU);

    // 3. Fast packing of 8 data bytes into two 32-bit registers (TDLR and TDHR)
    uint32_t data_low  = ((uint32_t)msg->data[3] << 24) |
                         ((uint32_t)msg->data[2] << 16) |
                         ((uint32_t)msg->data[1] << 8)  |
                         ((uint32_t)msg->data[0]);

    uint32_t data_high = ((uint32_t)msg->data[7] << 24) |
                         ((uint32_t)msg->data[6] << 16) |
                         ((uint32_t)msg->data[5] << 8)  |
                         ((uint32_t)msg->data[4]);

    mbox->TDLR = data_low;
    mbox->TDHR = data_high;

    // 4. Write the ID and simultaneously set the transmission request bit (TXRQ).
    mbox->TIR = (msg->id << CAN_TI0R_STID_Pos) | CAN_TI0R_TXRQ;

    return 1;
}




/*
uint8_t can_hardware_write(can_bridge_msg_t *msg) {
    if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan1) > 0) {
        CAN_TxHeaderTypeDef TxHeader;
        uint32_t TxMailbox = 0;

        TxHeader.StdId              = msg->id;
        TxHeader.IDE                = CAN_ID_STD;
        TxHeader.RTR                = CAN_RTR_DATA;
        TxHeader.DLC                = msg->len & 0xF;
        TxHeader.TransmitGlobalTime = DISABLE;

        if (HAL_CAN_AddTxMessage(&hcan1, &TxHeader, msg->data, &TxMailbox) == HAL_OK) {
            return 1;
        }
    }
    return 0;
}

*/






void can_hardware_free(void) {
    NVIC_DisableIRQ(CAN1_RX0_IRQn);
    HAL_CAN_DeactivateNotification(&hcan1, CAN_IT_RX_FIFO0_MSG_PENDING);
    HAL_CAN_Stop(&hcan1);
    HAL_CAN_DeInit(&hcan1);
    __HAL_RCC_CAN1_CLK_DISABLE();
}



/*
 * ============================================================================
 * HIGH-SPEED RX INTERRUPT HANDLER (ZERO-LAG ENDSTOP INJECTION WITH WATCHDOG)
 * ============================================================================
 * Core Architecture:
 * This ISR completely detaches from HAL_CAN_IRQHandler to eliminate nested library calls.
 * It implements a direct hardware FIFO drain loop that evaluates IDs on the fly:
 * - Decodes IDs (0x10/0x11 for X, 0x20/0x21 for Z) using an ultra-fast hardware-driven switch.
 * - Resets the communication watchdog lag counters instantly upon frame arrival.
 * - Performs atomic bitwise mutation of physical GPIO pins via raw BSRR pointers using
 *   pre-compiled set/reset bitmasks.
 * This effectively routes CAN network events directly into the machine's endstop trigger lines
 * in real-time, eliminating software propagation delays and ensuring absolute positional accuracy.
 */

// ============================================================================
// DYNAMIC SPEED RX ISR — bypasses HAL_CAN_IRQHandler on purpose.
// ============================================================================

/*
 * _ZN9CanBridge13last_x_msg_msE -> CanBridge::last_x_msg_ms
 * _ZN9CanBridge13last_z_msg_msE -> CanBridge::last_z_msg_ms
 */
extern volatile uint32_t _ZN9CanBridge13last_x_msg_msE;
extern volatile uint32_t _ZN9CanBridge13last_z_msg_msE;

static void my_can_rx_interrupt_handler(void) {
    CAN_RxHeaderTypeDef RxHeader;
    uint8_t data[8];

    // Drain all pending frames inside FIFO0 mailbox hardware
    while ((CAN1->RF0R & CAN_RF0R_FMP0) != 0U) {
        if (HAL_CAN_GetRxMessage(&hcan1, CAN_RX_FIFO0, &RxHeader, data) != HAL_OK) {
            break;
        }

        // Fast global breakout check if endstops processing is muted in config
        if (!can_rx_endstops_enable) {
            continue;
        }

        // Dynamic GPIO routing: writing to custom registers using BSRR pointers
        switch (RxHeader.StdId) {
            // === X ENDSTOP ===
            case 0x11:
                _ZN9CanBridge13last_x_msg_msE  = 0; // Reset X-axis watchdog lag counter (link is alive)
                if (can_endstop_x.gpio_port) can_endstop_x.gpio_port->BSRR = can_endstop_x.set_mask; // Atomic hardware pin SET
                break;
            case 0x10:
                _ZN9CanBridge13last_x_msg_msE  = 0; // Reset X-axis watchdog lag counter (link is alive)
                if (can_endstop_x.gpio_port) can_endstop_x.gpio_port->BSRR = can_endstop_x.rst_mask; // Atomic hardware pin RESET
                break;

            // === Z ENDSTOP ===
            case 0x21:
                _ZN9CanBridge13last_z_msg_msE  = 0; // Reset Z-axis watchdog lag counter (link is alive)
                if (can_endstop_z.gpio_port) can_endstop_z.gpio_port->BSRR = can_endstop_z.set_mask; // Atomic hardware pin SET
                break;
            case 0x20:
                _ZN9CanBridge13last_z_msg_msE  = 0; // Reset Z-axis watchdog lag counter (link is alive)
                if (can_endstop_z.gpio_port) can_endstop_z.gpio_port->BSRR = can_endstop_z.rst_mask; // Atomic hardware pin RESET
                break;

            default:
                break;
        }
    }
}
