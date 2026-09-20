/**
 * @file    App_Esc_Reg.h
 * @brief   EtherCAT Slave Controller register map used by the application layer.
 * @author  D0cC
 * @date    2026-09-11
 * @version V1.0.0
 *
 * @note    Offsets are relative to ESC_BASE_ADDR (see Mcal_Esc_Init.h), which is
 *          ETHERCAT_BASE = 0x400B0000 on this device.
 *
 * @warning The addresses marked [ET1100] below belong to the Beckhoff ET1100
 *          register layout, which on-chip ESC blocks follow. The entries marked
 *          [VERIFY] must be confirmed against the N32H7xx user manual before use,
 *          because the SDK ships no ESC register definitions.
 */

#ifndef APP_ESC_REG_H
#define APP_ESC_REG_H

#include <stdint.h>
#include "Mcal_Esc_Init.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 1. All entries below are offsets, not addresses. The single source of the
      absolute base is ESC_BASE_ADDR in Mcal_Esc_Init.h, reached through the
      Mcal_Esc_Reg accessors. Do not restate the base address here. */

/* --- ESC identity and features, all [ET1100] --- */
#define ESC_REG_TYPE                     (0x0000U)   /* ESC Type, 8 bit: 0x11 = ET1100 */
#define ESC_REG_REVISION                 (0x0001U)   /* ESC Revision, 8 bit */
#define ESC_REG_BUILD                    (0x0002U)   /* ESC Build, 16 bit */
#define ESC_REG_FMMU_COUNT               (0x0004U)   /* Supported FMMU count, 8 bit */
#define ESC_REG_SM_COUNT                 (0x0005U)   /* Supported SM count, 8 bit */
#define ESC_REG_RAM_SIZE                 (0x0006U)   /* Process RAM size in KB, 8 bit */
#define ESC_REG_PORT_DESC                (0x0007U)   /* Port descriptor, 8 bit */
#define ESC_REG_FEATURES                 (0x0008U)   /* Features, 16 bit (DC = bit0) */

/* --- Station address, [ET1100] --- */
#define ESC_REG_STATION_ALIAS            (0x0012U)   /* Configured station alias, 16 bit */
#define ESC_REG_STATION_ADDR             (0x0010U)   /* Configured station address, 16 bit */

/* --- AL Control / AL Status: the ESM handshake, all [ET1100] --- */
#define ESC_REG_AL_CONTROL               (0x0120U)   /* Master writes the requested state, 16 bit */
#define ESC_REG_AL_STATUS                (0x0130U)   /* Slave reports the actual state, 16 bit */
#define ESC_REG_AL_STATUS_CODE           (0x0134U)   /* Error code, 16 bit */
#define ESC_REG_PDI_CONTROL              (0x0140U)   /* PDI control register, 8 bit [VERIFY] */

/* --- Interrupt mask, [ET1100] --- */
#define ESC_REG_AL_EVENT_MASK            (0x0204U)   /* AL event request mask, 32 bit */
#define ESC_REG_AL_EVENT                 (0x0220U)   /* AL event request, 32 bit */

/* --- Watchdog, [ET1100] --- */
#define ESC_REG_WD_DIVIDER               (0x0400U)   /* Watchdog divider, 16 bit */
#define ESC_REG_WD_TIME_PDI              (0x0410U)   /* Watchdog time PDI, 16 bit */
#define ESC_REG_WD_STATUS                (0x0442U)   /* Watchdog status, 8 bit [VERIFY] */

/* --- EEPROM interface, [ET1100] --- */
#define ESC_REG_EEPROM_CONFIG            (0x0500U)   /* EEPROM configuration, 16 bit */
#define ESC_REG_EEPROM_CONTROL           (0x0502U)   /* EEPROM control/status, 16 bit */
#define ESC_REG_EEPROM_ADDR              (0x0504U)   /* EEPROM address, 32 bit */
#define ESC_REG_EEPROM_DATA              (0x0508U)   /* EEPROM data, 64 bit */

/* --- FMMU configuration: 16 bytes each, 4 FMMUs, all [ET1100] --- */
#define ESC_REG_FMMU_BASE                (0x0600U)
#define ESC_REG_FMMU_STRIDE              (0x0010U)
#define ESC_FMMU_LOGICAL_START           (0x00U)     /* 32 bit */
#define ESC_FMMU_LENGTH                  (0x04U)     /* 16 bit */
#define ESC_FMMU_LOGICAL_START_BIT       (0x06U)     /* 8 bit  */
#define ESC_FMMU_LOGICAL_STOP_BIT        (0x07U)     /* 8 bit  */
#define ESC_FMMU_PHYSICAL_START          (0x08U)     /* 16 bit */
#define ESC_FMMU_PHYSICAL_START_BIT      (0x0AU)     /* 8 bit  */
#define ESC_FMMU_TYPE                    (0x0BU)     /* 8 bit: 0 = read, 1 = write */
#define ESC_FMMU_ACTIVATE                (0x0CU)     /* 8 bit  */

/* --- SyncManager configuration: 8 bytes each, 4 SMs, all [ET1100] --- */
#define ESC_REG_SM_BASE                  (0x0800U)
#define ESC_REG_SM_STRIDE                (0x0008U)
#define ESC_SM_PHYSICAL_START            (0x00U)     /* 16 bit */
#define ESC_SM_LENGTH                    (0x02U)     /* 16 bit */
#define ESC_SM_CONTROL_BYTE              (0x04U)     /* 8 bit  */
#define ESC_SM_STATUS                    (0x05U)     /* 8 bit  */
#define ESC_SM_ACTIVATE                  (0x06U)     /* 8 bit  */
#define ESC_SM_PDI_CONTROL               (0x07U)     /* 8 bit  */

/* 2. AL Control state request values, written by the master (bits 0-3) */
#define ESC_AL_STATE_INIT                (0x01U)
#define ESC_AL_STATE_PREOP               (0x02U)
#define ESC_AL_STATE_BOOT                (0x03U)
#define ESC_AL_STATE_SAFEOP              (0x04U)
#define ESC_AL_STATE_OP                  (0x08U)
#define ESC_AL_STATE_MASK                (0x0FU)
#define ESC_AL_STATE_ERROR_ACK           (0x10U)     /* bit4: acknowledge error */

/* 3. SM Control Byte bit fields */
#define ESC_SM_CTRL_BUFFERED             (0x02U)     /* bits0-1: 10b = buffered */
#define ESC_SM_CTRL_DIR_WRITE            (0x04U)     /* bit2: 1 = master writes */
#define ESC_SM_CTRL_ECAT_IRQ             (0x20U)     /* bit4-5: 10b = IRQ on ECAT event */
#define ESC_SM_CTRL_WD_TRIGGER           (0x40U)     /* bit6: enable watchdog */

/* 4. Recommended Control Bytes (see ETHERCAT_LEARNING_NOTES.md section 10.3) */
#define ESC_SM2_CTRL_OUTPUTS             (ESC_SM_CTRL_BUFFERED | ESC_SM_CTRL_DIR_WRITE | \
                                          ESC_SM_CTRL_ECAT_IRQ | ESC_SM_CTRL_WD_TRIGGER)  /* 0x26 */
#define ESC_SM3_CTRL_INPUTS              (ESC_SM_CTRL_BUFFERED | ESC_SM_CTRL_ECAT_IRQ)      /* 0x22 */

/* 5. AL event request bits, [ET1100] */
#define ESC_AL_EVENT_AL_CONTROL          (0x00000100U)  /* bit8 : AL Control register written */
#define ESC_AL_EVENT_DC_SYNC0            (0x00000200U)  /* bit9 : DC SYNC0 */
#define ESC_AL_EVENT_DC_SYNC1            (0x00000400U)  /* bit10: DC SYNC1 */
#define ESC_AL_EVENT_SM0                 (0x00010000U)  /* bit16: SM0 event */
#define ESC_AL_EVENT_SM1                 (0x00020000U)  /* bit17: SM1 event */
#define ESC_AL_EVENT_SM2                 (0x00040000U)  /* bit18: SM2 event */
#define ESC_AL_EVENT_SM3                 (0x00080000U)  /* bit19: SM3 event */

/* 6. SM status register bits */
#define ESC_SM_STATUS_WRITE_EVENT        (0x01U)     /* bit0: write event occurred */
#define ESC_SM_STATUS_READ_EVENT         (0x02U)     /* bit1: read event occurred */
#define ESC_SM_STATUS_OPERATIONAL        (0x04U)     /* bit2: buffers with new data */
#define ESC_SM_STATUS_WD_ERROR           (0x08U)     /* bit3: watchdog triggered ★ */
#define ESC_SM_STATUS_MAILBOX_FULL       (0x10U)     /* bit4: mailbox full */

#ifdef __cplusplus
}
#endif

#endif /* APP_ESC_REG_H */
