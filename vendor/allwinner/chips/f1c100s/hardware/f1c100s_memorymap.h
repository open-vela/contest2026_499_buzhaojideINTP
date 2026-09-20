/****************************************************************************
 * vendor/allwinner/chips/f1c100s/hardware/f1c100s_memorymap.h
 *
 * F1C100s memory map definitions.
 ****************************************************************************/

#ifndef __VENDOR_ALLWINNER_CHIPS_F1C100S_HARDWARE_F1C100S_MEMORYMAP_H
#define __VENDOR_ALLWINNER_CHIPS_F1C100S_HARDWARE_F1C100S_MEMORYMAP_H

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Memory Regions */

#define F1C_SRAM_BASE           0x00000000
#define F1C_SRAM_SIZE           (32*1024)           /* 32KB */
#define F1C_DRAM_BASE           0x80000000
#define F1C_DRAM_SIZE           (32*1024*1024)      /* 32MB */

/* System Control */

#define F1C_CCU_BASE            0x01c20000
#define F1C_INTC_BASE           0x01c20400
#define F1C_PIO_BASE            0x01c20800
#define F1C_TIMER_BASE          0x01c20c00
#define F1C_WDT_BASE            0x01c20ca0
#define F1C_RTC_BASE            0x01c20d00
#define F1C_LRADC_BASE          0x01c23400

/* Peripheral Devices */

#define F1C_DMA_BASE            0x01c02000
#define F1C_SPI0_BASE           0x01c05000
#define F1C_SPI1_BASE           0x01c06000
#define F1C_TCON_BASE           0x01c0c000
#define F1C_TVE_BASE            0x01c0a000
#define F1C_MMC0_BASE           0x01c0f000
#define F1C_MMC1_BASE           0x01c10000
#define F1C_USB_OTG_BASE        0x01c13000
#define F1C_CODEC_BASE          0x01c23c00
#define F1C_UART0_BASE          0x01c25000
#define F1C_UART1_BASE          0x01c25400
#define F1C_UART2_BASE          0x01c25800
#define F1C_TWI0_BASE           0x01c27000
#define F1C_TWI1_BASE           0x01c27400
#define F1C_TWI2_BASE           0x01c27800

/* Display Engine */

#define F1C_DEFE_BASE           0x01e00000
#define F1C_DEBE_BASE           0x01e60000

#endif /* __VENDOR_ALLWINNER_CHIPS_F1C100S_HARDWARE_F1C100S_MEMORYMAP_H */
