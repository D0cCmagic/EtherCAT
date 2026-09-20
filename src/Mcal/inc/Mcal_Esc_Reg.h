/**
 * @file    Mcal_Esc_Reg.h
 * @brief   EtherCAT Slave Controller register access primitives.
 * @author  D0cC
 * @date    2026-09-11
 * @version V1.0.0
 *
 * @note    Single point of raw register access for the whole application. Every
 *          offset is relative to ESC_BASE_ADDR (ETHERCAT_BASE = 0x400B0000),
 *          so callers pass offsets such as ESC_REG_AL_STATUS, not addresses.
 *
 * @note    Access width matters. The ESC exposes some fields as 8 bit, some as
 *          16 bit and the distributed clock counters as 32 bit. Using a wider
 *          access than a field allows will disturb neighbouring registers.
 *
 * @warning The ESC is little endian, so these helpers use native C loads and
 *          stores. Confirm on hardware that 8/16/32 bit accesses are all
 *          permitted at the offsets you use (see ETHERCAT_LEARNING_NOTES.md 11.4).
 */

#ifndef MCAL_ESC_REG_H
#define MCAL_ESC_REG_H

#include <stdint.h>
#include "Mcal_Esc_Init.h"

#ifdef __cplusplus
extern "C" {
#endif

uint8_t  Mcal_Esc_Reg_Read8(uint16_t Offset);
void     Mcal_Esc_Reg_Write8(uint16_t Offset, uint8_t Value);
uint16_t Mcal_Esc_Reg_Read16(uint16_t Offset);
void     Mcal_Esc_Reg_Write16(uint16_t Offset, uint16_t Value);
uint32_t Mcal_Esc_Reg_Read32(uint16_t Offset);
void     Mcal_Esc_Reg_Write32(uint16_t Offset, uint32_t Value);

/* Distributed clock system time is a 64 bit nanosecond counter */
uint64_t Mcal_Esc_Reg_Read64(uint16_t Offset);

/* Read-modify-write helpers for individual bits inside a wider field */
void     Mcal_Esc_Reg_SetBits16(uint16_t Offset, uint16_t Mask);
void     Mcal_Esc_Reg_ClearBits16(uint16_t Offset, uint16_t Mask);

#ifdef __cplusplus
}
#endif

#endif /* MCAL_ESC_REG_H */
