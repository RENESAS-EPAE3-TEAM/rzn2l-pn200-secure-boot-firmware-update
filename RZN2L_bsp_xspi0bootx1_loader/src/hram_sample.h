/*
 * hram_sample.h
 *
 *  Created on: 2023/05/26
 *      Author: 3271
 */

#ifndef HRAM_SAMPLE_H_
#define HRAM_SAMPLE_H_

#include "hal_data.h"

/* Common macro for FSP header files. There is also a corresponding FSP_FOOTER macro at the end of this file. */
FSP_HEADER

/******************************************************************************
 * Typedef definitions
 *****************************************************************************/
typedef struct {
    uint16_t cmd;
    uint32_t addr;
    uint32_t data;
    uint32_t latency;
    bool write;
} hram_transfer_t;


/******************************************************************************
 * Macro definitions
 *****************************************************************************/
#if defined(BOARD_RZT2M_RSK)
#define BOARD                        "RZT2M RSK Board"
#define XSPIn                        (1)
#define XSPIn_CS                     (0)
#define XSPIn_DSTOCS                 (R_XSPIn->INTS_b.DSTOCS0)
#define XSPIn_DSTOCS_C               (R_XSPIn->INTC_b.DSTOCS0C)
#define XSPIn_CLOCK                  (R_SYSC_NS->SCKCR_b.FSELXSPI1)
#define MDVn                         (4)
#define R_XSPIn                      ((R_XSPI0_Type *) R_XSPI1_BASE)
#define HRAM_DEVICE_START_ADDRESS    (0x48000000UL)
#elif defined(BOARD_RZN2L_RSK)
#define BOARD                        "RZN2L RSK Board"
#define XSPIn                        (0)
#define XSPIn_CS                     (1)
#define XSPIn_DSTOCS                 (R_XSPIn->INTS_b.DSTOCS1)
#define XSPIn_DSTOCS_C               (R_XSPIn->INTC_b.DSTOCS1C)
#define XSPIn_CLOCK                  (R_SYSC_NS->SCKCR_b.FSELXSPI0)
#define MDVn                         (3)
#define R_XSPIn                      ((R_XSPI0_Type *) R_XSPI0_BASE)
#define HRAM_DEVICE_START_ADDRESS    (0x44000000UL)
#elif defined(BSP_CFG_BOARD_CUSTOM)
#define BOARD                        "RZN2L Custom Board"
#define XSPIn                        (0)
#define XSPIn_CS                     (1)
#define XSPIn_DSTOCS                 (R_XSPIn->INTS_b.DSTOCS1)
#define XSPIn_DSTOCS_C               (R_XSPIn->INTC_b.DSTOCS1C)
#define XSPIn_CLOCK                  (R_SYSC_NS->SCKCR_b.FSELXSPI0)
#define MDVn                         (3)
#define R_XSPIn                      ((R_XSPI0_Type *) R_XSPI0_BASE)
#define HRAM_DEVICE_START_ADDRESS    (0x44000000UL)
#elif defined(BOARD_RZT2L_RSK)
#define BOARD                        "RZT2L RSK Board"
#define XSPIn                        (0)
#define XSPIn_CS                     (1)
#define XSPIn_DSTOCS                 (R_XSPIn->INTS_b.DSTOCS1)
#define XSPIn_DSTOCS_C               (R_XSPIn->INTC_b.DSTOCS1C)
#define XSPIn_CLOCK                  (R_SYSC_NS->SCKCR_b.FSELXSPI0)
#define MDVn                         (3)
#define R_XSPIn                      ((R_XSPI0_Type *) R_XSPI0_BASE)
#define HRAM_DEVICE_START_ADDRESS    (0x44000000UL)
#endif

#define TPAT_NUM                     (4)
#define LOOP_ITER                    (TPAT_NUM * 1)
#define HRAM_DEVICE_SIZE             (0x800000)
#define HRAM_DEVICE_READ_CMD         (0xc000)
#define HRAM_DEVICE_ID0_ADDR         (0x00000000)
#define HRAM_DEVICE_ID1_ADDR         (0x00000001)
#define HRAM_DEVICE_CFG0_ADDR        (0x01000000)
#define HRAM_DEVICE_CFG1_ADDR        (0x01000001)

/******************************************************************************
 * Variable Externs
 *****************************************************************************/

/******************************************************************************
 * Functions Prototypes
 *****************************************************************************/
fsp_err_t hram_example(void);
fsp_err_t hram_trans(hram_transfer_t trans);
fsp_err_t hram_memory_test(void);
fsp_err_t check_ints(void);
uint16_t swap16(uint16_t value);

/* Common macro for FSP header files. There is also a corresponding FSP_HEADER macro at the top of this file. */
FSP_FOOTER

#endif /* HRAM_SAMPLE_H_ */
