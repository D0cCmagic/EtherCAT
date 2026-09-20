/**
 * @file    App_Cia402.c
 * @brief   CiA402 (IEC 61800-7-201) drive profile state machine implementation.
 * @author  D0cC
 * @date    2026-09-11
 * @version V1.0.0
 *
 * @note    The state machine is driven entirely by the Controlword: the master
 *          moves the drive with the predefined command patterns, and this module
 *          reports the resulting state through the Statusword. Output stage
 *          enabling is gated on Operation Enabled, which is why the profile
 *          provides two intermediate states before torque is produced.
 */

#include "App_Cia402.h"

static Cia402_State_t Cia402_State;
static Cia402_Mode_t  Cia402_Mode;
static uint16_t       Cia402_Statusword;
static uint8_t        Cia402_Fault_Reset_Prev;

static uint16_t Cia402_BuildStatusword(Cia402_State_t State);

/**
 * @name    App_Cia402_Init
 * @brief   Start the drive in Switch On Disabled with no mode selected.
 * @param   None
 * @retval  None
 */
void App_Cia402_Init(void)
{
    Cia402_State            = Cia402_State_SwitchOnDisabled;
    Cia402_Mode             = Cia402_Mode_NoMode;
    Cia402_Fault_Reset_Prev = 0U;
    Cia402_Statusword       = Cia402_BuildStatusword(Cia402_State_SwitchOnDisabled);
}

/**
 * @name    App_Cia402_Step
 * @brief   Advance the CiA402 state machine by one cycle from the given Controlword.
 * @param   Controlword: the value received in the receive PDO.
 * @retval  The resulting Statusword to transmit in the send PDO.
 */
uint16_t App_Cia402_Step(uint16_t Controlword)
{
    uint8_t Fault_Reset_Now;

    /* Fault reset acts on a rising edge of bit7 */
    Fault_Reset_Now = (uint8_t)((Controlword & CIA402_CW_FAULT_RESET) != 0U);

    switch (Cia402_State)
    {
        case Cia402_State_NotReadyToSwitchOn:
            /* Held only until the power stage reports ready; no command is valid here */
            break;

        case Cia402_State_SwitchOnDisabled:
            /* Shutdown command: voltage enabled, switch-on still blocked */
            if ((Controlword & CIA402_CMD_SHUTDOWN) == CIA402_CMD_SHUTDOWN)
            {
                Cia402_State = Cia402_State_ReadyToSwitchOn;
            }
            break;

        case Cia402_State_ReadyToSwitchOn:
            /* Disable voltage drops straight back */
            if ((Controlword & CIA402_CW_ENABLE_VOLTAGE) == 0U)
            {
                Cia402_State = Cia402_State_SwitchOnDisabled;
            }
            /* Switch on command: the power stage is armed but not modulating */
            else if ((Controlword & CIA402_CMD_SWITCH_ON) == CIA402_CMD_SWITCH_ON)
            {
                Cia402_State = Cia402_State_SwitchedOn;
            }
            else
            {
                /* Stay put */
            }
            break;

        case Cia402_State_SwitchedOn:
            if ((Controlword & CIA402_CW_ENABLE_VOLTAGE) == 0U)
            {
                Cia402_State = Cia402_State_SwitchOnDisabled;
            }
            /* Quick stop (active low): bit2 cleared means stop */
            else if ((Controlword & CIA402_CW_QUICK_STOP) == 0U)
            {
                Cia402_State = Cia402_State_QuickStopActive;
            }
            /* Enable operation: only now does the drive produce torque */
            else if ((Controlword & CIA402_CMD_ENABLE_OPERATION) == CIA402_CMD_ENABLE_OPERATION)
            {
                Cia402_State = Cia402_State_OperationEnabled;
            }
            else
            {
                /* Stay put */
            }
            break;

        case Cia402_State_OperationEnabled:
            if ((Controlword & CIA402_CW_ENABLE_VOLTAGE) == 0U)
            {
                Cia402_State = Cia402_State_SwitchOnDisabled;
            }
            else if ((Controlword & CIA402_CW_QUICK_STOP) == 0U)
            {
                Cia402_State = Cia402_State_QuickStopActive;
            }
            /* Dropping bit3 alone returns to Switched On */
            else if ((Controlword & CIA402_CW_ENABLE_OPERATION) == 0U)
            {
                Cia402_State = Cia402_State_SwitchedOn;
            }
            else
            {
                /* Stay put, torque is being produced */
            }
            break;

        case Cia402_State_QuickStopActive:
            if ((Controlword & CIA402_CW_ENABLE_VOLTAGE) == 0U)
            {
                Cia402_State = Cia402_State_SwitchOnDisabled;
            }
            /* Leaving quick stop requires re-issuing enable operation */
            else if ((Controlword & CIA402_CMD_ENABLE_OPERATION) == CIA402_CMD_ENABLE_OPERATION)
            {
                Cia402_State = Cia402_State_OperationEnabled;
            }
            else
            {
                /* Stay put */
            }
            break;

        case Cia402_State_FaultReactionActive:
            /* Automatic transition once the reaction has completed */
            Cia402_State = Cia402_State_Fault;
            break;

        case Cia402_State_Fault:
            /* Only a rising edge on fault reset clears the fault */
            if ((Fault_Reset_Now != 0U) && (Cia402_Fault_Reset_Prev == 0U))
            {
                Cia402_State = Cia402_State_SwitchOnDisabled;
            }
            break;

        default:
            Cia402_State = Cia402_State_SwitchOnDisabled;
            break;
    }

    Cia402_Fault_Reset_Prev = Fault_Reset_Now;
    Cia402_Statusword       = Cia402_BuildStatusword(Cia402_State);

    return Cia402_Statusword;
}

/**
 * @name    App_Cia402_GetStatusword
 * @brief   Return the Statusword produced by the most recent step.
 * @param   None
 * @retval  The current Statusword value.
 */
uint16_t App_Cia402_GetStatusword(void)
{
    return Cia402_Statusword;
}

/**
 * @name    App_Cia402_GetState
 * @brief   Return the drive state derived from the Statusword.
 * @param   None
 * @retval  The current CiA402 state.
 */
Cia402_State_t App_Cia402_GetState(void)
{
    return Cia402_State;
}

/**
 * @name    App_Cia402_SetFault
 * @brief   Force the drive into its fault reaction, disabling the output stage.
 * @param   None
 * @retval  None
 */
void App_Cia402_SetFault(void)
{
    Cia402_State      = Cia402_State_FaultReactionActive;
    Cia402_Statusword = Cia402_BuildStatusword(Cia402_State);
}

/**
 * @name    App_Cia402_SetMode
 * @brief   Record the mode of operation requested by the master.
 * @param   Mode: the requested mode, see Cia402_Mode_t.
 * @retval  None
 */
void App_Cia402_SetMode(Cia402_Mode_t Mode)
{
    Cia402_Mode = Mode;
}

/**
 * @name    App_Cia402_GetMode
 * @brief   Return the mode of operation currently in effect.
 * @param   None
 * @retval  The active mode, reported through object 0x6061.
 */
Cia402_Mode_t App_Cia402_GetMode(void)
{
    return Cia402_Mode;
}

/**
 * @name    App_Cia402_IsOutputEnabled
 * @brief   Report whether the drive may produce torque in the current state.
 * @param   None
 * @retval  1 only in Operation Enabled, 0 otherwise.
 */
uint8_t App_Cia402_IsOutputEnabled(void)
{
    return (uint8_t)((Cia402_State == Cia402_State_OperationEnabled) ? 1U : 0U);
}

/**
 * @name    Cia402_BuildStatusword
 * @brief   Encode a drive state into the CiA402 Statusword bit pattern.
 * @param   State: the state to report.
 * @retval  The Statusword for that state, including the voltage-enabled bit.
 */
static uint16_t Cia402_BuildStatusword(Cia402_State_t State)
{
    uint16_t Statusword;

    switch (State)
    {
        case Cia402_State_NotReadyToSwitchOn:
            Statusword = 0x0000U;
            break;

        case Cia402_State_SwitchOnDisabled:
            Statusword = CIA402_SW_SWITCH_ON_DISABLED;
            break;

        case Cia402_State_ReadyToSwitchOn:
            Statusword = CIA402_SW_READY_TO_SWITCH_ON;
            break;

        case Cia402_State_SwitchedOn:
            Statusword = (uint16_t)(CIA402_SW_READY_TO_SWITCH_ON | CIA402_SW_SWITCHED_ON);
            break;

        case Cia402_State_OperationEnabled:
            Statusword = (uint16_t)(CIA402_SW_READY_TO_SWITCH_ON | CIA402_SW_SWITCHED_ON |
                                    CIA402_SW_OPERATION_ENABLED);
            break;

        /* Quick Stop Active carries the same Statusword bits as Operation Enabled
           (bit5 = 0 distinguishes it at the profile level, not by other bits) */
        case Cia402_State_QuickStopActive:
            Statusword = (uint16_t)(CIA402_SW_READY_TO_SWITCH_ON | CIA402_SW_SWITCHED_ON |
                                    CIA402_SW_OPERATION_ENABLED);
            break;

        case Cia402_State_FaultReactionActive:
            Statusword = (uint16_t)(CIA402_SW_FAULT);
            break;

        case Cia402_State_Fault:
            Statusword = (uint16_t)(CIA402_SW_FAULT | CIA402_SW_SWITCH_ON_DISABLED);
            break;

        default:
            Statusword = 0x0000U;
            break;
    }

    /* Voltage is present in every state except Not Ready and Fault */
    if ((State != Cia402_State_NotReadyToSwitchOn) && (State != Cia402_State_Fault))
    {
        Statusword |= CIA402_SW_VOLTAGE_ENABLED;
    }

    return Statusword;
}
