/**
 * @file    Mcal_Esc_Init.h
 * @brief   EtherCAT Slave Controller power-up driver interface for the N32H785EC.
 * @author  D0cC
 * @date    2026-09-11
 * @version V1.0.0
 *
 * @note    Targets the N32H785 using the N32H7xx standard peripheral library.
 *
 * @warning The ESC kernel clock MUST be exactly 100 MHz. The ESCSYSDIV divider
 *          chain only produces power-of-two steps, so the SYSBUSDIV source can
 *          only reach 100 MHz when the AHB9 bus clock is 100/200/400 MHz.
 *          At the project's 300 MHz system bus the PLL sources must be used
 *          instead. Confirm the PLL constraints in the N32H7xx user manual
 *          before selecting a source.
 */

#ifndef MCAL_ESC_INIT_H
#define MCAL_ESC_INIT_H

#include <stdint.h>
#include "n32h7xx.h"
#include "n32h7xx_rcc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 1. Which core owns the ESC. Only one core may enable it. */
#define ESC_CORE_M7                 (1U)
/* #define ESC_CORE_M4              (1U) */

#if (defined(ESC_CORE_M7) && defined(ESC_CORE_M4))
#error "ESC_CORE_M7 and ESC_CORE_M4 are mutually exclusive: the ESC belongs to one core only"
#endif

#if (!defined(ESC_CORE_M7) && !defined(ESC_CORE_M4))
#error "Exactly one of ESC_CORE_M7 / ESC_CORE_M4 must be defined"
#endif

/* 2. ESC kernel clock source, must resolve to exactly 100 MHz */
#define ESC_KER_CLK_SRC_FROM_CFG    ESC_KER_CLK_FROM_PLL1B
#define ESC_KER_CLK_TARGET_HZ       (100000000U)

/* 3. ESCSYSDIV divider, only meaningful when the source is SYSBUSDIV */
#define ESC_KER_CLK_SYSBUSDIV       RCC_ESCKERCLK_SYSBUSDIV1

/* Clock source identifiers accepted by Mcal_Esc_SetKerClk */
typedef enum
{
    ESC_KER_CLK_FROM_SYSBUSDIV = 0,
    ESC_KER_CLK_FROM_PLL2B,
    ESC_KER_CLK_FROM_PLL3A,
    ESC_KER_CLK_FROM_PLL3C,
    ESC_KER_CLK_FROM_PLL1B
} Esc_Ker_Clk_Src_t;

/* ESC register block base address, see n32h7xx.h: ETHERCAT_BASE */
#define ESC_BASE_ADDR               ETHERCAT_BASE

int32_t Mcal_Esc_PowerUp(void);
int32_t Mcal_Esc_SetKerClk(Esc_Ker_Clk_Src_t Src, uint32_t Divider);
void    Mcal_Esc_EnableClk(FunctionalState Cmd);
void    Mcal_Esc_Reset(void);
uint8_t Mcal_Esc_ReportClocks(void);

#ifdef __cplusplus
}
#endif

#endif /* MCAL_ESC_INIT_H */
