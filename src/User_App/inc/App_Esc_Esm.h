/**
 * @file    App_Esc_Esm.h
 * @brief   EtherCAT State Machine (ESM) handling interface.
 * @author  D0cC
 * @date    2026-09-11
 * @version V1.0.0
 *
 * @note    Implements the INIT / PRE-OP / SAFE-OP / OP handshake between the
 *          master (which writes AL Control) and this slave (which answers in
 *          AL Status). The step function is non-blocking so it can run in the
 *          project's cooperative task slots.
 */

#ifndef APP_ESC_ESM_H
#define APP_ESC_ESM_H

#include <stdint.h>
#include "App_Esc_Reg.h"
#include "Mcal_Esc_Reg.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ESC states, matching the AL Status state field */
typedef enum
{
    Esc_State_Init    = 0x01,
    Esc_State_PreOp   = 0x02,
    Esc_State_Boot    = 0x03,
    Esc_State_SafeOp  = 0x04,
    Esc_State_Op      = 0x08
} Esc_State_t;

/* Application hooks invoked on each transition; any member may be NULL */
typedef struct
{
    void (*OnEnterPreOp)(void);
    void (*OnEnterSafeOp)(void);
    void (*OnEnterOp)(void);
    void (*OnLeaveOp)(void);
    void (*OnOutputsInvalid)(void);

    /* Reports whether this application requires a running distributed clock.
       SAFE-OP -> OP is refused with 0x0032 only when this hook is present and
       returns 1, so a master that does not use DC is never blocked. When the
       hook is NULL, distributed clocks are treated as not required. */
    uint8_t (*IsDcRequired)(void);
} Esc_Esm_Hooks_t;

void         App_Esc_Esm_Init(const Esc_Esm_Hooks_t *Hooks);
void         App_Esc_Esm_Step(void);
Esc_State_t  App_Esc_Esm_GetState(void);
void         App_Esc_Esm_RequestState(Esc_State_t State);
void         App_Esc_Esm_SetError(uint16_t Code);
void         App_Esc_Esm_ClearError(void);
uint16_t     App_Esc_Esm_GetError(void);
uint8_t      App_Esc_Esm_IsOutputValid(void);

/* Configuration preconditions checked before each transition */
uint8_t Esc_Esm_CheckMailbox(void);
uint8_t Esc_Esm_CheckProcessData(void);
uint8_t Esc_Esm_CheckDcSync(void);
uint8_t Esc_Esm_CheckWatchdog(void);


#ifdef __cplusplus
}
#endif

#endif /* APP_ESC_ESM_H */
