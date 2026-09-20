/**
 * @file    Mcal_Esc_Reg.c
 * @brief   EtherCAT Slave Controller register access primitives.
 * @author  D0cC
 * @date    2026-09-11
 * @version V1.0.0
 *
 * @note    Thin volatile accessors over the memory mapped ESC register block.
 *          Keeping them in one module means the addressing assumption is stated
 *          once, so a change forced by the user manual touches a single file.
 */

#include "Mcal_Esc_Reg.h"

/**
 * @name    Mcal_Esc_Reg_Read8
 * @brief   Read one 8-bit ESC register.
 * @param   Offset: register offset relative to ESC_BASE_ADDR.
 * @retval  The register value.
 */
uint8_t Mcal_Esc_Reg_Read8(uint16_t Offset)
{
    return *(volatile uint8_t *)(ESC_BASE_ADDR + Offset);
}

/**
 * @name    Mcal_Esc_Reg_Write8
 * @brief   Write one 8-bit ESC register.
 * @param   Offset: register offset relative to ESC_BASE_ADDR.
 * @param   Value: value to write.
 * @retval  None
 */
void Mcal_Esc_Reg_Write8(uint16_t Offset, uint8_t Value)
{
    *(volatile uint8_t *)(ESC_BASE_ADDR + Offset) = Value;
}

/**
 * @name    Mcal_Esc_Reg_Read16
 * @brief   Read one 16-bit ESC register.
 * @param   Offset: register offset relative to ESC_BASE_ADDR.
 * @retval  The register value.
 */
uint16_t Mcal_Esc_Reg_Read16(uint16_t Offset)
{
    return *(volatile uint16_t *)(ESC_BASE_ADDR + Offset);
}

/**
 * @name    Mcal_Esc_Reg_Write16
 * @brief   Write one 16-bit ESC register.
 * @param   Offset: register offset relative to ESC_BASE_ADDR.
 * @param   Value: value to write.
 * @retval  None
 */
void Mcal_Esc_Reg_Write16(uint16_t Offset, uint16_t Value)
{
    *(volatile uint16_t *)(ESC_BASE_ADDR + Offset) = Value;
}

/**
 * @name    Mcal_Esc_Reg_Read32
 * @brief   Read one 32-bit ESC register, used for distributed clock counters.
 * @param   Offset: register offset relative to ESC_BASE_ADDR.
 * @retval  The register value.
 */
uint32_t Mcal_Esc_Reg_Read32(uint16_t Offset)
{
    return *(volatile uint32_t *)(ESC_BASE_ADDR + Offset);
}

/**
 * @name    Mcal_Esc_Reg_Write32
 * @brief   Write one 32-bit ESC register.
 * @param   Offset: register offset relative to ESC_BASE_ADDR.
 * @param   Value: value to write.
 * @retval  None
 */
void Mcal_Esc_Reg_Write32(uint16_t Offset, uint32_t Value)
{
    *(volatile uint32_t *)(ESC_BASE_ADDR + Offset) = Value;
}

/**
 * @name    Mcal_Esc_Reg_Read64
 * @brief   Read a 64-bit ESC counter such as the distributed clock system time.
 * @param   Offset: register offset relative to ESC_BASE_ADDR.
 * @retval  The 64-bit value, low word first as stored by the ESC.
 */
uint64_t Mcal_Esc_Reg_Read64(uint16_t Offset)
{
    uint32_t Low;
    uint32_t High;

    Low  = *(volatile uint32_t *)(ESC_BASE_ADDR + Offset);
    High = *(volatile uint32_t *)(ESC_BASE_ADDR + Offset + 4U);

    return ((uint64_t)High << 32) | (uint64_t)Low;
}

/**
 * @name    Mcal_Esc_Reg_SetBits16
 * @brief   Set the given bits of a 16-bit ESC register without disturbing the rest.
 * @param   Offset: register offset relative to ESC_BASE_ADDR.
 * @param   Mask: bits to set.
 * @retval  None
 */
void Mcal_Esc_Reg_SetBits16(uint16_t Offset, uint16_t Mask)
{
    uint16_t Value;

    Value  = *(volatile uint16_t *)(ESC_BASE_ADDR + Offset);
    Value |= Mask;
    *(volatile uint16_t *)(ESC_BASE_ADDR + Offset) = Value;
}

/**
 * @name    Mcal_Esc_Reg_ClearBits16
 * @brief   Clear the given bits of a 16-bit ESC register without disturbing the rest.
 * @param   Offset: register offset relative to ESC_BASE_ADDR.
 * @param   Mask: bits to clear.
 * @retval  None
 */
void Mcal_Esc_Reg_ClearBits16(uint16_t Offset, uint16_t Mask)
{
    uint16_t Value;

    Value  = *(volatile uint16_t *)(ESC_BASE_ADDR + Offset);
    Value &= (uint16_t)~Mask;
    *(volatile uint16_t *)(ESC_BASE_ADDR + Offset) = Value;
}
