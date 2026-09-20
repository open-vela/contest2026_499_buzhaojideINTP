/****************************************************************************
 * vendor/allwinner/chips/f1c100s/f1c100s_usbdev.c
 *
 * F1C100s USB Device Controller Driver
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/kmalloc.h>
#include <nuttx/irq.h>
#include <nuttx/spinlock.h>
#include <nuttx/usb/usb.h>
#include <nuttx/usb/usbdev.h>
#include <nuttx/usb/usbdev_trace.h>

#include "hardware/f1c100s_usb.h"

#ifdef CONFIG_F1C100S_USB_DMA
#include "f1c100s_usb_dma.h"
#endif

#ifdef CONFIG_F1C100S_USBDEV

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define F1C100S_NENDPOINTS    4   /* EP0 + EP1-EP3; EP4/EP5 bulk is dead */

/* Packet FIFO: F1C100/F1C100s datasheet 2048-byte SPRAM. Linux sunxi
 * uses ram_bits=11 and treats size as 1<<(ram_bits+2)=8KiB, which is
 * why a 5-EP * 512 layout "fits" on paper; on the wire EP4/EP5 bulk
 * then EPROTO. Budget 2048 and fail configure rather than wrap.
 */

#define F1C100S_FIFO_RAM      2048
#define F1C100S_FIFO_EP0      64

/****************************************************************************
 * USB Endpoint Resource Check
 *
 * Usable data endpoints are EP1-EP3 (on-wire bulk on EP4/EP5 is
 * EPROTO at HS and EPIPE at FS). One function only:
 *   - CDC ACM / RNDIS: 3
 *   - MSC / ADB / Fastboot: 2
 ****************************************************************************/

#define F1C100S_USB_EP_AVAILABLE  3

/* Endpoint requirements per function */

#ifdef CONFIG_F1C100S_USB_CDCACM
#  define F1C100S_USB_EP_CDCACM   3
#else
#  define F1C100S_USB_EP_CDCACM   0
#endif

#ifdef CONFIG_F1C100S_USB_MSC
#  define F1C100S_USB_EP_MSC      2
#else
#  define F1C100S_USB_EP_MSC      0
#endif

#ifdef CONFIG_F1C100S_USB_ADB
#  define F1C100S_USB_EP_ADB      2
#else
#  define F1C100S_USB_EP_ADB      0
#endif

#ifdef CONFIG_F1C100S_USB_FASTBOOT
#  define F1C100S_USB_EP_FASTBOOT 2
#else
#  define F1C100S_USB_EP_FASTBOOT 0
#endif

#ifdef CONFIG_F1C100S_USB_RNDIS
#  define F1C100S_USB_EP_RNDIS    3
#else
#  define F1C100S_USB_EP_RNDIS    0
#endif

/* Total endpoints required */

#define F1C100S_USB_EP_REQUIRED \
  (F1C100S_USB_EP_CDCACM + F1C100S_USB_EP_MSC + F1C100S_USB_EP_ADB + \
   F1C100S_USB_EP_FASTBOOT + F1C100S_USB_EP_RNDIS)

#if F1C100S_USB_EP_REQUIRED > F1C100S_USB_EP_AVAILABLE
#  error "USB endpoint count exceeded: only EP1-EP3 work. Pick one function."
#endif



/* Function count check */

#ifdef CONFIG_F1C100S_USB_CDCACM
#  define F1C100S_USB_FUNC_CDCACM 1
#else
#  define F1C100S_USB_FUNC_CDCACM 0
#endif

#ifdef CONFIG_F1C100S_USB_MSC
#  define F1C100S_USB_FUNC_MSC    1
#else
#  define F1C100S_USB_FUNC_MSC    0
#endif

#ifdef CONFIG_F1C100S_USB_ADB
#  define F1C100S_USB_FUNC_ADB    1
#else
#  define F1C100S_USB_FUNC_ADB    0
#endif

#ifdef CONFIG_F1C100S_USB_FASTBOOT
#  define F1C100S_USB_FUNC_FASTBOOT 1
#else
#  define F1C100S_USB_FUNC_FASTBOOT 0
#endif

#ifdef CONFIG_F1C100S_USB_RNDIS
#  define F1C100S_USB_FUNC_RNDIS  1
#else
#  define F1C100S_USB_FUNC_RNDIS  0
#endif

#define F1C100S_USB_FUNC_COUNT \
  (F1C100S_USB_FUNC_CDCACM + F1C100S_USB_FUNC_MSC + F1C100S_USB_FUNC_ADB + \
   F1C100S_USB_FUNC_FASTBOOT + F1C100S_USB_FUNC_RNDIS)

#if F1C100S_USB_FUNC_COUNT > 2
#  error "F1C100s USB supports maximum 2 functions in composite mode"
#endif

/* ADB and Fastboot are mutually exclusive */

#if defined(CONFIG_F1C100S_USB_ADB) && defined(CONFIG_F1C100S_USB_FASTBOOT)
#  error "ADB and Fastboot are mutually exclusive, enable only one"
#endif

/* Trace interrupt IDs */

#define F1C100S_TRACEINTID_RESET      0x0001
#define F1C100S_TRACEINTID_EP0        0x0002
#define F1C100S_TRACEINTID_EPIN       0x0003
#define F1C100S_TRACEINTID_EPOUT      0x0004
#define F1C100S_TRACEINTID_SUSPEND    0x0005
#define F1C100S_TRACEINTID_RESUME     0x0006
#define F1C100S_TRACEINTID_SETUP      0x0007
#define F1C100S_TRACEINTID_SETADDR    0x0008
#define F1C100S_TRACEINTID_GETDESC    0x0009
#define F1C100S_TRACEINTID_SETCONFIG  0x000a

/* Trace error IDs */

#define F1C100S_TRACEERR_ALLOCFAIL    0x0001
#define F1C100S_TRACEERR_BADREQUEST   0x0002
#define F1C100S_TRACEERR_EPSTALL      0x0003

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

void arm_usbinitialize(void);

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Request container */

struct f1c100s_req_s
{
  struct usbdev_req_s req;        /* Standard USB request */
  struct f1c100s_req_s *flink;    /* Next request in queue */
};

/* Endpoint state */

struct f1c100s_ep_s
{
  struct usbdev_ep_s ep;          /* Standard endpoint structure */
  struct f1c100s_usbdev_s *dev;   /* Reference to device */
  struct f1c100s_req_s *head;     /* Request queue head */
  struct f1c100s_req_s *tail;     /* Request queue tail */
  uint8_t epphy;                  /* Physical endpoint number */
  uint8_t stalled:1;              /* Endpoint is stalled */
  uint8_t txbusy:1;               /* TX in progress */
};

/* Device state */

struct f1c100s_usbdev_s
{
  struct usbdev_s usbdev;         /* Standard USB device */
  struct usbdevclass_driver_s *driver;  /* Class driver */
  struct f1c100s_ep_s eplist[F1C100S_NENDPOINTS];  /* Endpoints */
  uint8_t ep0state;               /* EP0 state machine */
  uint8_t devaddr;                /* Device address */
  bool setaddr_pending;           /* Deferred SET_ADDRESS awaiting the
                                    * status stage ACK (see
                                    * f1c100s_ep0_interrupt()) */
  uint8_t selfpowered:1;          /* Self-powered flag */
  uint8_t connected:1;            /* Connected to host */
  uint8_t highspeed:1;            /* High-speed mode */
  struct usb_ctrlreq_s ctrl __attribute__((aligned(4)));  /* Last SETUP packet.
                                    * Must be word-aligned: ARM926EJ-S runs
                                    * with SCTLR.A (alignment check) clear
                                    * (see f1c100s_head.S), so an unaligned
                                    * STR silently masks the address to
                                    * addr & ~3 instead of faulting -
                                    * without this attribute, this field's
                                    * natural offset in the struct lands on
                                    * a non-word boundary and 32-bit FIFO
                                    * writes corrupt neighboring fields
                                    * instead of writing ctrl itself. */
  uint8_t ep0data[256];           /* control OUT/IN data (Linux RNDIS QUERY is 76B) */
  uint16_t ep0datlen;             /* EP0 data length so far */
  uint16_t ep0outlen;             /* Expected control-OUT wLength */
  uint16_t fifo_next;             /* Next free FIFO addr in 8-byte units */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/* Endpoint operations */

static int  f1c100s_ep_configure(struct usbdev_ep_s *ep,
              const struct usb_epdesc_s *desc, bool last);
static int  f1c100s_ep_disable(struct usbdev_ep_s *ep);
static struct usbdev_req_s *f1c100s_ep_allocreq(struct usbdev_ep_s *ep);
static void f1c100s_ep_freereq(struct usbdev_ep_s *ep,
              struct usbdev_req_s *req);
static int  f1c100s_ep_submit(struct usbdev_ep_s *ep,
              struct usbdev_req_s *req);
static int  f1c100s_ep_cancel(struct usbdev_ep_s *ep,
              struct usbdev_req_s *req);
static int  f1c100s_ep_stall(struct usbdev_ep_s *ep, bool resume);
static void f1c100s_ep_txdata(struct f1c100s_ep_s *privep);

/* Device operations */

static struct usbdev_ep_s *f1c100s_allocep(struct usbdev_s *dev,
              uint8_t epno, bool in, uint8_t eptype);
static void f1c100s_freeep(struct usbdev_s *dev, struct usbdev_ep_s *ep);
static int  f1c100s_getframe(struct usbdev_s *dev);
static int  f1c100s_wakeup(struct usbdev_s *dev);
static int  f1c100s_selfpowered(struct usbdev_s *dev, bool selfpowered);
static int  f1c100s_pullup(struct usbdev_s *dev, bool enable);

/* Interrupt handling */

static int  f1c100s_interrupt(int irq, void *context, void *arg);
static void f1c100s_ep0_interrupt(struct f1c100s_usbdev_s *priv);
static void f1c100s_epin_interrupt(struct f1c100s_usbdev_s *priv, int epno);
static void f1c100s_epout_interrupt(struct f1c100s_usbdev_s *priv, int epno);
static void f1c100s_reset_interrupt(struct f1c100s_usbdev_s *priv);

/* EP0 control transfer helpers */

static void f1c100s_ep0_wrstatus(struct f1c100s_usbdev_s *priv);
static void f1c100s_ep0_transmit(struct f1c100s_usbdev_s *priv);
static void f1c100s_ep0_setup(struct f1c100s_usbdev_s *priv);

/* PHY and hardware init */

static void f1c100s_phy_write(uint8_t addr, uint8_t data, uint8_t len);
static void f1c100s_hw_init(struct f1c100s_usbdev_s *priv);
static void f1c100s_hw_shutdown(struct f1c100s_usbdev_s *priv);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct usbdev_epops_s g_epops =
{
  .configure = f1c100s_ep_configure,
  .disable   = f1c100s_ep_disable,
  .allocreq  = f1c100s_ep_allocreq,
  .freereq   = f1c100s_ep_freereq,
  .submit    = f1c100s_ep_submit,
  .cancel    = f1c100s_ep_cancel,
  .stall     = f1c100s_ep_stall,
};

static const struct usbdev_ops_s g_devops =
{
  .allocep     = f1c100s_allocep,
  .freeep      = f1c100s_freeep,
  .getframe    = f1c100s_getframe,
  .wakeup      = f1c100s_wakeup,
  .selfpowered = f1c100s_selfpowered,
  .pullup      = f1c100s_pullup,
};

static struct f1c100s_usbdev_s g_usbdev;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: f1c100s_read_fifo / f1c100s_write_fifo
 *
 * Description:
 *   Byte-safe FIFO transfer helpers. The FIFO register itself is a fixed,
 *   word-aligned MMIO address (USB_FIFO_OFFSET(ep)); a 32-bit access to it
 *   is always fine on the register side. The risk is the *memory* side:
 *   ARM926EJ-S runs with SCTLR.A (alignment check) clear, so casting an
 *   arbitrary uint8_t* to uint32_t* and dereferencing it silently masks
 *   the address to addr & ~3 when that pointer isn't word-aligned,
 *   corrupting whatever struct field happens to sit at the aligned
 *   address instead of the intended one. These helpers always do the
 *   32-bit register access, but assemble/split it byte-by-byte on the
 *   memory side so the caller's buffer alignment never matters.
 *
 ****************************************************************************/

static void f1c100s_read_fifo(uint8_t ep, uint8_t *dst, size_t len)
{
  size_t i = 0;

  while (i + 4 <= len)
    {
      uint32_t tmp = USB_GETREG32(USB_FIFO_OFFSET(ep));

      dst[i]     = (uint8_t)tmp;
      dst[i + 1] = (uint8_t)(tmp >> 8);
      dst[i + 2] = (uint8_t)(tmp >> 16);
      dst[i + 3] = (uint8_t)(tmp >> 24);
      i += 4;
    }

  if (i < len)
    {
      uint32_t tmp = USB_GETREG32(USB_FIFO_OFFSET(ep));

      while (i < len)
        {
          dst[i++] = (uint8_t)tmp;
          tmp >>= 8;
        }
    }
}

static void f1c100s_write_fifo(uint8_t ep, const uint8_t *src, size_t len)
{
  size_t i = 0;

  while (i + 4 <= len)
    {
      uint32_t tmp = (uint32_t)src[i] | ((uint32_t)src[i + 1] << 8) |
                     ((uint32_t)src[i + 2] << 16) |
                     ((uint32_t)src[i + 3] << 24);

      USB_PUTREG32(tmp, USB_FIFO_OFFSET(ep));
      i += 4;
    }

  /* Tail: byte writes only. A 4-byte MUSB FIFO write always advances the
   * FIFO write pointer by a full word - padding a 1-3 byte tail up to
   * 4 bytes via USB_PUTREG32 would make the packet appear longer to the
   * host than USB_CSR0_TXPKTRDY says it is (e.g. an 18-byte device
   * descriptor arriving as 20 bytes).
   */

  for (; i < len; i++)
    {
      USB_PUTREG8(src[i], USB_FIFO_OFFSET(ep));
    }
}

/****************************************************************************
 * Name: f1c100s_phy_write
 *
 * Description:
 *   Write to USB PHY using bit-banging protocol
 *
 ****************************************************************************/

static void f1c100s_phy_write(uint8_t addr, uint8_t data, uint8_t len)
{
  uint32_t phyctl;
  int i;

  for (i = 0; i < len; i++)
    {
      phyctl = USB_GETREG32(USB_PHYCTL_OFFSET);
      phyctl &= ~0xffff;
      phyctl |= ((addr + i) << 8) | (((data >> i) & 1) << 7);
      USB_PUTREG32(phyctl, USB_PHYCTL_OFFSET);

      phyctl |= 0x01;
      USB_PUTREG32(phyctl, USB_PHYCTL_OFFSET);

      phyctl &= ~0x01;
      USB_PUTREG32(phyctl, USB_PHYCTL_OFFSET);
    }
}

/****************************************************************************
 * Name: f1c100s_hw_init
 *
 * Description:
 *   Initialize USB hardware
 *
 ****************************************************************************/

static void f1c100s_hw_init(struct f1c100s_usbdev_s *priv)
{
  uint32_t reg;

  /* Enable USB PHY clock and deassert reset */

  reg = *(volatile uint32_t *)F1C100S_CCU_USBPHY_CFG;
  reg |= CCU_USBPHY_SCLK_GATING | CCU_USBPHY_PHY_RST;
  *(volatile uint32_t *)F1C100S_CCU_USBPHY_CFG = reg;

  /* Enable USB OTG bus clock */

  reg = *(volatile uint32_t *)F1C100S_CCU_BUS_CLK_GATING0;
  reg |= CCU_BUS_CLK_USBOTG;
  *(volatile uint32_t *)F1C100S_CCU_BUS_CLK_GATING0 = reg;

  /* Deassert USB OTG reset */

  reg = *(volatile uint32_t *)F1C100S_CCU_BUS_SOFT_RST0;
  reg |= CCU_BUS_RST_USBOTG;
  *(volatile uint32_t *)F1C100S_CCU_BUS_SOFT_RST0 = reg;

  /* Power up the analog PHY (clear SIDDQ) before any indirect PHY
   * register write. Matches Linux mainline's suniv_f1c100s_cfg init
   * order in sun4i_usb_phy_init() - without this the PHY can stay in
   * its power-down state and D+/D- are not reliably driven.
   */

  reg = USB_GETREG32(USB_PHYCTL_OFFSET);
  reg &= ~USB_PHYCTL_SIDDQ;
  USB_PUTREG32(reg, USB_PHYCTL_OFFSET);

  /* Configure PHY */

  f1c100s_phy_write(0x0c, 0x01, 1);
  f1c100s_phy_write(0x20, 0x14, 5);
  f1c100s_phy_write(0x2a, 0x03, 2);

  /* Route USB SRAM to USB controller */

  reg = *(volatile uint32_t *)F1C100S_SYS_CTRL1;
  reg |= 0x01;
  *(volatile uint32_t *)F1C100S_SYS_CTRL1 = reg;

  /* Configure ISCR for device mode */

  reg = USB_GETREG32(USB_ISCR_OFFSET);
  reg &= ~(3 << 10);
  reg &= ~0x70;
  reg |= (0x3f << 12);
  USB_PUTREG32(reg, USB_ISCR_OFFSET);

  /* Clear vendor register */

  USB_PUTREG8(0x00, USB_VEND0_OFFSET);

  /* Flush any stale EP0 FIFO content from FEL mode */

  USB_PUTREG8(0, USB_EP_IDX_OFFSET);
  USB_PUTREG16(USB_CSR0_FLUSHFIFO, USB_TXCSR_OFFSET);
  USB_PUTREG16(USB_CSR0_FLUSHFIFO, USB_TXCSR_OFFSET);

  /* Disable and clear all interrupts */

  USB_PUTREG8(0x00, USB_BUS_IE_OFFSET);
  USB_PUTREG8(0xff, USB_BUS_IS_OFFSET);
  USB_PUTREG32(0x00, USB_EP_IE_OFFSET);
  USB_PUTREG32(0xffffffff, USB_EP_IS_OFFSET);

  /* High-speed only when configured. HSENAB without DUALSPEED makes
   * Linux reject 64-byte bulk at 480 Mbps; ADB+RNDIS also cannot
   * fit four 512-byte FIFOs in the 2KB packet RAM.
   */

  reg = USB_GETREG8(USB_POWER_OFFSET);
  reg &= ~(USB_POWER_ISOUPDATE | USB_POWER_SOFTCONN | USB_POWER_HSENAB);
#ifdef CONFIG_F1C100S_USBDEV_HIGHSPEED
  reg |= USB_POWER_HSENAB;
#endif
  USB_PUTREG8(reg, USB_POWER_OFFSET);

  priv->fifo_next = F1C100S_FIFO_EP0 / 8;

  /* Enable bus interrupts: Reset, Resume, Suspend */

  USB_PUTREG8(USB_INT_RESET | USB_INT_RESUME | USB_INT_SUSPEND,
              USB_BUS_IE_OFFSET);

  /* Enable EP0 interrupt */

  USB_PUTREG32(USB_EP_INT_EP0, USB_EP_IE_OFFSET);
}


/****************************************************************************
 * Name: f1c100s_hw_shutdown
 *
 * Description:
 *   Shutdown USB hardware
 *
 ****************************************************************************/

static void f1c100s_hw_shutdown(struct f1c100s_usbdev_s *priv)
{
  /* Disable soft connect */

  uint8_t power = USB_GETREG8(USB_POWER_OFFSET);
  power &= ~USB_POWER_SOFTCONN;
  USB_PUTREG8(power, USB_POWER_OFFSET);

  /* Disable all interrupts */

  USB_PUTREG8(0x00, USB_BUS_IE_OFFSET);
  USB_PUTREG32(0x00, USB_EP_IE_OFFSET);
}

/****************************************************************************
 * Name: f1c100s_ep0_wrstatus
 *
 * Description:
 *   Send zero-length status packet on EP0
 *
 ****************************************************************************/

static void f1c100s_ep0_wrstatus(struct f1c100s_usbdev_s *priv)
{
  USB_PUTREG8(0, USB_EP_IDX_OFFSET);
  USB_PUTREG16(USB_CSR0_SVDRXPKTRDY | USB_CSR0_DATAEND, USB_TXCSR_OFFSET);
  priv->ep0state = EP0STATE_IDLE;
}

/****************************************************************************
 * Name: f1c100s_ep0_transmit
 *
 * Description:
 *   Transmit data on EP0 (DATA IN phase)
 *
 ****************************************************************************/

static void f1c100s_ep0_transmit(struct f1c100s_usbdev_s *priv)
{
  struct f1c100s_ep_s *ep0 = &priv->eplist[0];
  struct f1c100s_req_s *privreq = ep0->head;
  uint8_t *buf;
  size_t nbytes;
  size_t total;
  uint16_t wlen;

  if (privreq == NULL)
    {
      return;
    }

  USB_PUTREG8(0, USB_EP_IDX_OFFSET);

  /* Never send more than the host asked for (SETUP wLength). */

  wlen  = GETUINT16(priv->ctrl.len);
  total = privreq->req.len;
  if (total > wlen)
    {
      total = wlen;
    }

  if (total == 0 && privreq->req.xfrd == 0)
    {
      /* Zero-length EP0 response */

      USB_PUTREG16(USB_CSR0_SVDRXPKTRDY | USB_CSR0_DATAEND, USB_TXCSR_OFFSET);

      ep0->head = privreq->flink;
      if (ep0->head == NULL)
        {
          ep0->tail = NULL;
        }

      privreq->req.result = OK;
      if (privreq->req.callback != NULL)
        {
          privreq->req.callback(&ep0->ep, &privreq->req);
        }

      priv->ep0state = EP0STATE_IDLE;
      return;
    }

  buf = privreq->req.buf + privreq->req.xfrd;
  nbytes = total - privreq->req.xfrd;

  if (nbytes > F1C100S_EP0_MAXPACKET)
    {
      nbytes = F1C100S_EP0_MAXPACKET;
    }

  /* Write data to FIFO */

  f1c100s_write_fifo(0, buf, nbytes);

  privreq->req.xfrd += nbytes;

  /* Check if this is the last packet */

  if (privreq->req.xfrd >= total ||
      nbytes < F1C100S_EP0_MAXPACKET)
    {
      /* Last packet - set DataEnd */

      USB_PUTREG16(USB_CSR0_TXPKTRDY | USB_CSR0_DATAEND, USB_TXCSR_OFFSET);

      /* Complete the request */

      ep0->head = privreq->flink;
      if (ep0->head == NULL)
        {
          ep0->tail = NULL;
        }

      privreq->req.result = OK;
      if (privreq->req.callback != NULL)
        {
          privreq->req.callback(&ep0->ep, &privreq->req);
        }

      priv->ep0state = EP0STATE_IDLE;
    }
  else
    {
      /* More data to send */

      USB_PUTREG16(USB_CSR0_TXPKTRDY, USB_TXCSR_OFFSET);
      priv->ep0state = EP0STATE_DATA_IN;
    }
}

/****************************************************************************
 * Name: f1c100s_ep0_setup
 *
 * Description:
 *   Handle SETUP packet and standard USB requests
 *
 ****************************************************************************/

static void f1c100s_ep0_setup(struct f1c100s_usbdev_s *priv)
{
  struct usb_ctrlreq_s *ctrl = &priv->ctrl;
  uint16_t value = GETUINT16(ctrl->value);
  uint16_t len = GETUINT16(ctrl->len);
  int ret = -EOPNOTSUPP;

  usbtrace(TRACE_INTDECODE(F1C100S_TRACEINTID_SETUP), ctrl->req);

  /* Handle standard device requests */

  if ((ctrl->type & USB_REQ_TYPE_MASK) == USB_REQ_TYPE_STANDARD)
    {
      switch (ctrl->req)
        {
          case USB_REQ_SETADDRESS:
            {
              usbtrace(TRACE_INTDECODE(F1C100S_TRACEINTID_SETADDR), value);

              /* MUSB: the new address must take effect only AFTER the
               * status stage completes, NOT immediately. Writing
               * TXFUNCADDR now makes the zero-length status ACK go out
               * from the new address while the host still expects
               * address 0 - the status fails and the host re-resets
               * (real-hardware symptom: endless RESET/disconnect after
               * a successful GET_DESCRIPTOR). Defer the write to the
               * EP0 interrupt that signals status completion, same
               * fix already applied on T113 (t113_usbdev.c).
               */

              priv->devaddr = value & 0x7f;
              priv->setaddr_pending = true;
              f1c100s_ep0_wrstatus(priv);
              return;
            }

          case USB_REQ_GETSTATUS:
            {
              uint16_t status = 0;

              if ((ctrl->type & USB_REQ_RECIPIENT_MASK) ==
                  USB_REQ_RECIPIENT_DEVICE)
                {
                  /* Bit 0: Self-powered, Bit 1: Remote wakeup */

                  status = priv->selfpowered ? (1 << 0) : 0;
                }

              priv->ep0data[0] = status & 0xff;
              priv->ep0data[1] = (status >> 8) & 0xff;
              priv->ep0datlen = 2;

              /* Queue the response */

              struct f1c100s_ep_s *ep0 = &priv->eplist[0];
              struct f1c100s_req_s *privreq = ep0->head;

              if (privreq != NULL)
                {
                  privreq->req.buf = priv->ep0data;
                  privreq->req.len = len < 2 ? len : 2;
                  privreq->req.xfrd = 0;
                  f1c100s_ep0_transmit(priv);
                }
              else
                {
                  f1c100s_ep0_wrstatus(priv);
                }
              return;
            }

          case USB_REQ_SETFEATURE:
          case USB_REQ_CLEARFEATURE:
            {
              f1c100s_ep0_wrstatus(priv);
              return;
            }

          default:
            break;
        }
    }

  /* Control-OUT WITH a data stage (host->device, wLength>0): the OUT
   * data has NOT arrived yet, so we must NOT call the class driver now
   * (it would parse stale/empty ep0data — this is what made RNDIS
   * SEND_ENCAPSULATED_COMMAND fail with host -110).  Defer: collect the
   * OUT data in the DATA_OUT branch, then forward. Same sequence as
   * t113_usbdev.c.
   */

  if ((ctrl->type & USB_DIR_IN) == 0 && GETUINT16(ctrl->len) != 0)
    {
      priv->ep0state  = EP0STATE_DATA_OUT;
      priv->ep0outlen = GETUINT16(ctrl->len);
      priv->ep0datlen = 0;
      return;
    }

  /* Forward to class driver (no-data or control-IN requests) */

  if (priv->driver != NULL)
    {
      ret = CLASS_SETUP(priv->driver, &priv->usbdev, ctrl,
                        priv->ep0data, sizeof(priv->ep0data));
    }

  if (ret < 0)
    {
      /* Stall EP0 on error */

      USB_PUTREG8(0, USB_EP_IDX_OFFSET);
      USB_PUTREG16(USB_CSR0_SENDSTALL, USB_TXCSR_OFFSET);
      priv->ep0state = EP0STATE_STALLED;
    }
}

/****************************************************************************
 * Name: f1c100s_reset_interrupt
 *
 * Description:
 *   Handle USB reset interrupt
 *
 ****************************************************************************/

static void f1c100s_reset_interrupt(struct f1c100s_usbdev_s *priv)
{
  int i;

  /* Clear all endpoint interrupt status */

  USB_PUTREG32(0xffffffff, USB_EP_IS_OFFSET);

  /* Reset device address */

  USB_PUTREG8(0, USB_EP_IDX_OFFSET);
  USB_PUTREG8(0, USB_TXFUNCADDR_OFFSET);

  /* Reset device state */

  priv->devaddr = 0;
  priv->setaddr_pending = false;
  priv->ep0state = EP0STATE_IDLE;
  priv->fifo_next = F1C100S_FIFO_EP0 / 8;

  /* Check if high-speed negotiation succeeded */

  priv->highspeed = (USB_GETREG8(USB_POWER_OFFSET) & USB_POWER_HSMODE) != 0;
  priv->usbdev.speed = priv->highspeed ? USB_SPEED_HIGH : USB_SPEED_FULL;

  /* Update endpoint max packet sizes based on speed */

  for (i = 1; i < F1C100S_NENDPOINTS; i++)
    {
      priv->eplist[i].ep.maxpacket = priv->highspeed ?
        F1C100S_BULK_HS_MAXPACKET : F1C100S_BULK_FS_MAXPACKET;
    }

  /* Notify class driver of disconnect/reconnect */

  if (priv->driver != NULL)
    {
      CLASS_DISCONNECT(priv->driver, &priv->usbdev);
    }

  priv->connected = true;
}

/****************************************************************************
 * Name: f1c100s_ep0_interrupt
 *
 * Description:
 *   Handle EP0 control transfer interrupt
 *
 ****************************************************************************/

static void f1c100s_ep0_interrupt(struct f1c100s_usbdev_s *priv)
{
  uint16_t csr0;

  /* Select EP0 */

  USB_PUTREG8(0, USB_EP_IDX_OFFSET);
  csr0 = USB_GETREG16(USB_TXCSR_OFFSET);

  /* Handle STALL sent */

  if (csr0 & USB_CSR0_SENTSTALL)
    {
      USB_PUTREG16(csr0 & ~USB_CSR0_SENTSTALL, USB_TXCSR_OFFSET);
      priv->ep0state = EP0STATE_IDLE;
      return;
    }

  /* Apply a deferred SET_ADDRESS once the status stage has completed
   * (TxPktRdy cleared = the zero-length status IN was ACKed by the
   * host). Only now is it safe to load the new function address.
   */

  if (priv->setaddr_pending && !(csr0 & USB_CSR0_TXPKTRDY))
    {
      USB_PUTREG8(0, USB_EP_IDX_OFFSET);
      USB_PUTREG8(priv->devaddr, USB_TXFUNCADDR_OFFSET);
      priv->setaddr_pending = false;
    }

  /* Handle setup end */

  if (csr0 & USB_CSR0_SETUPEND)
    {
      USB_PUTREG16(csr0 | USB_CSR0_SVDSETUPEND, USB_TXCSR_OFFSET);
      priv->ep0state = EP0STATE_IDLE;
    }

  /* Handle DATA IN completion (TX done) */

  if (priv->ep0state == EP0STATE_DATA_IN && !(csr0 & USB_CSR0_TXPKTRDY))
    {
      /* Continue transmitting if more data */

      f1c100s_ep0_transmit(priv);
      return;
    }

  /* Handle received packet (SETUP or DATA OUT) */

  if (csr0 & USB_CSR0_RXPKTRDY)
    {
      uint16_t count;

      count = USB_GETREG16(USB_RXCOUNT_OFFSET);

      if (priv->ep0state == EP0STATE_IDLE && count == 8)
        {
          /* SETUP packet received - read from FIFO */

          f1c100s_read_fifo(0, (uint8_t *)&priv->ctrl, 8);

          /* For requests WITH a data stage, service RxPktRdy now and
           * wait for the hardware to actually clear it before moving
           * on -- mirrors t113_usbdev.c (t113_ep0_interrupt()) and
           * U-Boot's musb_read_setup() spin. For NO-DATA requests
           * (wLength==0) do NOT service it yet: MUSB wants a single
           * ServicedRxPktRdy|DataEnd write, done in
           * f1c100s_ep0_wrstatus() or the zero-length EP0 submit.
           */

          if (GETUINT16(priv->ctrl.len) != 0)
            {
              USB_PUTREG16(USB_CSR0_SVDRXPKTRDY, USB_TXCSR_OFFSET);

              while (USB_GETREG16(USB_TXCSR_OFFSET) & USB_CSR0_RXPKTRDY)
                {
                }
            }

          /* Process the SETUP packet */

          f1c100s_ep0_setup(priv);
        }
      else if (priv->ep0state == EP0STATE_DATA_OUT)
        {
          uint8_t *buf = priv->ep0data + priv->ep0datlen;

          if (priv->ep0datlen + count > sizeof(priv->ep0data))
            {
              count = sizeof(priv->ep0data) - priv->ep0datlen;
            }

          f1c100s_read_fifo(0, buf, count);
          priv->ep0datlen += count;

          if (priv->ep0datlen >= priv->ep0outlen)
            {
              /* All OUT data received: NOW forward the complete request
               * + data to the class driver, then finish the status stage.
               */

              if (priv->driver != NULL)
                {
                  CLASS_SETUP(priv->driver, &priv->usbdev, &priv->ctrl,
                              priv->ep0data, priv->ep0datlen);
                }

              /* CLASS_SETUP may submit on another endpoint (RNDIS
               * RESPONSE_AVAILABLE on the interrupt IN), which leaves
               * USB_EP_IDX pointing at that EP. Re-select EP0 before
               * writing TXCSR or the status stage never completes
               * (host times out -> rndis_host -110).
               */

              USB_PUTREG8(0, USB_EP_IDX_OFFSET);
              USB_PUTREG16(USB_CSR0_SVDRXPKTRDY | USB_CSR0_DATAEND,
                           USB_TXCSR_OFFSET);
              priv->ep0state = EP0STATE_IDLE;
            }
          else
            {
              USB_PUTREG16(USB_CSR0_SVDRXPKTRDY, USB_TXCSR_OFFSET);
            }
        }
    }
}

/****************************************************************************
 * Name: f1c100s_epin_interrupt
 *
 * Description:
 *   Handle TX (IN) endpoint interrupt
 *
 ****************************************************************************/

static void f1c100s_epin_interrupt(struct f1c100s_usbdev_s *priv, int epno)
{
  struct f1c100s_ep_s *privep = &priv->eplist[epno];
  struct f1c100s_req_s *privreq;
  uint16_t txcsr;

  /* Select endpoint */

  USB_PUTREG8(epno, USB_EP_IDX_OFFSET);
  txcsr = USB_GETREG16(USB_TXCSR_OFFSET);

  /* Clear sent stall. Also drop SENDSTALL or the EP keeps stalling
   * every subsequent IN (host usbmon: Bi complete -EPIPE, CLEAR_HALT,
   * repeat — rndis_host rx_errors in the tens of thousands).
   */

  if (txcsr & USB_TXCSR_SENTSTALL)
    {
      USB_PUTREG16(txcsr & ~(USB_TXCSR_SENTSTALL | USB_TXCSR_SENDSTALL),
                   USB_TXCSR_OFFSET);
      return;
    }

  privep->txbusy = false;
  privreq = privep->head;
  if (privreq == NULL)
    {
      return;
    }

  /* Multi-packet: a request larger than maxpacket (RNDIS ~86B ARP at
   * FS 64, or a full Ethernet frame) must continue, not complete.
   */

  if (privreq->req.xfrd < privreq->req.len)
    {
      f1c100s_ep_txdata(privep);
      return;
    }

  privep->head = privreq->flink;
  if (privep->head == NULL)
    {
      privep->tail = NULL;
    }

  privreq->req.result = OK;
  if (privreq->req.callback != NULL)
    {
      privreq->req.callback(&privep->ep, &privreq->req);
    }

  if (privep->head != NULL)
    {
      f1c100s_ep_txdata(privep);
    }
}

/****************************************************************************
 * Name: f1c100s_epout_interrupt
 *
 * Description:
 *   Handle RX (OUT) endpoint interrupt
 *
 ****************************************************************************/

static void f1c100s_epout_interrupt(struct f1c100s_usbdev_s *priv, int epno)
{
  struct f1c100s_ep_s *privep = &priv->eplist[epno];
  struct f1c100s_req_s *privreq;
  uint16_t rxcsr;
  uint16_t count;

  /* Select endpoint */

  USB_PUTREG8(epno, USB_EP_IDX_OFFSET);
  rxcsr = USB_GETREG16(USB_RXCSR_OFFSET);

  if (rxcsr & USB_RXCSR_SENTSTALL)
    {
      USB_PUTREG16(rxcsr & ~(USB_RXCSR_SENTSTALL | USB_RXCSR_SENDSTALL),
                   USB_RXCSR_OFFSET);
      return;
    }

  /* Check for received data */

  if (!(rxcsr & USB_RXCSR_RXPKTRDY))
    {
      return;
    }

  count = USB_GETREG16(USB_RXCOUNT_OFFSET);
  privreq = privep->head;

  if (privreq != NULL && privreq->req.buf != NULL)
    {
      uint8_t *buf = privreq->req.buf + privreq->req.xfrd;
      size_t remaining = privreq->req.len - privreq->req.xfrd;
      size_t nbytes = count < remaining ? count : remaining;

#ifdef CONFIG_F1C100S_USB_DMA
      if (remaining >= CONFIG_F1C100S_USB_DMA_THRESHOLD)
        {
          int ret;

          ret = f1c100s_usb_dma_start(epno, USB_DMA_DIR_RX, buf, remaining,
                                      NULL, NULL);
          if (ret == OK)
            {
              ret = f1c100s_usb_dma_wait(epno, 5000);
              if (ret == OK)
                {
                  privreq->req.xfrd = privreq->req.len;

                  /* Re-read rxcsr after DMA completes */

                  rxcsr = USB_GETREG16(USB_RXCSR_OFFSET);

                  /* Clear RxPktRdy */

                  USB_PUTREG16(rxcsr & ~USB_RXCSR_RXPKTRDY, USB_RXCSR_OFFSET);

                  /* Complete the request */

                  privep->head = privreq->flink;
                  if (privep->head == NULL)
                    {
                      privep->tail = NULL;
                    }

                  privreq->req.result = OK;
                  if (privreq->req.callback != NULL)
                    {
                      privreq->req.callback(&privep->ep, &privreq->req);
                    }
                  return;
                }
            }
          /* DMA failed, fall through to PIO */
        }
#endif

      /* Read data from FIFO */

      f1c100s_read_fifo(epno, buf, nbytes);

      privreq->req.xfrd += nbytes;

      /* Clear RxPktRdy */

      USB_PUTREG16((rxcsr | USB_RXCSR_SENTSTALL | USB_RXCSR_OVERRUN) &
                   ~(USB_RXCSR_RXPKTRDY | USB_RXCSR_FLUSHFIFO | USB_RXCSR_CLRDATATOG),
                   USB_RXCSR_OFFSET);

      /* Check if request is complete */

      if (privreq->req.xfrd >= privreq->req.len ||
          count < privep->ep.maxpacket)
        {
          /* Complete the request */

          privep->head = privreq->flink;
          if (privep->head == NULL)
            {
              privep->tail = NULL;
            }

          privreq->req.result = OK;
          if (privreq->req.callback != NULL)
            {
              privreq->req.callback(&privep->ep, &privreq->req);
            }
        }
    }
  else
    {
      /* No request queued yet: leave RxPktRdy set so the hardware NAKs
       * further OUT tokens instead of accepting (and silently
       * discarding) more data into an already-full FIFO. Clearing it
       * here without reading the FIFO was confirmed on real hardware
       * to drop data the host had already been ACKed for -- see
       * docs/issues/fastboot_erase_f1c100s.md. f1c100s_ep_submit()
       * drains this pending packet as soon as a request is queued.
       */
    }
}

/****************************************************************************
 * Name: f1c100s_interrupt
 *
 * Description:
 *   USB interrupt handler
 *
 ****************************************************************************/

static int f1c100s_interrupt(int irq, void *context, void *arg)
{
  struct f1c100s_usbdev_s *priv = (struct f1c100s_usbdev_s *)arg;
  uint8_t bus_is;
  uint32_t ep_is;
  int epno;

  /* Read and clear bus interrupt status */

  bus_is = USB_GETREG8(USB_BUS_IS_OFFSET);
  if (bus_is != 0)
    {
      USB_PUTREG8(bus_is, USB_BUS_IS_OFFSET);
    }

  /* Read and clear endpoint interrupt status */

  ep_is = USB_GETREG32(USB_EP_IS_OFFSET);
  if (ep_is != 0)
    {
      USB_PUTREG32(ep_is, USB_EP_IS_OFFSET);
    }
  /* Handle bus interrupts */

  if (bus_is & USB_INT_RESET)
    {
      usbtrace(TRACE_INTDECODE(F1C100S_TRACEINTID_RESET), bus_is);
      f1c100s_reset_interrupt(priv);
    }

  if (bus_is & USB_INT_SUSPEND)
    {
      usbtrace(TRACE_INTDECODE(F1C100S_TRACEINTID_SUSPEND), 0);
      if (priv->driver != NULL)
        {
          CLASS_SUSPEND(priv->driver, &priv->usbdev);
        }
    }

  if (bus_is & USB_INT_RESUME)
    {
      usbtrace(TRACE_INTDECODE(F1C100S_TRACEINTID_RESUME), 0);
      if (priv->driver != NULL)
        {
          CLASS_RESUME(priv->driver, &priv->usbdev);
        }
    }

  /* Handle EP0 interrupt */

  if (ep_is & USB_EP_INT_EP0)
    {
      usbtrace(TRACE_INTDECODE(F1C100S_TRACEINTID_EP0), ep_is);
      f1c100s_ep0_interrupt(priv);
    }

  /* Handle TX endpoint interrupts (EP1-4) */

  for (epno = 1; epno < F1C100S_NENDPOINTS; epno++)
    {
      if (ep_is & USB_EP_INT_TX(epno))
        {
          usbtrace(TRACE_INTDECODE(F1C100S_TRACEINTID_EPIN), epno);
          f1c100s_epin_interrupt(priv, epno);
        }
    }

  /* Handle RX endpoint interrupts (EP1-4) */

  for (epno = 1; epno < F1C100S_NENDPOINTS; epno++)
    {
      if (ep_is & USB_EP_INT_RX(epno))
        {
          usbtrace(TRACE_INTDECODE(F1C100S_TRACEINTID_EPOUT), epno);
          f1c100s_epout_interrupt(priv, epno);
        }
    }

  return OK;
}

/****************************************************************************
 * Endpoint Operations
 ****************************************************************************/

/****************************************************************************
 * Name: f1c100s_ep_setfifo
 *
 * Description:
 *   Bump-allocate a power-of-two FIFO slice >= maxpacket from the 2KB
 *   packet RAM. Addresses are in 8-byte units (MUSB TX/RXFIFOADDR).
 *
 ****************************************************************************/

static int f1c100s_ep_setfifo(struct f1c100s_usbdev_s *priv, bool in,
                              uint16_t maxpacket)
{
  uint8_t  enc = 0;
  uint16_t sz = 8;
  uint16_t units;
  uint16_t addr;

  if (maxpacket == 0)
    {
      maxpacket = 8;
    }

  while (sz < maxpacket && enc < USB_FIFOSZ_1024)
    {
      sz <<= 1;
      enc++;
    }

  units = 1u << enc;
  if ((uint32_t)priv->fifo_next + units > (F1C100S_FIFO_RAM / 8))
    {
      usbtrace(TRACE_DEVERROR(F1C100S_TRACEERR_ALLOCFAIL), maxpacket);
      uerr("USB FIFO full: need %u bytes at offset %u\n",
           (unsigned)sz, (unsigned)(priv->fifo_next * 8));
      return -ENOSPC;
    }

  addr = priv->fifo_next;
  priv->fifo_next += units;

  if (in)
    {
      USB_PUTREG8(enc, USB_TXFIFOSZ_OFFSET);
      USB_PUTREG16(addr, USB_TXFIFOADDR_OFFSET);
    }
  else
    {
      USB_PUTREG8(enc, USB_RXFIFOSZ_OFFSET);
      USB_PUTREG16(addr, USB_RXFIFOADDR_OFFSET);
    }

  return OK;
}

/****************************************************************************
 * Name: f1c100s_ep_configure
 *
 * Description:
 *   Configure an endpoint for use
 *
 ****************************************************************************/

static int f1c100s_ep_configure(struct usbdev_ep_s *ep,
                                const struct usb_epdesc_s *desc, bool last)
{
  struct f1c100s_ep_s *privep = (struct f1c100s_ep_s *)ep;
  struct f1c100s_usbdev_s *priv = privep->dev;
  uint16_t maxpacket;
  uint8_t eptype;
  uint8_t epno;
  bool in;
  int ret;

  epno = USB_EPNO(desc->addr);
  in = USB_ISEPIN(desc->addr);
  maxpacket = GETUINT16(desc->mxpacketsize);
  eptype = desc->attr & USB_EP_ATTR_XFERTYPE_MASK;

  usbtrace(TRACE_EPCONFIGURE, epno);

  /* Update endpoint info */

  privep->ep.maxpacket = maxpacket;
  privep->ep.eplog = desc->addr;
  privep->stalled = false;
  privep->txbusy = false;

  /* Select endpoint */

  USB_PUTREG8(epno, USB_EP_IDX_OFFSET);

  ret = f1c100s_ep_setfifo(priv, in, maxpacket);
  if (ret < 0)
    {
      return ret;
    }

  if (in)
    {
      uint16_t txcsr;

      USB_PUTREG16(maxpacket, USB_TXMAXP_OFFSET);

      txcsr = USB_TXCSR_CLRDATATOG | USB_TXCSR_FLUSHFIFO | USB_TXCSR_MODE;
      if (eptype == USB_EP_ATTR_XFER_ISOC)
        {
          txcsr |= USB_TXCSR_ISO;
        }

      USB_PUTREG16(txcsr, USB_TXCSR_OFFSET);

      USB_PUTREG32(USB_GETREG32(USB_EP_IE_OFFSET) | USB_EP_INT_TX(epno),
                   USB_EP_IE_OFFSET);
    }
  else
    {
      uint16_t rxcsr;

      USB_PUTREG16(maxpacket, USB_RXMAXP_OFFSET);

      rxcsr = USB_RXCSR_CLRDATATOG | USB_RXCSR_FLUSHFIFO;
      if (eptype == USB_EP_ATTR_XFER_ISOC)
        {
          rxcsr |= USB_RXCSR_ISO;
        }

      USB_PUTREG16(rxcsr, USB_RXCSR_OFFSET);

      USB_PUTREG32(USB_GETREG32(USB_EP_IE_OFFSET) | USB_EP_INT_RX(epno),
                   USB_EP_IE_OFFSET);
    }

  return OK;
}

/****************************************************************************
 * Name: f1c100s_ep_disable
 *
 * Description:
 *   Disable an endpoint
 *
 ****************************************************************************/

static int f1c100s_ep_disable(struct usbdev_ep_s *ep)
{
  struct f1c100s_ep_s *privep = (struct f1c100s_ep_s *)ep;
  struct f1c100s_req_s *privreq;
  irqstate_t flags;
  uint8_t epno;

  epno = privep->epphy;

  flags = enter_critical_section();

  /* Cancel all pending requests */

  while ((privreq = privep->head) != NULL)
    {
      privep->head = privreq->flink;
      privreq->req.result = -ESHUTDOWN;
      if (privreq->req.callback != NULL)
        {
          privreq->req.callback(&privep->ep, &privreq->req);
        }
    }

  privep->tail = NULL;

  /* Disable endpoint interrupts */

  USB_PUTREG8(epno, USB_EP_IDX_OFFSET);
  USB_PUTREG32(USB_GETREG32(USB_EP_IE_OFFSET) &
               ~(USB_EP_INT_TX(epno) | USB_EP_INT_RX(epno)),
               USB_EP_IE_OFFSET);

  leave_critical_section(flags);
  return OK;
}

static struct usbdev_req_s *f1c100s_ep_allocreq(struct usbdev_ep_s *ep)
{
  struct f1c100s_req_s *privreq;

  privreq = kmm_zalloc(sizeof(struct f1c100s_req_s));
  if (privreq != NULL)
    {
      return &privreq->req;
    }

  return NULL;
}

static void f1c100s_ep_freereq(struct usbdev_ep_s *ep,
                               struct usbdev_req_s *req)
{
  struct f1c100s_req_s *privreq = (struct f1c100s_req_s *)req;

  if (privreq != NULL)
    {
      kmm_free(privreq);
    }
}

/****************************************************************************
 * Name: f1c100s_ep_txdata
 *
 * Description:
 *   Write data to TX FIFO and start transmission
 *
 ****************************************************************************/

static void f1c100s_ep_txdata(struct f1c100s_ep_s *privep)
{
  struct f1c100s_req_s *privreq = privep->head;
  uint8_t *buf;
  size_t nbytes;

  if (privreq == NULL || privep->txbusy)
    {
      return;
    }

  USB_PUTREG8(privep->epphy, USB_EP_IDX_OFFSET);

  buf = privreq->req.buf + privreq->req.xfrd;
  nbytes = privreq->req.len - privreq->req.xfrd;

#ifdef CONFIG_F1C100S_USB_DMA
  if (nbytes >= CONFIG_F1C100S_USB_DMA_THRESHOLD)
    {
      int ret;

      ret = f1c100s_usb_dma_start(privep->epphy, USB_DMA_DIR_TX, buf, nbytes,
                                  NULL, NULL);
      if (ret == OK)
        {
          ret = f1c100s_usb_dma_wait(privep->epphy, 5000);
          if (ret == OK)
            {
              /* Set TxPktRdy to trigger transmission */

              uint16_t txcsr = USB_GETREG16(USB_TXCSR_OFFSET);
              txcsr |= USB_TXCSR_TXPKTRDY;
              USB_PUTREG16(txcsr, USB_TXCSR_OFFSET);

              privreq->req.xfrd = privreq->req.len;
              privep->txbusy = true;
              return;
            }
        }
      /* DMA failed, fall through to PIO */
    }
#endif

  if (nbytes > privep->ep.maxpacket)
    {
      nbytes = privep->ep.maxpacket;
    }

  usbtrace(TRACE_WRITE(privep->epphy), nbytes);

  /* Write data to FIFO */

  f1c100s_write_fifo(privep->epphy, buf, nbytes);

  privreq->req.xfrd += nbytes;
  privep->txbusy = true;

  /* Set TxPktRdy to start transmission */

  USB_PUTREG16(USB_GETREG16(USB_TXCSR_OFFSET) | USB_TXCSR_TXPKTRDY,
               USB_TXCSR_OFFSET);
}

/****************************************************************************
 * Name: f1c100s_ep_submit
 *
 * Description:
 *   Submit a request to an endpoint
 *
 ****************************************************************************/

static int f1c100s_ep_submit(struct usbdev_ep_s *ep,
                             struct usbdev_req_s *req)
{
  struct f1c100s_ep_s *privep = (struct f1c100s_ep_s *)ep;
  struct f1c100s_usbdev_s *priv = privep->dev;
  struct f1c100s_req_s *privreq = (struct f1c100s_req_s *)req;
  irqstate_t flags;
  bool in;

  if (ep == NULL || req == NULL || req->callback == NULL || req->buf == NULL)
    {
      usbtrace(TRACE_DEVERROR(F1C100S_TRACEERR_BADREQUEST), 0);
      return -EINVAL;
    }

  usbtrace(TRACE_EPSUBMIT, USB_EPNO(privep->ep.eplog));

  req->xfrd = 0;
  req->result = -EBUSY;
  privreq->flink = NULL;

  flags = enter_critical_section();

  /* RNDIS SEND_ENCAPSULATED_COMMAND (and other class control-OUT)
   * returns 0 from CLASS_SETUP and composite_ep0submit() queues a
   * zero-length EP0 request for the status stage.  MUSB already
   * completes that stage with SVDRXPKTRDY|DATAEND in the DATA_OUT
   * path.  If we queue the ZLP, it sits on EP0 until the next
   * control-IN (GET_ENCAPSULATED_RESPONSE): first GET may return 0
   * bytes or hang, so Linux rndis_host INIT can pass and QUERY then
   * fails with -110.  Complete the ZLP locally; do not put it on
   * the wire.
   */

  if (privep->epphy == 0 &&
      priv->ep0state == EP0STATE_DATA_OUT &&
      req->len == 0)
    {
      leave_critical_section(flags);
      req->result = OK;
      if (req->callback != NULL)
        {
          req->callback(ep, req);
        }

      return OK;
    }

  /* Add request to queue */

  if (privep->tail != NULL)
    {
      privep->tail->flink = privreq;
    }
  else
    {
      privep->head = privreq;
    }

  privep->tail = privreq;

  /* Handle EP0 specially - it's bidirectional */

  if (privep->epphy == 0)
    {
      /* EP0 DATA IN phase - transmit response */

      if (priv->ep0state == EP0STATE_IDLE ||
          priv->ep0state == EP0STATE_DATA_IN)
        {
          f1c100s_ep0_transmit(priv);
        }
    }
  else
    {
      in = USB_ISEPIN(privep->ep.eplog);
      if (in)
        {
          /* Start transfer if this is an IN endpoint and not busy */

          if (!privep->txbusy && privep->head == privreq)
            {
              f1c100s_ep_txdata(privep);
            }
        }
      else if (privep->head == privreq)
        {
          /* A packet may already be sitting in the FIFO with RxPktRdy
           * held (see f1c100s_epout_interrupt()'s no-request branch):
           * drain it into this freshly-queued request now instead of
           * waiting for an interrupt that will never come, since the
           * hardware won't assert a new one while RxPktRdy stays set.
           */

          f1c100s_epout_interrupt(priv, USB_EPNO(privep->ep.eplog));
        }
    }

  leave_critical_section(flags);
  return OK;
}

/****************************************************************************
 * Name: f1c100s_ep_cancel
 *
 * Description:
 *   Cancel a pending request
 *
 ****************************************************************************/

static int f1c100s_ep_cancel(struct usbdev_ep_s *ep,
                             struct usbdev_req_s *req)
{
  struct f1c100s_ep_s *privep = (struct f1c100s_ep_s *)ep;
  struct f1c100s_req_s *privreq = (struct f1c100s_req_s *)req;
  struct f1c100s_req_s *curr;
  struct f1c100s_req_s *prev;
  irqstate_t flags;

  flags = enter_critical_section();

  /* Find and remove the request from the queue */

  prev = NULL;
  for (curr = privep->head; curr != NULL; prev = curr, curr = curr->flink)
    {
      if (curr == privreq)
        {
          if (prev != NULL)
            {
              prev->flink = curr->flink;
            }
          else
            {
              privep->head = curr->flink;
            }

          if (privep->tail == curr)
            {
              privep->tail = prev;
            }

          curr->req.result = -ECANCELED;
          leave_critical_section(flags);

          if (curr->req.callback != NULL)
            {
              curr->req.callback(&privep->ep, &curr->req);
            }

          return OK;
        }
    }

  leave_critical_section(flags);
  return -ENOENT;
}

/****************************************************************************
 * Name: f1c100s_ep_stall
 *
 * Description:
 *   Stall or resume an endpoint
 *
 ****************************************************************************/

static int f1c100s_ep_stall(struct usbdev_ep_s *ep, bool resume)
{
  struct f1c100s_ep_s *privep = (struct f1c100s_ep_s *)ep;
  uint8_t epno = privep->epphy;
  bool in;

  in = USB_ISEPIN(privep->ep.eplog);

  USB_PUTREG8(epno, USB_EP_IDX_OFFSET);

  if (resume)
    {
      /* Clear stall */

      privep->stalled = false;

      if (in)
        {
          USB_PUTREG16(USB_GETREG16(USB_TXCSR_OFFSET) &
                       ~USB_TXCSR_SENDSTALL, USB_TXCSR_OFFSET);
        }
      else
        {
          USB_PUTREG16(USB_GETREG16(USB_RXCSR_OFFSET) &
                       ~USB_RXCSR_SENDSTALL, USB_RXCSR_OFFSET);
        }
    }
  else
    {
      /* Set stall */

      privep->stalled = true;

      if (in)
        {
          USB_PUTREG16(USB_GETREG16(USB_TXCSR_OFFSET) |
                       USB_TXCSR_SENDSTALL, USB_TXCSR_OFFSET);
        }
      else
        {
          USB_PUTREG16(USB_GETREG16(USB_RXCSR_OFFSET) |
                       USB_RXCSR_SENDSTALL, USB_RXCSR_OFFSET);
        }
    }

  return OK;
}

/****************************************************************************
 * Device Operations
 ****************************************************************************/

static struct usbdev_ep_s *f1c100s_allocep(struct usbdev_s *dev,
                                           uint8_t epno, bool in,
                                           uint8_t eptype)
{
  struct f1c100s_usbdev_s *priv = (struct f1c100s_usbdev_s *)dev;
  struct f1c100s_ep_s *privep;

  /* Ignore direction bits in logical address */

  epno = USB_EPNO(epno);

  /* EP0 is reserved */

  if (epno == 0)
    {
      return NULL;
    }

  /* Check endpoint number */

  if (epno >= F1C100S_NENDPOINTS)
    {
      return NULL;
    }

  privep = &priv->eplist[epno];

  /* Set the logical endpoint address (including direction bit) */

  privep->ep.eplog = epno | (in ? USB_DIR_IN : USB_DIR_OUT);

  return &privep->ep;
}

static void f1c100s_freeep(struct usbdev_s *dev, struct usbdev_ep_s *ep)
{
  /* Nothing to do - endpoints are statically allocated */
}

static int f1c100s_getframe(struct usbdev_s *dev)
{
  return (int)USB_GETREG16(USB_FRAME_OFFSET);
}

static int f1c100s_wakeup(struct usbdev_s *dev)
{
  /* TODO: Implement remote wakeup */

  return OK;
}

static int f1c100s_selfpowered(struct usbdev_s *dev, bool selfpowered)
{
  struct f1c100s_usbdev_s *priv = (struct f1c100s_usbdev_s *)dev;

  priv->selfpowered = selfpowered;
  return OK;
}

static int f1c100s_pullup(struct usbdev_s *dev, bool enable)
{
  uint8_t power;
  uint8_t devctl;

  power  = USB_GETREG8(USB_POWER_OFFSET);
  devctl = USB_GETREG8(USB_DEVCTL_OFFSET);

  if (enable)
    {
      power |= USB_POWER_SOFTCONN;

      /* Start a B-device session.  The MUSB only drives the D+ pull-up once
       * DEVCTL.Session is set; without it SOFTCONN alone has no effect.
       */

      devctl |= USB_DEVCTL_SESSION;
    }
  else
    {
      power  &= ~USB_POWER_SOFTCONN;
      devctl &= ~USB_DEVCTL_SESSION;
    }

  USB_PUTREG8(devctl, USB_DEVCTL_OFFSET);
  USB_PUTREG8(power, USB_POWER_OFFSET);
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: f1c100s_usbdev_initialize
 *
 * Description:
 *   Initialize the USB device controller
 *
 ****************************************************************************/

int f1c100s_usbdev_initialize(void)
{
  struct f1c100s_usbdev_s *priv = &g_usbdev;
  int i;
#ifdef CONFIG_F1C100S_USB_DMA
  int ret;
#endif

  usbtrace(TRACE_DEVINIT, 0);
  uinfo("F1C100s USB Device initializing...\n");

  /* Initialize device structure */

  memset(priv, 0, sizeof(struct f1c100s_usbdev_s));

  priv->usbdev.ops = &g_devops;
  priv->usbdev.ep0 = &priv->eplist[0].ep;
  priv->ep0state = EP0STATE_IDLE;

  /* Initialize endpoints */

  for (i = 0; i < F1C100S_NENDPOINTS; i++)
    {
      priv->eplist[i].ep.ops = &g_epops;
      priv->eplist[i].ep.maxpacket = (i == 0) ?
        F1C100S_EP0_MAXPACKET : F1C100S_BULK_FS_MAXPACKET;
      priv->eplist[i].dev = priv;
      priv->eplist[i].epphy = i;
    }

  /* Initialize hardware */

  f1c100s_hw_init(priv);

#ifdef CONFIG_F1C100S_USB_DMA
  ret = f1c100s_usb_dma_init();
  if (ret < 0)
    {
      uerr("USB DMA init failed: %d\n", ret);
      /* Continue without DMA - it's optional */
    }
#endif

  /* Attach interrupt handler */

  irq_attach(F1C100S_USB_IRQ, f1c100s_interrupt, priv);
  up_enable_irq(F1C100S_USB_IRQ);

  uinfo("F1C100s USB Device initialized, IRQ %d attached\n", F1C100S_USB_IRQ);

  return OK;
}

/****************************************************************************
 * Name: f1c100s_usbdev_uninitialize
 *
 * Description:
 *   Uninitialize the USB device controller
 *
 ****************************************************************************/

void f1c100s_usbdev_uninitialize(void)
{
  struct f1c100s_usbdev_s *priv = &g_usbdev;

  up_disable_irq(F1C100S_USB_IRQ);
  irq_detach(F1C100S_USB_IRQ);

  f1c100s_hw_shutdown(priv);
}

/****************************************************************************
 * Name: usbdev_register
 *
 * Description:
 *   Register a USB device class driver
 *
 ****************************************************************************/

int usbdev_register(struct usbdevclass_driver_s *driver)
{
  struct f1c100s_usbdev_s *priv = &g_usbdev;
  int ret;

  if (driver == NULL || driver->ops->bind == NULL ||
      driver->ops->unbind == NULL || driver->ops->setup == NULL)
    {
      return -EINVAL;
    }

  if (priv->driver != NULL)
    {
      return -EBUSY;
    }

  priv->driver = driver;

  ret = CLASS_BIND(driver, &priv->usbdev);
  if (ret < 0)
    {
      priv->driver = NULL;
      return ret;
    }

  return OK;
}

/****************************************************************************
 * Name: usbdev_unregister
 *
 * Description:
 *   Unregister a USB device class driver
 *
 ****************************************************************************/

int usbdev_unregister(struct usbdevclass_driver_s *driver)
{
  struct f1c100s_usbdev_s *priv = &g_usbdev;

  if (driver != priv->driver)
    {
      return -EINVAL;
    }

  CLASS_DISCONNECT(driver, &priv->usbdev);
  CLASS_UNBIND(driver, &priv->usbdev);

  priv->driver = NULL;
  return OK;
}

/****************************************************************************
 * Name: arm_usbinitialize / arm_usbuninitialize
 *
 * Description:
 *   Core hooks called from arm_initialize (up_initialize).  The real USB
 *   device controller init is performed explicitly from board bringup
 *   (f1c100s_usbdev_initialize() + cdcacm_initialize()), so these are no-op
 *   stubs that only satisfy the linker and avoid early-boot initialization
 *   before scheduler and board bringup are ready.
 *
 ****************************************************************************/

void arm_usbinitialize(void)
{
}

void arm_usbuninitialize(void)
{
}

#endif /* CONFIG_F1C100S_USBDEV */
