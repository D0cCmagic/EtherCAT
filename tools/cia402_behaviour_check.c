/* Throwaway behaviour check for App_Cia402 - not part of the Keil build.
 * Exercises the canonical enable sequence and the fault path. */
#include "App_Cia402.h"

extern int  printf(const char *fmt, ...);
extern void abort(void);

static void Expect(int Cond, const char *What)
{
    if (Cond == 0)
    {
        printf("FAIL: %s\r\n", What);
        abort();
    }
}

void Esc_Cia402_Behaviour_Check(void)
{
    uint16_t Sw;

    App_Cia402_Init();

    /* Power-up state must be Switch On Disabled */
    Sw = App_Cia402_GetStatusword();
    Expect(App_Cia402_GetState() == Cia402_State_SwitchOnDisabled, "init state");
    Expect((Sw & CIA402_SW_SWITCH_ON_DISABLED) != 0U, "init bit6");
    Expect(App_Cia402_IsOutputEnabled() == 0U, "init output disabled");

    /* Shutdown -> Ready To Switch On */
    Sw = App_Cia402_Step(CIA402_CMD_SHUTDOWN);
    Expect(App_Cia402_GetState() == Cia402_State_ReadyToSwitchOn, "shutdown state");
    Expect((Sw & CIA402_SW_READY_TO_SWITCH_ON) != 0U, "shutdown bit0");
    Expect(App_Cia402_IsOutputEnabled() == 0U, "shutdown output disabled");

    /* Switch On -> Switched On */
    Sw = App_Cia402_Step(CIA402_CMD_SWITCH_ON);
    Expect(App_Cia402_GetState() == Cia402_State_SwitchedOn, "switch on state");
    Expect((Sw & CIA402_SW_SWITCHED_ON) != 0U, "switch on bit1");
    Expect(App_Cia402_IsOutputEnabled() == 0U, "switch on output disabled");

    /* Enable Operation -> Operation Enabled, output now live */
    Sw = App_Cia402_Step(CIA402_CMD_ENABLE_OPERATION);
    Expect(App_Cia402_GetState() == Cia402_State_OperationEnabled, "enable state");
    Expect((Sw & CIA402_SW_OPERATION_ENABLED) != 0U, "enable bit2");
    Expect(App_Cia402_IsOutputEnabled() == 1U, "enable output LIVE");

    /* Dropping bit3 alone must fall back to Switched On */
    Sw = App_Cia402_Step(CIA402_CMD_SWITCH_ON);
    Expect(App_Cia402_GetState() == Cia402_State_SwitchedOn, "drop bit3");
    Expect(App_Cia402_IsOutputEnabled() == 0U, "drop bit3 output off");

    /* Quick stop is ACTIVE LOW: 0x0002 must enter Quick Stop Active */
    (void)App_Cia402_Step(CIA402_CMD_ENABLE_OPERATION);
    Sw = App_Cia402_Step(CIA402_CMD_QUICK_STOP);
    Expect(App_Cia402_GetState() == Cia402_State_QuickStopActive, "quick stop active-low");

    /* Disable voltage (0x0000) must drop to Switch On Disabled */
    Sw = App_Cia402_Step(CIA402_CMD_DISABLE_VOLTAGE);
    Expect(App_Cia402_GetState() == Cia402_State_SwitchOnDisabled, "disable voltage");
    Expect(App_Cia402_IsOutputEnabled() == 0U, "disable voltage output off");

    /* Fault path: only a RISING edge on bit7 clears it */
    App_Cia402_SetFault();
    Expect(App_Cia402_GetState() == Cia402_State_FaultReactionActive, "fault reaction");
    Expect(App_Cia402_IsOutputEnabled() == 0U, "fault output off");

    Sw = App_Cia402_Step(0x0000U);                 /* fault reaction -> fault */
    Expect(App_Cia402_GetState() == Cia402_State_Fault, "fault entered");
    Expect((Sw & CIA402_SW_FAULT) != 0U, "fault bit3");

    /* Holding the reset bit high must NOT clear a fault (needs an edge) */
    Sw = App_Cia402_Step(CIA402_CMD_FAULT_RESET);
    Expect(App_Cia402_GetState() == Cia402_State_Fault, "fault held reset stays fault");
    (void)Sw;

    /* Release then re-assert the reset bit to produce the rising edge */
    (void)App_Cia402_Step(0x0000U);
    Sw = App_Cia402_Step(CIA402_CMD_FAULT_RESET);
    Expect(App_Cia402_GetState() == Cia402_State_SwitchOnDisabled, "fault cleared by edge");

    /* Mode handling */
    App_Cia402_SetMode(Cia402_Mode_Csp);
    Expect(App_Cia402_GetMode() == Cia402_Mode_Csp, "mode csp");
    Expect(Cia402_Mode_Csp == 8, "csp is 8");
    Expect(Cia402_Mode_Csv == 9, "csv is 9");
    Expect(Cia402_Mode_Cst == 10, "cst is 10");

    printf("CiA402 behaviour check: ALL PASS\r\n");
}
