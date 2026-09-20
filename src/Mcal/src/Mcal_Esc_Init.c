/**
 * @file    Mcal_Esc_Init.c
 * @brief   EtherCAT Slave Controller power-up driver for the N32H785EC.
 * @author  D0cC
 * @date    2026-09-11
 * @version V1.0.1
 *
 * @note    Brings up only the ESC clock tree and reset. ESC register access and
 *          the mailbox / PDO configuration are separate modules, because the
 *          SDK ships no ESC register definitions and the layout must come from
 *          the N32H7xx user manual plus the Beckhoff ET1100 register map.
 */

#include "Mcal_Esc_Init.h"
#include "log.h"

/**
 * @name    Mcal_Esc_PowerUp
 * @brief   Ungate the AHB9 bus and ESC clocks, reset the ESC, and select its kernel clock.
 * @param   None
 * @retval  0 on success, -1 if the configured kernel clock source is rejected.
 */
int32_t Mcal_Esc_PowerUp(void)
{
    int32_t Ret;

    /* The ESC register block sits behind the AHB9 bus clock, which is a CFG4 gate */
    RCC_EnableCFG4PeriphClk1(RCC_CFG4_PERIPHEN_AHB9BUS, ENABLE);
    Mcal_Esc_Reset();
    Mcal_Esc_EnableClk(ENABLE);

    Ret = Mcal_Esc_SetKerClk(ESC_KER_CLK_SRC_FROM_CFG, ESC_KER_CLK_SYSBUSDIV);

    return Ret;
}

/**
 * @name    Mcal_Esc_EnableClk
 * @brief   Gate or ungate the ESC clock belonging to the owning core.
 * @param   Cmd: ENABLE to ungate the ESC clock, DISABLE to gate it.
 * @retval  None
 */
void Mcal_Esc_EnableClk(FunctionalState Cmd)
{
#if defined(ESC_CORE_M7)
    RCC_EnableAHB9PeriphClk1(RCC_AHB9_PERIPHEN_M7_ESC, Cmd);
#else
    RCC_EnableAHB9PeriphClk1(RCC_AHB9_PERIPHEN_M4_ESC, Cmd);
#endif
}

/**
 * @name    Mcal_Esc_Reset
 * @brief   Assert then release the ESC peripheral reset.
 * @param   None
 * @retval  None
 */
void Mcal_Esc_Reset(void)
{
    RCC_EnableAHB9PeriphReset1(RCC_AHB9_PERIPHRST_ESC);
}

/**
 * @name    Mcal_Esc_SetKerClk
 * @brief   Select the ESC kernel clock source and, for the bus source, its divider.
 * @param   Src: kernel clock source, see Esc_Ker_Clk_Src_t.
 * @param   Divider: ESCSYSDIV value, applied only when Src selects SYSBUSDIV.
 * @retval  0 on success, -1 when Src is not a known clock source.
 */
int32_t Mcal_Esc_SetKerClk(Esc_Ker_Clk_Src_t Src, uint32_t Divider)
{
    uint32_t Clk_Src;

    switch (Src)
    {
        case ESC_KER_CLK_FROM_SYSBUSDIV:
            Clk_Src = RCC_ESCKERCLK_SRC_SYSBUSDIV;
            break;
        case ESC_KER_CLK_FROM_PLL2B:
            Clk_Src = RCC_ESCKERCLK_SRC_PLL2B;
            break;
        case ESC_KER_CLK_FROM_PLL3A:
            Clk_Src = RCC_ESCKERCLK_SRC_PLL3A;
            break;
        case ESC_KER_CLK_FROM_PLL3C:
            Clk_Src = RCC_ESCKERCLK_SRC_PLL3C;
            break;
        case ESC_KER_CLK_FROM_PLL1B:
            Clk_Src = RCC_ESCKERCLK_SRC_PLL1B;
            break;
        default:
            return -1;
    }

    /* The divider field is only defined for the bus-clocked source */
    RCC_ConfigETHERCATKerClk(Clk_Src, Divider);

    return 0;
}

/**
 * @name    Mcal_Esc_ReportClocks
 * @brief   Print the system bus and AHB9 clock frequencies for bring-up verification.
 * @param   None
 * @retval  1 when the AHB9 clock is non-zero, 0 otherwise.
 */
uint8_t Mcal_Esc_ReportClocks(void)
{
    RCC_ClocksTypeDef Clocks;

    RCC_GetClocksFreqValue(&Clocks);

    log_info("ESC bring-up: SysBus=%u Hz AHB9=%u Hz APB1=%u Hz\r\n",
             (unsigned int)Clocks.SysBusDivClkFreq,
             (unsigned int)Clocks.AHB9ClkFreq,
             (unsigned int)Clocks.APB1ClkFreq);

    return (Clocks.AHB9ClkFreq != 0U) ? 1U : 0U;
}
