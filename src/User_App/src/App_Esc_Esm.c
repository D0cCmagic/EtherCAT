/**
 * @file    App_Esc_Esm.c
 * @brief   EtherCAT State Machine (ESM) handling implementation.
 * @author  D0cC
 * @date    2026-09-11
 * @version V1.0.2
 *
 * @note    The master requests a state by writing AL Control (0x0120); this
 *          module compares that request with AL Status (0x0130) and performs
 *          the transition, or rejects it with an AL Status Code (0x0134).
 *          Rejection is expressed as "requested state != actual state" plus a
 *          set error code, which is how the master detects a refusal.
 *
 * @note    Application hooks are supplied through an Esc_Esm_Hooks_t table so
 *          this module never introduces an unresolved external symbol.
 */

#include "App_Esc_Esm.h"

/* Module state, single instance for a single ESC */
static volatile Esc_State_t Esc_Current_State;
static volatile uint16_t    Esc_Status_Code;
static volatile uint8_t     Esc_Error_Flag;
static volatile uint8_t     Esc_Output_Valid;
static const Esc_Esm_Hooks_t *Esc_Hooks;

static uint8_t  Esc_Esm_TryTransition(Esc_State_t Target);
static uint16_t Esc_Esm_SmField(uint8_t Sm, uint16_t Field);
static uint8_t  Esc_Esm_IsDcRequired(void);
static void     Esc_Esm_CallHook(void (*Hook)(void));
static void     Esc_Esm_InvalidateOutputs(void);

/**
 * @name    App_Esc_Esm_Init
 * @brief   Reset the ESM to INIT, mark outputs invalid, and store the application hooks.
 * @param   Hooks: hook table, or NULL when no hooks are needed.
 * @retval  None
 */
void App_Esc_Esm_Init(const Esc_Esm_Hooks_t *Hooks)
{
    Esc_Hooks         = Hooks;
    Esc_Current_State = Esc_State_Init;
    Esc_Status_Code   = 0U;
    Esc_Error_Flag    = 0U;
    Esc_Output_Valid  = 0U;

    Mcal_Esc_Reg_Write16(ESC_REG_AL_STATUS, (uint16_t)Esc_State_Init);
    Mcal_Esc_Reg_Write16(ESC_REG_AL_STATUS_CODE, 0U);
}

/**
 * @name    App_Esc_Esm_Step
 * @brief   Serve one pending AL Control request; call cyclically from a task slot.
 * @param   None
 * @retval  None
 */
void App_Esc_Esm_Step(void)
{
    uint16_t    Al_Control;
    Esc_State_t Requested;

    Al_Control = Mcal_Esc_Reg_Read16(ESC_REG_AL_CONTROL);

    /* The master raises bit4 to acknowledge and clear a reported error */
    if (((Al_Control & ESC_AL_STATE_ERROR_ACK) != 0U) && (Esc_Error_Flag != 0U))
    {
        App_Esc_Esm_ClearError();
    }

    /* The SM2 watchdog fires when the master stops refreshing the outputs */
    if (Esc_Esm_CheckWatchdog() != 0U)
    {
        App_Esc_Esm_SetError(0x8001U);
    }

    Requested = (Esc_State_t)(Al_Control & ESC_AL_STATE_MASK);

    /* A request equal to the current state is an acknowledgement, not a transition */
    if (Requested == Esc_Current_State)
    {
        return;
    }

    /* While an error is pending the slave must not accept further transitions */
    if (Esc_Error_Flag != 0U)
    {
        return;
    }

    (void)Esc_Esm_TryTransition(Requested);
}

/**
 * @name    App_Esc_Esm_GetState
 * @brief   Return the state currently reported to the master.
 * @param   None
 * @retval  The current ESM state.
 */
Esc_State_t App_Esc_Esm_GetState(void)
{
    return Esc_Current_State;
}

/**
 * @name    App_Esc_Esm_RequestState
 * @brief   Have the slave itself request a state change from the master.
 * @param   State: the state this slave wants to move to.
 * @retval  None
 */
void App_Esc_Esm_RequestState(Esc_State_t State)
{
    Mcal_Esc_Reg_Write16(ESC_REG_AL_CONTROL, (uint16_t)State);
}

/**
 * @name    App_Esc_Esm_SetError
 * @brief   Latch an AL Status Code and freeze transitions until the master acknowledges.
 * @param   Code: the AL Status Code to report.
 * @retval  None
 */
void App_Esc_Esm_SetError(uint16_t Code)
{
    Esc_Status_Code = Code;
    Esc_Error_Flag  = 1U;

    Esc_Esm_InvalidateOutputs();
    Mcal_Esc_Reg_Write16(ESC_REG_AL_STATUS_CODE, Code);
}

/**
 * @name    App_Esc_Esm_ClearError
 * @brief   Drop the latched error and its code.
 * @param   None
 * @retval  None
 */
void App_Esc_Esm_ClearError(void)
{
    Esc_Status_Code = 0U;
    Esc_Error_Flag  = 0U;

    Mcal_Esc_Reg_Write16(ESC_REG_AL_STATUS_CODE, 0U);
}

/**
 * @name    App_Esc_Esm_GetError
 * @brief   Return the currently latched AL Status Code.
 * @param   None
 * @retval  The latched code, or 0 when no error is pending.
 */
uint16_t App_Esc_Esm_GetError(void)
{
    return Esc_Status_Code;
}

/**
 * @name    App_Esc_Esm_IsOutputValid
 * @brief   Report whether the master's output data may be applied to the drive.
 * @param   None
 * @retval  1 when outputs may be applied, 0 otherwise.
 */
uint8_t App_Esc_Esm_IsOutputValid(void)
{
    return Esc_Output_Valid;
}

/**
 * @name    Esc_Esm_CheckMailbox
 * @brief   Verify the mailbox sync managers are configured and activated.
 * @param   None
 * @retval  1 when both mailbox sync managers are usable, 0 otherwise.
 */
uint8_t Esc_Esm_CheckMailbox(void)
{
    if ((Esc_Esm_SmField(0U, ESC_SM_LENGTH) == 0U) || (Esc_Esm_SmField(1U, ESC_SM_LENGTH) == 0U))
    {
        return 0U;
    }
    if ((Mcal_Esc_Reg_Read8(ESC_REG_SM_BASE + (0U * ESC_REG_SM_STRIDE) + ESC_SM_ACTIVATE) == 0U) ||
        (Mcal_Esc_Reg_Read8(ESC_REG_SM_BASE + (1U * ESC_REG_SM_STRIDE) + ESC_SM_ACTIVATE) == 0U))
    {
        return 0U;
    }

    return 1U;
}

/**
 * @name    Esc_Esm_CheckProcessData
 * @brief   Verify the process data sync managers are configured and activated.
 * @param   None
 * @retval  1 when both process data sync managers are usable, 0 otherwise.
 */
uint8_t Esc_Esm_CheckProcessData(void)
{
    if ((Esc_Esm_SmField(2U, ESC_SM_LENGTH) == 0U) || (Esc_Esm_SmField(3U, ESC_SM_LENGTH) == 0U))
    {
        return 0U;
    }
    if ((Mcal_Esc_Reg_Read8(ESC_REG_SM_BASE + (2U * ESC_REG_SM_STRIDE) + ESC_SM_ACTIVATE) == 0U) ||
        (Mcal_Esc_Reg_Read8(ESC_REG_SM_BASE + (3U * ESC_REG_SM_STRIDE) + ESC_SM_ACTIVATE) == 0U))
    {
        return 0U;
    }

    return 1U;
}

/**
 * @name    Esc_Esm_CheckWatchdog
 * @brief   Report and clear whether the SM2 output watchdog has fired.
 * @param   None
 * @retval  1 when the watchdog error bit was set, 0 otherwise.
 */
uint8_t Esc_Esm_CheckWatchdog(void)
{
    uint8_t Sm2_Status;

    Sm2_Status = Mcal_Esc_Reg_Read8(ESC_REG_SM_BASE + (2U * ESC_REG_SM_STRIDE) + ESC_SM_STATUS);

    if ((Sm2_Status & ESC_SM_STATUS_WD_ERROR) == 0U)
    {
        return 0U;
    }

    /* Clear the latched flag by writing it back */
    Mcal_Esc_Reg_Write8(ESC_REG_SM_BASE + (2U * ESC_REG_SM_STRIDE) + ESC_SM_STATUS,
                   ESC_SM_STATUS_WD_ERROR);
    Esc_Esm_InvalidateOutputs();

    return 1U;
}

/**
 * @name    Esc_Esm_CheckDcSync
 * @brief   Report whether distributed clocks are enabled in the ESC feature register.
 * @param   None
 * @retval  1 when DC is enabled, 0 otherwise.
 */
uint8_t Esc_Esm_CheckDcSync(void)
{
    return (uint8_t)((Mcal_Esc_Reg_Read16(ESC_REG_FEATURES) & 0x0001U) != 0U);
}

/**
 * @name    Esc_Esm_IsDcRequired
 * @brief   Ask the application whether a running distributed clock is mandatory.
 * @param   None
 * @retval  1 when DC is required, 0 when it is not required or undeclared.
 */
static uint8_t Esc_Esm_IsDcRequired(void)
{
    if ((Esc_Hooks == 0) || (Esc_Hooks->IsDcRequired == 0))
    {
        return 0U;
    }

    return Esc_Hooks->IsDcRequired();
}

/**
 * @name    Esc_Esm_TryTransition
 * @brief   Validate and perform a transition, or reject it with an AL Status Code.
 * @param   Target: the state requested by the master.
 * @retval  1 when the transition was accepted, 0 when it was rejected.
 */
static uint8_t Esc_Esm_TryTransition(Esc_State_t Target)
{
    /* This slave implements INIT, PRE-OP, SAFE-OP and OP only */
    if ((Target != Esc_State_PreOp) && (Target != Esc_State_SafeOp) &&
        (Target != Esc_State_Op) && (Target != Esc_State_Init))
    {
        App_Esc_Esm_SetError(0x0011U);
        return 0U;
    }

    /* Entering INIT always succeeds: it tears everything down */
    if (Target == Esc_State_Init)
    {
        if (Esc_Current_State == Esc_State_Op)
        {
            Esc_Esm_CallHook((Esc_Hooks != 0) ? Esc_Hooks->OnLeaveOp : 0);
        }
        Esc_Esm_InvalidateOutputs();
        Esc_Current_State = Esc_State_Init;
        Mcal_Esc_Reg_Write16(ESC_REG_AL_STATUS, (uint16_t)Esc_State_Init);
        return 1U;
    }

    /* Reaching PRE-OP requires the mailboxes to be usable */
    if (Target == Esc_State_PreOp)
    {
        if (Esc_Esm_CheckMailbox() == 0U)
        {
            App_Esc_Esm_SetError(0x0016U);
            return 0U;
        }
        if (Esc_Current_State == Esc_State_Op)
        {
            Esc_Esm_CallHook((Esc_Hooks != 0) ? Esc_Hooks->OnLeaveOp : 0);
        }
        Esc_Esm_CallHook((Esc_Hooks != 0) ? Esc_Hooks->OnEnterPreOp : 0);
        Esc_Esm_InvalidateOutputs();
        Esc_Current_State = Esc_State_PreOp;
        Mcal_Esc_Reg_Write16(ESC_REG_AL_STATUS, (uint16_t)Esc_State_PreOp);
        return 1U;
    }

    /* SAFE-OP needs PRE-OP first, plus valid SM2/SM3 and FMMU configuration */
    if (Target == Esc_State_SafeOp)
    {
        if (Esc_Current_State != Esc_State_PreOp)
        {
            App_Esc_Esm_SetError(0x0011U);
            return 0U;
        }
        if (Esc_Esm_CheckProcessData() == 0U)
        {
            App_Esc_Esm_SetError(0x0017U);
            return 0U;
        }
        Esc_Esm_CallHook((Esc_Hooks != 0) ? Esc_Hooks->OnEnterSafeOp : 0);
        Esc_Esm_InvalidateOutputs();
        Esc_Current_State = Esc_State_SafeOp;
        Mcal_Esc_Reg_Write16(ESC_REG_AL_STATUS, (uint16_t)Esc_State_SafeOp);
        return 1U;
    }

    /* OP needs SAFE-OP first; a running DC is required only if the application
       declares it, so masters that do not use distributed clocks still reach OP */
    if (Esc_Current_State != Esc_State_SafeOp)
    {
        App_Esc_Esm_SetError(0x0011U);
        return 0U;
    }
    if (Esc_Esm_IsDcRequired() != 0U)
    {
        if (Esc_Esm_CheckDcSync() == 0U)
        {
            App_Esc_Esm_SetError(0x0032U);
            return 0U;
        }
    }

    Esc_Esm_CallHook((Esc_Hooks != 0) ? Esc_Hooks->OnEnterOp : 0);
    Esc_Output_Valid  = 1U;
    Esc_Current_State = Esc_State_Op;
    Mcal_Esc_Reg_Write16(ESC_REG_AL_STATUS, (uint16_t)Esc_State_Op);

    return 1U;
}

/**
 * @name    Esc_Esm_CallHook
 * @brief   Invoke an optional hook, tolerating a NULL table and NULL entries.
 * @param   Hook: function pointer to call, may be NULL.
 * @retval  None
 */
static void Esc_Esm_CallHook(void (*Hook)(void))
{
    if (Hook != 0)
    {
        Hook();
    }
}

/**
 * @name    Esc_Esm_InvalidateOutputs
 * @brief   Mark the master's outputs unusable and notify the application once.
 * @param   None
 * @retval  None
 */
static void Esc_Esm_InvalidateOutputs(void)
{
    if (Esc_Output_Valid != 0U)
    {
        Esc_Output_Valid = 0U;
        Esc_Esm_CallHook((Esc_Hooks != 0) ? Esc_Hooks->OnOutputsInvalid : 0);
    }
}

/**
 * @name    Esc_Esm_SmField
 * @brief   Read a 16-bit field of one sync manager configuration block.
 * @param   Sm: sync manager index, 0 to 3.
 * @param   Field: field offset inside the block, see ESC_SM_* macros.
 * @retval  The field value, or 0 when the index is out of range.
 */
static uint16_t Esc_Esm_SmField(uint8_t Sm, uint16_t Field)
{
    if (Sm > 3U)
    {
        return 0U;
    }

    return Mcal_Esc_Reg_Read16(ESC_REG_SM_BASE + ((uint16_t)Sm * ESC_REG_SM_STRIDE) + Field);
}

