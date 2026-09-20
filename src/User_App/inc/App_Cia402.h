/**
 * @file    App_Cia402.h
 * @brief   CiA402 (IEC 61800-7-201) drive profile state machine interface.
 * @author  D0cC
 * @date    2026-09-11
 * @version V1.0.0
 *
 * @note    This module is independent of any ESC register layout: it maps the
 *          Controlword (0x6040) and Statusword (0x6041) bits defined by the
 *          CiA402 profile to a drive state. It therefore does not depend on the
 *          unverified ESC register map (see ETHERCAT_LEARNING_NOTES.md 11.4).
 *
 * @note    Bit assignments follow the established CiA402 profile. They should
 *          still be confirmed against IEC 61800-7-201 before certification.
 */

#ifndef APP_CIA402_H
#define APP_CIA402_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Controlword (0x6040) command bits */
#define CIA402_CW_SWITCH_ON          (0x0001U)  /* bit0 */
#define CIA402_CW_ENABLE_VOLTAGE     (0x0002U)  /* bit1 */
#define CIA402_CW_QUICK_STOP         (0x0004U)  /* bit2, ACTIVE LOW */
#define CIA402_CW_ENABLE_OPERATION   (0x0008U)  /* bit3 */
#define CIA402_CW_FAULT_RESET        (0x0080U)  /* bit7, rising edge */
#define CIA402_CW_HALT               (0x0100U)  /* bit8 */

/* Statusword (0x6041) bits */
#define CIA402_SW_READY_TO_SWITCH_ON (0x0001U)  /* bit0 */
#define CIA402_SW_SWITCHED_ON        (0x0002U)  /* bit1 */
#define CIA402_SW_OPERATION_ENABLED  (0x0004U)  /* bit2 */
#define CIA402_SW_FAULT              (0x0008U)  /* bit3 */
#define CIA402_SW_VOLTAGE_ENABLED    (0x0010U)  /* bit4 */
#define CIA402_SW_QUICK_STOP         (0x0020U)  /* bit5, active low */
#define CIA402_SW_SWITCH_ON_DISABLED (0x0040U)  /* bit6 */
#define CIA402_SW_WARNING            (0x0080U)  /* bit7 */
#define CIA402_SW_REMOTE             (0x0200U)  /* bit9 */
#define CIA402_SW_TARGET_REACHED     (0x0400U)  /* bit10 */
#define CIA402_SW_INTERNAL_LIMIT     (0x0800U)  /* bit11 */

/* Predefined Controlword commands from the profile */
#define CIA402_CMD_SHUTDOWN          (0x0006U)  /* -> Ready to switch on */
#define CIA402_CMD_SWITCH_ON         (0x0007U)  /* -> Switched on */
#define CIA402_CMD_ENABLE_OPERATION  (0x000FU)  /* -> Operation enabled */
#define CIA402_CMD_DISABLE_VOLTAGE   (0x0000U)  /* -> Switch on disabled */
#define CIA402_CMD_QUICK_STOP        (0x0002U)  /* -> Quick stop active */
#define CIA402_CMD_FAULT_RESET       (0x0080U)  /* rising edge clears fault */

/* Modes of operation (0x6060 / 0x6061) */
typedef enum
{
    Cia402_Mode_NoMode     = 0,
    Cia402_Mode_ProfilePos = 1,   /* PP  */
    Cia402_Mode_ProfileVel = 3,   /* PV  */
    Cia402_Mode_ProfileTorq = 4,  /* PT  */
    Cia402_Mode_Homing     = 6,   /* HM  */
    Cia402_Mode_InterpPos  = 7,   /* IP  */
    Cia402_Mode_Csp        = 8,   /* CSP - cyclic synchronous position */
    Cia402_Mode_Csv        = 9,   /* CSV - cyclic synchronous velocity */
    Cia402_Mode_Cst        = 10   /* CST - cyclic synchronous torque   */
} Cia402_Mode_t;

/* Drive states derived from the Statusword */
typedef enum
{
    Cia402_State_NotReadyToSwitchOn = 0,
    Cia402_State_SwitchOnDisabled,
    Cia402_State_ReadyToSwitchOn,
    Cia402_State_SwitchedOn,
    Cia402_State_OperationEnabled,
    Cia402_State_QuickStopActive,
    Cia402_State_FaultReactionActive,
    Cia402_State_Fault
} Cia402_State_t;

void           App_Cia402_Init(void);
uint16_t       App_Cia402_Step(uint16_t Controlword);
uint16_t       App_Cia402_GetStatusword(void);
Cia402_State_t App_Cia402_GetState(void);
void           App_Cia402_SetFault(void);
void           App_Cia402_SetMode(Cia402_Mode_t Mode);
Cia402_Mode_t  App_Cia402_GetMode(void);
uint8_t        App_Cia402_IsOutputEnabled(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_CIA402_H */
