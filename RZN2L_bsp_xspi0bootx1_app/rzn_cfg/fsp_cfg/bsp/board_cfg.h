/* generated configuration header file - do not edit */
#ifndef BOARD_CFG_H_
#define BOARD_CFG_H_
#include "../../../rzn/board/rzn2l_rsk/board.h"
            #define BSP_CFG_XSPI0_X1_BOOT (1)
            #ifndef APP_STANDALONE_DEBUG
            #define APP_STANDALONE_DEBUG (1)
            #endif
            #if APP_STANDALONE_DEBUG
            #define BSP_CFG_USE_LOADER_PARAMETER (1)
            #else
            #define BSP_CFG_USE_LOADER_PARAMETER (0)
            #endif
            #define BSP_CFG_CACHE_FLG (0x00000000)
            #define BSP_CFG_CS0BCR_V_WRAPCFG_V (0x00000000)
            #define BSP_CFG_CS0WCR_V_COMCFG_V (0x00000000)
            #define BSP_CFG_DUMMY0_BMCFG_V (0x00000000)
            #define BSP_CFG_BSC_FLG_xSPI_FLG (0x00000000)
            #define BSP_CFG_LDR_ADDR_NML (0x60000050)
            #define BSP_CFG_LDR_SIZE_NML (0x00006000)
            #define BSP_CFG_DEST_ADDR_NML (0x00102000)
            #define BSP_CFG_DUMMY1 (0x00000000)
            #define BSP_CFG_DUMMY2 (0x00000000)
            #define BSP_CFG_DUMMY3_CSSCTL_V (0x0000003F)
            #define BSP_CFG_DUMMY4_LIOCFGCS0_V (0x00070000)
            #define BSP_CFG_DUMMY5 (0x00000000)
            #define BSP_CFG_DUMMY6 (0x00000000)
            #define BSP_CFG_DUMMY7 (0x00000000)
            #define BSP_CFG_DUMMY8 (0x00000000)
            #define BSP_CFG_DUMMY9 (0x00000000)
            #define BSP_CFG_DUMMY10_ACCESS_SPEED (0x00000006)
            #define BSP_CFG_CHECK_SUM (0xE0AC)
#endif /* BOARD_CFG_H_ */
