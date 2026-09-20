/****************************************************************************
 * vendor/allwinner/chips/f1c100s/hardware/f1c100s_usb.h
 *
 * F1C100s USB OTG Controller Register Definitions
 * Based on Mentor Graphics MUSBMHDRC with Sunxi-specific layout
 ****************************************************************************/

#ifndef __VENDOR_ALLWINNER_CHIPS_F1C100S_HARDWARE_F1C100S_USB_H
#define __VENDOR_ALLWINNER_CHIPS_F1C100S_HARDWARE_F1C100S_USB_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* USB Controller Base Address */

#define F1C100S_USB_BASE            0x01c13000

/* CCU Registers for USB */

#define F1C100S_CCU_BASE            0x01c20000
#define F1C100S_CCU_USBPHY_CFG      (F1C100S_CCU_BASE + 0x00cc)
#define F1C100S_CCU_BUS_CLK_GATING0 (F1C100S_CCU_BASE + 0x0060)
#define F1C100S_CCU_BUS_SOFT_RST0   (F1C100S_CCU_BASE + 0x02c0)

/* System Control */

#define F1C100S_SYS_BASE            0x01c00000
#define F1C100S_SYS_CTRL1           (F1C100S_SYS_BASE + 0x0004)

/* USB Register Offsets (Sunxi MUSB Layout) */

#define USB_FIFO_OFFSET(n)          (0x0000 + ((n) << 2))  /* EP FIFOs */
#define USB_POWER_OFFSET            0x0040
#define USB_DEVCTL_OFFSET           0x0041
#define USB_EP_IDX_OFFSET           0x0042
#define USB_VEND0_OFFSET            0x0043
#define USB_EP_IS_OFFSET            0x0044  /* 32-bit */
#define USB_EP_IE_OFFSET            0x0048  /* 32-bit */
#define USB_BUS_IS_OFFSET           0x004c  /* 8-bit */
#define USB_BUS_IE_OFFSET           0x0050  /* 8-bit */
#define USB_FRAME_OFFSET            0x0054  /* 16-bit */

/* Indexed Registers (selected by EP_IDX) */

#define USB_TXMAXP_OFFSET           0x0080
#define USB_TXCSR_OFFSET            0x0082  /* CSR0 when EP_IDX=0 */
#define USB_RXMAXP_OFFSET           0x0084
#define USB_RXCSR_OFFSET            0x0086
#define USB_RXCOUNT_OFFSET          0x0088
#define USB_TXTYPE_OFFSET           0x008c
#define USB_TXINTERVAL_OFFSET       0x008d
#define USB_RXTYPE_OFFSET           0x008e
#define USB_RXINTERVAL_OFFSET       0x008f
#define USB_TXFIFOSZ_OFFSET         0x0090
#define USB_TXFIFOADDR_OFFSET       0x0092
#define USB_RXFIFOSZ_OFFSET         0x0094
#define USB_RXFIFOADDR_OFFSET       0x0096
#define USB_TXFUNCADDR_OFFSET       0x0098

/* PHY Registers */

#define USB_ISCR_OFFSET             0x0400
#define USB_PHYCTL_OFFSET           0x0404
#define USB_PHYBIST_OFFSET          0x0408
#define USB_PHYTUNE_OFFSET          0x040c

/* PHYCTL Register Bits (0x404). Bit 3 (SIDDQ) is the analog PHY
 * power-down bit for this PHY generation (REG_PHYCTL_A10 in Linux
 * mainline's drivers/phy/allwinner/phy-sun4i-usb.c, PHY_CTL_SIDDQ).
 * Distinct from bits 0/7/8-13, which the indirect addr/data/strobe
 * write protocol (see f1c100s_phy_write()) uses on this same
 * register. Must be cleared before any indirect PHY register write,
 * or the analog PHY stays powered down and D+/D- are not reliably
 * driven.
 */

#define USB_PHYCTL_SIDDQ            (1 << 3)

/* POWER Register Bits (0x40) */

#define USB_POWER_ISOUPDATE         (1 << 7)
#define USB_POWER_SOFTCONN          (1 << 6)
#define USB_POWER_HSENAB            (1 << 5)
#define USB_POWER_HSMODE            (1 << 4)  /* Read-only */
#define USB_POWER_RESET             (1 << 3)
#define USB_POWER_RESUME            (1 << 2)
#define USB_POWER_SUSPENDM          (1 << 1)
#define USB_POWER_ENSUSPEND         (1 << 0)

/* DEVCTL Register Bits (0x41) */

#define USB_DEVCTL_BDEVICE          (1 << 7)
#define USB_DEVCTL_FSDEV            (1 << 6)
#define USB_DEVCTL_LSDEV            (1 << 5)
#define USB_DEVCTL_VBUS_MASK        (3 << 3)
#define USB_DEVCTL_HOSTMODE         (1 << 2)
#define USB_DEVCTL_HOSTREQ          (1 << 1)
#define USB_DEVCTL_SESSION          (1 << 0)

/* CSR0 Register Bits (EP0, 0x82 when EP_IDX=0) */

#define USB_CSR0_FLUSHFIFO          (1 << 8)
#define USB_CSR0_SVDSETUPEND        (1 << 7)
#define USB_CSR0_SVDRXPKTRDY        (1 << 6)
#define USB_CSR0_SENDSTALL          (1 << 5)
#define USB_CSR0_SETUPEND           (1 << 4)  /* Read-only */
#define USB_CSR0_DATAEND            (1 << 3)
#define USB_CSR0_SENTSTALL          (1 << 2)  /* Read-only */
#define USB_CSR0_TXPKTRDY           (1 << 1)
#define USB_CSR0_RXPKTRDY           (1 << 0)  /* Read-only */

/* TXCSR Register Bits (EPn TX, 0x82 when EP_IDX>0) */

#define USB_TXCSR_AUTOSET           (1 << 15)
#define USB_TXCSR_ISO               (1 << 14)
#define USB_TXCSR_MODE              (1 << 13)  /* 1=TX/IN */
#define USB_TXCSR_DMAREQENAB        (1 << 12)
#define USB_TXCSR_FRCDATATOG        (1 << 11)
#define USB_TXCSR_DMAREQMODE        (1 << 10)
#define USB_TXCSR_INCOMPTX          (1 << 7)
#define USB_TXCSR_CLRDATATOG        (1 << 6)
#define USB_TXCSR_SENTSTALL         (1 << 5)
#define USB_TXCSR_SENDSTALL         (1 << 4)
#define USB_TXCSR_FLUSHFIFO         (1 << 3)
#define USB_TXCSR_UNDERRUN          (1 << 2)
#define USB_TXCSR_FIFONOTEMPTY      (1 << 1)
#define USB_TXCSR_TXPKTRDY          (1 << 0)

/* RXCSR Register Bits (EPn RX, 0x86) */

#define USB_RXCSR_AUTOCLEAR         (1 << 15)
#define USB_RXCSR_ISO               (1 << 14)
#define USB_RXCSR_DMAREQENAB        (1 << 13)
#define USB_RXCSR_DISNYET           (1 << 12)
#define USB_RXCSR_DMAREQMODE        (1 << 11)
#define USB_RXCSR_INCOMPRX          (1 << 8)
#define USB_RXCSR_CLRDATATOG        (1 << 7)
#define USB_RXCSR_SENTSTALL         (1 << 6)
#define USB_RXCSR_SENDSTALL         (1 << 5)
#define USB_RXCSR_FLUSHFIFO         (1 << 4)
#define USB_RXCSR_DATAERROR         (1 << 3)
#define USB_RXCSR_OVERRUN           (1 << 2)
#define USB_RXCSR_FIFOFULL          (1 << 1)
#define USB_RXCSR_RXPKTRDY          (1 << 0)

/* BUS_IS/BUS_IE Interrupt Bits (0x4C/0x50) */

#define USB_INT_VBUSERR             (1 << 7)
#define USB_INT_SESSREQ             (1 << 6)
#define USB_INT_DISCONNECT          (1 << 5)
#define USB_INT_CONNECT             (1 << 4)
#define USB_INT_SOF                 (1 << 3)
#define USB_INT_RESET               (1 << 2)
#define USB_INT_RESUME              (1 << 1)
#define USB_INT_SUSPEND             (1 << 0)

/* Host Mode CSR0 Bits (EP0, 0x82 when EP_IDX=0) */

#define USB_CSR0_H_NAKTIMEOUT       (1 << 7)   /* Host: NAK timeout */
#define USB_CSR0_H_STATUSPKT        (1 << 6)   /* Host: Status packet */
#define USB_CSR0_H_REQPKT           (1 << 5)   /* Host: Request IN packet */
#define USB_CSR0_H_ERROR            (1 << 4)   /* Host: Error */
#define USB_CSR0_H_SETUPPKT         (1 << 3)   /* Host: Setup packet */
#define USB_CSR0_H_RXSTALL          (1 << 2)   /* Host: STALL received */

/* Host Mode TXCSR Bits (EPn TX, 0x82 when EP_IDX>0) */

#define USB_TXCSR_H_NAKTIMEOUT      (1 << 7)   /* Host: NAK timeout */
#define USB_TXCSR_H_RXSTALL         (1 << 5)   /* Host: STALL received */
#define USB_TXCSR_H_ERROR           (1 << 2)   /* Host: Error */

/* Host Mode RXCSR Bits (EPn RX, 0x86) */

#define USB_RXCSR_H_AUTOREQ         (1 << 14)  /* Host: Auto request */
#define USB_RXCSR_H_AUTOCLEAR       (1 << 15)  /* Host: Auto clear */
#define USB_RXCSR_H_NAKTIMEOUT      (1 << 7)   /* Host: NAK timeout */
#define USB_RXCSR_H_RXSTALL         (1 << 6)   /* Host: STALL received */
#define USB_RXCSR_H_REQPKT          (1 << 5)   /* Host: Request IN packet */
#define USB_RXCSR_H_ERROR           (1 << 2)   /* Host: Error */

/* EP_IS/EP_IE Interrupt Bits (0x44/0x48) */

#define USB_EP_INT_EP0              (1 << 0)
#define USB_EP_INT_TX(n)            (1 << (n))        /* TX EP 1-15 */
#define USB_EP_INT_RX(n)            (1 << ((n) + 16)) /* RX EP 1-15 */

/* ISCR Register Bits (0x400) */

#define USB_ISCR_DPDM_PULLUP_EN     (1 << 17)
#define USB_ISCR_ID_PULLUP_EN       (1 << 16)
#define USB_ISCR_VBUS_VALID_SRC     (3 << 12)
#define USB_ISCR_FORCE_VBUS_HIGH    (3 << 12)
#define USB_ISCR_FORCE_VBUS_LOW     (2 << 12)
#define USB_ISCR_FORCE_ID_HIGH      (3 << 14)
#define USB_ISCR_FORCE_ID_LOW       (2 << 14)

/* CCU USB PHY Configuration Bits */

#define CCU_USBPHY_SCLK_GATING      (1 << 0)
#define CCU_USBPHY_PHY_RST          (1 << 1)

/* CCU Bus Clock Gating Bits */

#define CCU_BUS_CLK_USBOTG          (1 << 24)

/* CCU Bus Soft Reset Bits */

#define CCU_BUS_RST_USBOTG          (1 << 24)

/* Configuration Constants */

#define F1C100S_USB_NENDPOINTS      6     /* EP0 + 5 configurable EPs (per Linux kernel) */
#define F1C100S_EP0_MAXPACKET       64
#define F1C100S_BULK_HS_MAXPACKET   512
#define F1C100S_BULK_FS_MAXPACKET   64
#define F1C100S_INT_MAXPACKET       64
#define F1C100S_USB_IRQ             26    /* F1C_IRQ_USB_OTG */

/* FIFO Size Encoding (for TXFIFOSZ/RXFIFOSZ) */

#define USB_FIFOSZ_8                0     /* 2^3 = 8 bytes */
#define USB_FIFOSZ_16               1     /* 2^4 = 16 bytes */
#define USB_FIFOSZ_32               2     /* 2^5 = 32 bytes */
#define USB_FIFOSZ_64               3     /* 2^6 = 64 bytes */
#define USB_FIFOSZ_128              4     /* 2^7 = 128 bytes */
#define USB_FIFOSZ_256              5     /* 2^8 = 256 bytes */
#define USB_FIFOSZ_512              6     /* 2^9 = 512 bytes */
#define USB_FIFOSZ_1024             7     /* 2^10 = 1024 bytes */
#define USB_FIFOSZ_DPB              (1 << 4)  /* Double packet buffering */

/* Register Access Macros */

#define USB_GETREG8(offset)   \
  (*(volatile uint8_t *)(F1C100S_USB_BASE + (offset)))
#define USB_GETREG16(offset)  \
  (*(volatile uint16_t *)(F1C100S_USB_BASE + (offset)))
#define USB_GETREG32(offset)  \
  (*(volatile uint32_t *)(F1C100S_USB_BASE + (offset)))

#define USB_PUTREG8(val, offset)  \
  (*(volatile uint8_t *)(F1C100S_USB_BASE + (offset)) = (val))
#define USB_PUTREG16(val, offset) \
  (*(volatile uint16_t *)(F1C100S_USB_BASE + (offset)) = (val))
#define USB_PUTREG32(val, offset) \
  (*(volatile uint32_t *)(F1C100S_USB_BASE + (offset)) = (val))

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* EP0 State Machine States */

enum f1c100s_ep0state_e
{
  EP0STATE_IDLE = 0,
  EP0STATE_SETUP,
  EP0STATE_DATA_IN,
  EP0STATE_DATA_OUT,
  EP0STATE_STATUS_IN,
  EP0STATE_STATUS_OUT,
  EP0STATE_STALLED
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
extern "C"
{
#endif

#ifdef CONFIG_F1C100S_USBDEV

/****************************************************************************
 * Name: f1c100s_usbdev_initialize
 *
 * Description:
 *   Initialize the USB device controller hardware.
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int f1c100s_usbdev_initialize(void);

/****************************************************************************
 * Name: f1c100s_usbdev_uninitialize
 *
 * Description:
 *   Uninitialize the USB device controller hardware.
 *
 ****************************************************************************/

void f1c100s_usbdev_uninitialize(void);

#endif /* CONFIG_F1C100S_USBDEV */

#ifdef CONFIG_F1C100S_USBHOST

/****************************************************************************
 * Name: f1c100s_usbhost_initialize
 *
 * Description:
 *   Initialize the USB host controller hardware.
 *
 * Input Parameters:
 *   controller - USB host controller index (0 for F1C100s)
 *
 * Returned Value:
 *   A non-NULL pointer to the USB host driver instance on success;
 *   NULL on failure.
 *
 ****************************************************************************/

struct usbhost_connection_s *f1c100s_usbhost_initialize(int controller);

#endif /* CONFIG_F1C100S_USBHOST */

#ifdef __cplusplus
}
#endif

#endif /* __VENDOR_ALLWINNER_CHIPS_F1C100S_HARDWARE_F1C100S_USB_H */
