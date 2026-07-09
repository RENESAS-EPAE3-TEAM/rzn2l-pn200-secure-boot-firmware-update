#include "app_manifest_abi.h"
#include "sio_char.h"
#include "hal_data.h"
#include "loader_table.h"
#include "r_xspi_qspi.h"
#include "r_spi_flash_api.h"
#include "secure_app_deploy.h"
#include "secure_app_package.h"
#include "secure_app_verify.h"
#include "secure_ssbl_config.h"
#include <stdio.h>


FSP_CPP_HEADER
void R_BSP_WarmStart(bsp_warm_start_event_t event) BSP_PLACE_IN_SECTION(".warm_start");
FSP_CPP_FOOTER


void handle_error (fsp_err_t err);
void user_uart_callback (uart_callback_args_t * p_args);

uint8_t  g_out_of_band_received[TRANSFER_LENGTH];
uint32_t volatile g_transfer_complete = 0;
uint32_t volatile g_receive_complete  = 0;
uint32_t g_out_of_band_index = 0;
//extern bsp_leds_t g_bsp_leds;
extern void bsp_copy_multibyte(uintptr_t * src, uintptr_t * dst, uintptr_t bytesize);
extern const loader_table table[TABLE_ENTRY_NUM];
extern void R_BSP_CacheCleanInvalidateAll(void);

/* Ported from rzn2l_xspi_boot: SDRAM (W9825G6KH-6) controller initialization.
 * Runs in the Loader (SSBL) so that the Application, once copied to SystemRAM
 * and started, can immediately access the external SDRAM (CS2/CS3 mirror). */
#define BSC_PROTECT_KEY     (0xa55a0000)
static void bsp_sdram_init(void);
static void bsp_qspi_quad_enable(void);

#if SSBL_CFG_DEBUG_UART_ENABLE
#define SSBL_TRACE(...) printf(__VA_ARGS__)

static void ssbl_print_verify_status(const char * label)
{
    const volatile secure_app_verify_status_t * status = secure_app_verify_last_status();

    SSBL_TRACE("[SSBL][VERIFY][%s] result=%lu stage=%lu fsp=0x%08lx\n",
               label,
               (unsigned long) status->result,
               (unsigned long) status->stage,
               (unsigned long) status->fsp_error);
    SSBL_TRACE("[SSBL][VERIFY][%s] key=0x%08lx len=0x%08lx code=0x%08lx len=0x%08lx\n",
               label,
               (unsigned long) status->key_cert_addr,
               (unsigned long) status->key_cert_size,
               (unsigned long) status->code_cert_addr,
               (unsigned long) status->code_cert_size);
}

static void ssbl_print_manifest_summary(const secure_app_manifest_t * manifest)
{
    const secure_app_manifest_header_t * header = &manifest->header;
    uint32_t segment_count = header->segment_count;

    SSBL_TRACE("[SSBL][RZSM] addr=0x%08lx magic=0x%08lx ver=0x%08lx segments=%lu\n",
               (unsigned long)(uintptr_t) manifest,
               (unsigned long) header->magic,
               (unsigned long) header->format_version,
               (unsigned long) header->segment_count);
    SSBL_TRACE("[SSBL][RZSM] package=0x%08lx payload_off=0x%08lx payload=0x%08lx entry=0x%08lx\n",
               (unsigned long) header->package_size,
               (unsigned long) header->payload_offset,
               (unsigned long) header->payload_size,
               (unsigned long) header->entry_point);
    SSBL_TRACE("[SSBL][RZSM] image_base=0x%08lx app_manifest_off=0x%08lx signed_off=0x%08lx signed_size=0x%08lx\n",
               (unsigned long) header->image_base_addr,
               (unsigned long) header->app_manifest_offset,
               (unsigned long) header->signed_region_offset,
               (unsigned long) header->signed_region_size);

    if (segment_count > SECURE_APP_MANIFEST_MAX_SEGMENTS)
    {
        segment_count = SECURE_APP_MANIFEST_MAX_SEGMENTS;
    }

    for (uint32_t i = 0u; i < segment_count; i++)
    {
        const secure_app_manifest_entry_t * entry = &manifest->entries[i];
        SSBL_TRACE("[SSBL][RZSM][SEG%lu] id=%lu flags=0x%08lx src_off=0x%08lx dst=0x%08lx file=0x%08lx mem=0x%08lx\n",
                   (unsigned long) i,
                   (unsigned long) entry->segment_id,
                   (unsigned long) entry->flags,
                   (unsigned long) entry->src_offset,
                   (unsigned long) entry->dst_addr,
                   (unsigned long) entry->file_size,
                   (unsigned long) entry->mem_size);
    }
}

static void ssbl_print_deploy_status(const char * label)
{
    const volatile secure_app_deploy_status_t * status = secure_app_deploy_last_status();

    SSBL_TRACE("[SSBL][DEPLOY][%s] result=%lu stage=%lu seg_index=%lu seg_id=%lu\n",
               label,
               (unsigned long) status->result,
               (unsigned long) status->stage,
               (unsigned long) status->segment_index,
               (unsigned long) status->segment_id);
    SSBL_TRACE("[SSBL][DEPLOY][%s] addr=0x%08lx size=0x%08lx detail=0x%08lx\n",
               label,
               (unsigned long) status->address,
               (unsigned long) status->size,
               (unsigned long) status->detail);
}
#else
#define SSBL_TRACE(...) ((void) 0)
#define ssbl_print_verify_status(label) ((void) 0)
#define ssbl_print_manifest_summary(manifest) ((void) 0)
#define ssbl_print_deploy_status(label) ((void) 0)
#endif

/*******************************************************************************************************************//**
 * main() is generated by the FSP Configuration editor and is used to generate threads if an RTOS is used.  This function
 * is called by main() when no RTOS is used.
 **********************************************************************************************************************/
void hal_entry(void)
{
  
      /* Enable interrupt. */
    __asm volatile ("cpsie i");
    
    void (*app_prg)(void);

    /*UART open*/
    fsp_err_t err = R_SCI_UART_Open(&g_uart0_ctrl, &g_uart0_cfg);
    handle_error(err);
    
    
    SSBL_TRACE("*** PN2.0.0 for rzn2l SSBL starting!!! ***\n");
    
    /* LED type structure */
//    bsp_leds_t leds = g_bsp_leds;

    /* If this board has no LEDs then trap here */
//    if (0 == leds.led_count)
//    {
//        while (1)
//        {
//            ;                          // There are no LEDs on this board
//        }
//    }

    /* Holds level to set for pins */
    bsp_io_level_t pin_level = BSP_IO_LEVEL_HIGH;

    /* Unprotect I/O port registers */
    R_BSP_PinAccessEnable();

    /* Turn on LED0: Indicate application program copy start */
    pin_level = R_BSP_PortRead(BSP_IO_REGION_SAFE, BSP_IO_PORT_18);
    R_BSP_PortWrite(BSP_IO_REGION_SAFE, BSP_IO_PORT_18, (pin_level | 1U << 2));

#if SSBL_CFG_RSIP_PACKAGE_VERIFY_ENABLE
    SSBL_TRACE("[SSBL] secure package verify start\n");
    if (SECURE_APP_VERIFY_OK != secure_app_verify_package())
    {
        ssbl_print_verify_status("FAIL");
        while (1)
        {
            ;
        }
    }
    ssbl_print_verify_status("OK");
    ssbl_print_manifest_summary(secure_app_manifest_ptr());

  #if !SSBL_CFG_SECURE_PACKAGE_DEPLOY_ENABLE
    SSBL_TRACE("[SSBL] secure package deploy disabled after verify\n");
    while (1)
    {
        ;
    }
#else
    SSBL_TRACE("[SSBL] secure package deploy start\n");
    if (SECURE_APP_DEPLOY_OK != secure_app_deploy_copy(secure_app_manifest_ptr(), bsp_copy_multibyte, &app_prg))
    {
        ssbl_print_deploy_status("FAIL");
        while (1)
        {
            ;
        }
    }
    ssbl_print_deploy_status("OK");
  #endif
#else

    /* Read the App's manifest from the well-known flash address. The App's
     * ICF places .app_manifest at APP_MANIFEST_ADDR and fills src/dst/size
     * from source-compatible App block pairs such as USER_PRG_RBLOCK/WBLOCK,
     * SYSTEM_PRG_RBLOCK/WBLOCK and NONCACHE_RBLOCK/WBLOCK. */
    const app_manifest_t * manifest = (const app_manifest_t *) APP_MANIFEST_ADDR;

    if (APP_MANIFEST_MAGIC == manifest->magic)
    {
        uint32_t count = manifest->entry_count;
        SSBL_TRACE("[SSBL][LEGACY] manifest=0x%08lx count=%lu entry=0x%08lx\n",
                   (unsigned long)(uintptr_t) manifest,
                   (unsigned long) manifest->entry_count,
                   (unsigned long) manifest->entry_point);
        if (count > APP_MANIFEST_ENTRIES)
        {
            count = APP_MANIFEST_ENTRIES;
        }

        for (uint32_t i = 0; i < count; i++)
        {
            const app_manifest_entry_t * e = &manifest->entries[i];
            if ((e->flags & APP_MANIFEST_ENTRY_FLAG_ENABLE) && (e->size != 0u))
            {
                SSBL_TRACE("[SSBL][LEGACY] copy index=%lu src=0x%08lx dst=0x%08lx size=0x%08lx\n",
                           (unsigned long) i,
                           (unsigned long) e->src,
                           (unsigned long) e->dst,
                           (unsigned long) e->size);
                bsp_copy_multibyte((uintptr_t *)(uintptr_t)e->src,
                                   (uintptr_t *)(uintptr_t)e->dst,
                                   (uintptr_t) e->size);
            }
        }

        app_prg = (void(*)(void))(uintptr_t)manifest->entry_point;
    }
    else
    {
#if SSBL_CFG_LEGACY_TABLE_FALLBACK_ENABLE
        SSBL_TRACE("[SSBL][LEGACY] manifest missing, use loader_table fallback\n");
        /* Fall back to the static loader_table (kept for backwards-compat /
         * single-image debug builds). */
        for (uint8_t table_num = 0; table_num < TABLE_ENTRY_NUM; table_num++)
        {
            if (table[table_num].enable_flag == TABLE_ENABLE)
            {
                SSBL_TRACE("[SSBL][TABLE] copy index=%u src=0x%08lx dst=0x%08lx size=0x%08lx\n",
                           (unsigned int) table_num,
                           (unsigned long)(uintptr_t) table[table_num].src,
                           (unsigned long)(uintptr_t) table[table_num].dst,
                           (unsigned long)(uintptr_t) table[table_num].size);
                bsp_copy_multibyte(table[table_num].src, table[table_num].dst, table[table_num].size);
            }
        }
        app_prg = (void(*)(void))table[0].dst;
#else
        SSBL_TRACE("[SSBL][LEGACY] manifest missing and fallback disabled\n");
        while (1)
        {
            ;
        }
#endif
    }
#endif

    /* Ensuring data-changing */
    __asm volatile("dsb");

    /* Turn on LED3: Indicate application program copy end */
    pin_level = R_BSP_PortRead(BSP_IO_REGION_SAFE, BSP_IO_PORT_17);
    R_BSP_PortWrite(BSP_IO_REGION_SAFE, BSP_IO_PORT_17, (pin_level | 1U << 3));

    /* Protect I/O port registers */
    R_BSP_PinAccessDisable();

    /* Delay */
    R_BSP_SoftwareDelay(1000, BSP_DELAY_UNITS_MILLISECONDS);

    R_BSP_CacheCleanInvalidateAll();
    __asm volatile("dsb");
    __asm volatile("isb");

    /* Jump to the application project */
    SSBL_TRACE("[SSBL] jump app=0x%08lx\n", (unsigned long)(uintptr_t) app_prg);
    app_prg();
}

/*******************************************************************************************************************//**
 * This function is called at various points during the startup process.  This implementation uses the event that is
 * called right before main() to set up the pins.
 *
 * @param[in]  event    Where at in the start up process the code is currently at
 **********************************************************************************************************************/
void R_BSP_WarmStart (bsp_warm_start_event_t event)
{
    if (BSP_WARM_START_RESET == event)
    {
    	/* Pre clock initialization */
    }

    if (BSP_WARM_START_POST_C == event)
    {
        /* C runtime environment and system clocks are setup. */

#if BSP_FEATURE_DDR_SUPPORTED
  #if (1 == BSP_CFG_DDR_INIT_ENABLE)

       /* Initialize the DDR settings. */
       bsp_ddr_init();
 #endif
#endif

        if (NULL != g_bsp_pin_cfg.p_extend)
        {
            /* Configure pins. */
            R_IOPORT_Open(&IOPORT_CFG_CTRL, &IOPORT_CFG_NAME);
        }

        /* QSPI Quad-Enable bootstrap.
         * Ported from rzn2l_xspi_boot/src/hal_entry.c (POST_LOADER hook).
         * In the split design this Loader (SSBL) executes from SystemRAM and is
         * responsible for switching the external QSPI flash to 1S-4S-4S mode
         * BEFORE jumping to the Application (which runs XIP @ 0x60100000). */
        //bsp_qspi_quad_enable();  //mask for the debug uart

        /* Initialize external SDRAM so the Application can use CS2/CS3 mirror. */
        //bsp_sdram_init();
    }
}

/*******************************************************************************************************************//**
 * @brief      Switch external QSPI flash to Quad mode (1S-4S-4S, 0xEB read).
 *
 * Ported verbatim from rzn2l_xspi_boot/src/hal_entry.c POST_LOADER block.
 * Issues WEN -> WRSR(0x40) -> poll WIP=0 -> poll status&0x41==0x40 -> protocol switch.
 **********************************************************************************************************************/
#define QSPI_CMD_WRITE_ENABLE 0
#define QSPI_CMD_WRITE_STATUS 1
#define QSPI_CMD_READ_STATUS  2
static spi_flash_direct_transfer_t qspi_command[3] =
{
    {
    .command        = 0x06, /* WEN  (Write Enable)        */
    .address        = 0U,
    .data           = 0U,
    .command_length = 1U,
    .address_length = 0U,
    .data_length    = 0U,
    .dummy_cycles   = 0U
    },
    {
    .command        = 0x01, /* WRSR (Write Status Reg)    */
    .address        = 0U,
    .data           = 0x40,
    .command_length = 1U,
    .address_length = 0U,
    .data_length    = 1U,
    .dummy_cycles   = 0U
    },
    {
    .command        = 0x05, /* RDSR (Read Status Reg)     */
    .address        = 0U,
    .data           = 0U,
    .command_length = 1U,
    .address_length = 0U,
    .data_length    = 1U,
    .dummy_cycles   = 0U
    },
};

static void bsp_qspi_quad_enable (void)
{
    R_XSPI_QSPI_Open(&g_qspi_ldr_ctrl, &g_qspi_ldr_cfg);
    R_XSPI_QSPI_SpiProtocolSet(&g_qspi_ldr_ctrl, SPI_FLASH_PROTOCOL_1S_1S_1S);
    R_XSPI_QSPI_DirectTransfer(&g_qspi_ldr_ctrl, &qspi_command[QSPI_CMD_WRITE_ENABLE], SPI_FLASH_DIRECT_TRANSFER_DIR_WRITE);
    R_XSPI_QSPI_DirectTransfer(&g_qspi_ldr_ctrl, &qspi_command[QSPI_CMD_WRITE_STATUS], SPI_FLASH_DIRECT_TRANSFER_DIR_WRITE);
    do {
        R_XSPI_QSPI_DirectTransfer(&g_qspi_ldr_ctrl, &qspi_command[QSPI_CMD_READ_STATUS], SPI_FLASH_DIRECT_TRANSFER_DIR_READ);
    } while ((qspi_command[QSPI_CMD_READ_STATUS].data & 0x01) == 0x0);
    do {
        R_XSPI_QSPI_DirectTransfer(&g_qspi_ldr_ctrl, &qspi_command[QSPI_CMD_READ_STATUS], SPI_FLASH_DIRECT_TRANSFER_DIR_READ);
    } while ((qspi_command[QSPI_CMD_READ_STATUS].data & 0x41) != 0x40);
    R_XSPI_QSPI_SpiProtocolSet(&g_qspi_ldr_ctrl, SPI_FLASH_PROTOCOL_1S_4S_4S);
}

/*******************************************************************************************************************//**
 * @brief      Setup SDRAM controller (BSC + W9825G6KH-6)
 *
 * Ported verbatim from rzn2l_xspi_boot/src/hal_entry.c (SDRAM build configuration).
 * Configures CKIO @ 66.7MHz, BSC CS2/CS3 16-bit SDRAM space and issues the
 * mode-register power-on sequence.
 **********************************************************************************************************************/
static void bsp_sdram_init (void)
{
    volatile uint32_t val;

    R_RWP_S->PRCRS  = 0x0000A50F;
    R_RWP_NS->PRCRN = 0x0000A50F;

    /* NOTE: Port setting and CKIO configuration should have been done before */

    /* Configure clock frequency */
    val = R_SYSC_NS->SCKCR;
    val &= ~(7 << 16);
    val |=  (1 << 16);              /* CKIO clock: 66.7MHz */
    R_SYSC_NS->SCKCR = val;

    /* Enable BSC and CKIO module */
    val = R_SYSC_NS->MSTPCRA;
    val &= ~(1 << 0);
    R_SYSC_NS->MSTPCRA = val;
    val = R_SYSC_NS->MSTPCRA;       /* dummy read */

    val = R_SYSC_NS->MSTPCRD;
    val &= ~(1 << 11);
    R_SYSC_NS->MSTPCRD = val;
    val = R_SYSC_NS->MSTPCRD;       /* dummy read */

    R_RWP_NS->PRCRN = 0x0000A500;
    R_RWP_S->PRCRS  = 0x0000A500;

    /* Wait (CS3) */
    val = R_BSC->CSnBCR[3];
    val = R_BSC->CSnBCR[3];
    val = R_BSC->CSnBCR[3];
    val = R_BSC->CSnBCR[3];
    val = R_BSC->CSnBCR[3];

    /* SDRAM:W9825G6KH-6 on CS3. Row A0-A12, Col A0-A8 */
    val = ( 2 <<  9)    /* BSZ:  16-bit data bus */
        | ( 1 << 11)    /* Reserved */
        | ( 4 << 12)    /* TYPE: SDRAM */
        | ( 0 << 16)
        | ( 0 << 19)
        | ( 0 << 22)
        | ( 0 << 25)
        | ( 0 << 28);
    R_BSC->CSnBCR[3] = val;

    val = ( 2 <<  0)    /* WTRC */
        | ( 2 <<  3)    /* TRWL */
        | ( 1 <<  7)    /* A3CL : CAS Latency 2 */
        | ( 1 << 10)    /* WTRCD */
        | ( 1 << 13);   /* WTRP */
    R_BSC->CS3WCR_1 = val;

    /* Wait (CS2) */
    val = R_BSC->CSnBCR[2];
    val = R_BSC->CSnBCR[2];
    val = R_BSC->CSnBCR[2];
    val = R_BSC->CSnBCR[2];
    val = R_BSC->CSnBCR[2];

    /* SDRAM:W9825G6KH-6 on CS2. Row A0-A12, Col A0-A8 */
    val = ( 2 <<  9)
        | ( 1 << 11)
        | ( 4 << 12)
        | ( 0 << 16)
        | ( 0 << 19)
        | ( 0 << 22)
        | ( 0 << 25)
        | ( 0 << 28);
    R_BSC->CSnBCR[2] = val;

    val = ( 1 <<  7)    /* A3CL : CAS Latency 2 */
        | ( 1 << 10);
    R_BSC->CS2WCR_1 = val;

    /* SDRAM control: auto-refresh, auto-precharge mode, Col 9-bits, Row 13-bits */
    R_BSC->SDCR = 0x00110811;

    /* Refresh setting for SDRAM */
    R_BSC->RTCOR = BSC_PROTECT_KEY | ( 29 << 0);
    R_BSC->RTCSR = BSC_PROTECT_KEY
                 | (  0 <<  7)
                 | (  0 <<  6)
                 | (  2 <<  3)      /* Refresh timer count clock: CKIO/16 */
                 | (  0 <<  0);

    /* wait 200us */
    R_BSP_SoftwareDelay(200, BSP_DELAY_UNITS_MICROSECONDS);

    /* Power-on Sequence:
     * Set mode register of SDRAM. Needs wait 2 SDRAM clocks after set. */
    *((volatile uint16_t *)0x80212040) = 0x0000;   /* CS3 mode set */
    *((volatile uint16_t *)0x80211040) = 0x0000;   /* CS2 mode set */
}

void user_uart_callback (uart_callback_args_t * p_args)
{
    /* Handle the UART event */
    switch (p_args->event)
    {
        /* Received a character */
        case UART_EVENT_RX_CHAR:
        {
            /* Only put the next character in the receive buffer if there is space for it */
            if (sizeof(g_out_of_band_received) > g_out_of_band_index)
            {
                /* Write either the next one or two bytes depending on the receive data size */
                if ((UART_DATA_BITS_7 == g_uart0_cfg.data_bits) || (UART_DATA_BITS_8 == g_uart0_cfg.data_bits))
                {
                    g_out_of_band_received[g_out_of_band_index++] = (uint8_t) p_args->data;
                }
                else
                {
                    uint16_t * p_dest = (uint16_t *) &g_out_of_band_received[g_out_of_band_index];
                    *p_dest              = (uint16_t) p_args->data;
                    g_out_of_band_index += 2;
                }
            }
            break;
        }
        /* Receive complete */
        case UART_EVENT_RX_COMPLETE:
        {
            g_receive_complete = 1;
            break;
        }
        /* Transmit complete */
        case UART_EVENT_TX_COMPLETE:
        {
            g_transfer_complete = 1;
            break;
        }
        default:
        {
        }
    }
}

void handle_error (fsp_err_t err)
{
    FSP_PARAMETER_NOT_USED(err);
}

