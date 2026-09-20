/****************************************************************************
 * vendor/allwinner/chips/f1c100s/f1c100s_usbhost.c
 *
 * F1C100s USB Host Controller Driver - Phase 1
 * Minimal implementation: PHY init + wait/enumerate
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/spinlock.h>
#include <nuttx/kmalloc.h>
#include <nuttx/signal.h>
#include <nuttx/mutex.h>
#include <nuttx/semaphore.h>
#include <nuttx/usb/usb.h>
#include <nuttx/usb/usbhost.h>

#ifdef CONFIG_F1C100S_USBHOST_VBUS_DETECT
#include <fcntl.h>
#include <sys/ioctl.h>
#include <nuttx/ioexpander/gpio.h>
#include <nuttx/wqueue.h>
#endif

#include "arm_internal.h"
#include "hardware/f1c100s_usb.h"

#ifdef CONFIG_F1C100S_USB_DMA
#include "f1c100s_usb_dma.h"
#endif

#ifdef CONFIG_F1C100S_USBHOST

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define CTRL_TIMEOUT      MSEC2TICK(500)   /* Control transfer timeout */
#define BULK_TIMEOUT      MSEC2TICK(1000)  /* Bulk transfer timeout */
#define CONNECT_TIMEOUT   100     /* Connection debounce (ms) */

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Endpoint structure */

struct f1c100s_ep_s
{
  uint8_t epno;           /* Endpoint number */
  bool in;                /* Direction: true=IN */
  uint8_t xfrtype;        /* Transfer type */
  uint16_t maxpacket;     /* Max packet size */
  uint8_t interval;       /* Polling interval */
  uint8_t funcaddr;       /* Device function address */

  /* Async transfer support */

#ifdef CONFIG_USBHOST_ASYNCH
  usbhost_asynch_t callback;
  void *arg;
  uint8_t *buffer;
  size_t buflen;
#endif
};

/* Main driver structure */

struct f1c100s_usbhost_s
{
  struct usbhost_driver_s drvr;       /* NuttX driver interface */
  struct usbhost_connection_s conn;   /* Connection interface */
  struct usbhost_roothubport_s rhport; /* Root Hub port */
  struct usbhost_devaddr_s devgen;    /* Device address generator */

  mutex_t lock;           /* Mutex */
  sem_t pscsem;           /* Port status change semaphore */

  volatile bool connected; /* Device connection status */
  volatile bool change;    /* Connection state changed */
  uint8_t speed;          /* Device speed */

#ifdef CONFIG_F1C100S_USBHOST_VBUS_DETECT
  /* VBUS detection GPIO support */

  int vbus_fd;                    /* VBUS detect GPIO file descriptor */
  struct work_s vbus_work;        /* VBUS detect work queue item */
#endif

  /* Endpoint pool */

  struct f1c100s_ep_s ep[F1C100S_USB_NENDPOINTS];
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/* Connection interface methods */

static int f1c100s_wait(struct usbhost_connection_s *conn,
                        struct usbhost_hubport_s **hport);
static int f1c100s_enumerate(struct usbhost_connection_s *conn,
                             struct usbhost_hubport_s *hport);

/* Driver interface methods */

static int f1c100s_ep0configure(struct usbhost_driver_s *drvr,
                                usbhost_ep_t ep0, uint8_t funcaddr,
                                uint8_t speed, uint16_t maxpacketsize);
static int f1c100s_epalloc(struct usbhost_driver_s *drvr,
                           const struct usbhost_epdesc_s *epdesc,
                           usbhost_ep_t *ep);
static int f1c100s_epfree(struct usbhost_driver_s *drvr, usbhost_ep_t ep);
static int f1c100s_alloc(struct usbhost_driver_s *drvr,
                         uint8_t **buffer, size_t *maxlen);
static int f1c100s_free(struct usbhost_driver_s *drvr, uint8_t *buffer);
static int f1c100s_ioalloc(struct usbhost_driver_s *drvr,
                           uint8_t **buffer, size_t buflen);
static int f1c100s_iofree(struct usbhost_driver_s *drvr, uint8_t *buffer);
static int f1c100s_ctrlin(struct usbhost_driver_s *drvr, usbhost_ep_t ep0,
                          const struct usb_ctrlreq_s *req, uint8_t *buffer);
static int f1c100s_ctrlout(struct usbhost_driver_s *drvr, usbhost_ep_t ep0,
                           const struct usb_ctrlreq_s *req,
                           const uint8_t *buffer);
static ssize_t f1c100s_transfer(struct usbhost_driver_s *drvr,
                                usbhost_ep_t ep, uint8_t *buffer,
                                size_t buflen);
#ifdef CONFIG_USBHOST_ASYNCH
static int f1c100s_asynch(struct usbhost_driver_s *drvr, usbhost_ep_t ep,
                          uint8_t *buffer, size_t buflen,
                          usbhost_asynch_t callback, void *arg);
#endif
static int f1c100s_cancel(struct usbhost_driver_s *drvr, usbhost_ep_t ep);
#ifdef CONFIG_USBHOST_HUB
static int f1c100s_connect(struct usbhost_driver_s *drvr,
                           struct usbhost_hubport_s *hport, bool connected);
#endif
static void f1c100s_disconnect(struct usbhost_driver_s *drvr,
                               struct usbhost_hubport_s *hport);

/* Internal functions */

static void f1c100s_read_fifo(uint8_t ep, uint8_t *buffer, size_t len);

static void f1c100s_phy_write(uint8_t addr, uint8_t data, uint8_t len);
static void f1c100s_phy_init_host(void);
static void f1c100s_usb_reset(void);
static uint8_t f1c100s_get_speed(void);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct f1c100s_usbhost_s g_usbhost;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: f1c100s_phy_write
 *
 * Description:
 *   Write to USB PHY register via bit-banging interface.
 *
 ****************************************************************************/

static void f1c100s_phy_write(uint8_t addr, uint8_t data, uint8_t len)
{
  uint32_t phyctl;
  uint32_t i;

  for (i = 0; i < len; i++)
    {
      phyctl = USB_GETREG32(USB_PHYCTL_OFFSET);
      phyctl &= 0xffff0000;
      phyctl |= ((addr + i) << 8) | ((data & 1) << 7);
      USB_PUTREG32(phyctl, USB_PHYCTL_OFFSET);

      phyctl |= 1;
      USB_PUTREG32(phyctl, USB_PHYCTL_OFFSET);

      phyctl &= ~1;
      USB_PUTREG32(phyctl, USB_PHYCTL_OFFSET);

      data >>= 1;
    }
}

/****************************************************************************
 * Name: f1c100s_phy_init_host
 *
 * Description:
 *   Initialize USB PHY for Host mode.
 *
 ****************************************************************************/

static void f1c100s_phy_init_host(void)
{
  uint32_t reg;

  /* 1. Enable USB PHY clock and reset */

  reg = getreg32(F1C100S_CCU_USBPHY_CFG);
  reg |= CCU_USBPHY_SCLK_GATING | CCU_USBPHY_PHY_RST;
  putreg32(reg, F1C100S_CCU_USBPHY_CFG);

  /* 2. Enable USB OTG bus clock */

  reg = getreg32(F1C100S_CCU_BUS_CLK_GATING0);
  reg |= CCU_BUS_CLK_USBOTG;
  putreg32(reg, F1C100S_CCU_BUS_CLK_GATING0);

  /* 3. De-assert then assert USB OTG reset */

  reg = getreg32(F1C100S_CCU_BUS_SOFT_RST0);
  reg &= ~CCU_BUS_RST_USBOTG;
  putreg32(reg, F1C100S_CCU_BUS_SOFT_RST0);
  up_udelay(10);
  reg |= CCU_BUS_RST_USBOTG;
  putreg32(reg, F1C100S_CCU_BUS_SOFT_RST0);

  /* 4. Power up the analog PHY (clear SIDDQ) before any indirect PHY
   *    register write. Matches Linux mainline's suniv_f1c100s_cfg
   *    init order in sun4i_usb_phy_init() - without this the PHY can
   *    stay in its power-down state and D+/D- are not reliably
   *    driven, which would look like "host mode never enumerates
   *    anything" regardless of ISCR/ID/VBUS being set correctly.
   */

  reg = USB_GETREG32(USB_PHYCTL_OFFSET);
  reg &= ~USB_PHYCTL_SIDDQ;
  USB_PUTREG32(reg, USB_PHYCTL_OFFSET);

  /* 5. PHY configuration */

  f1c100s_phy_write(0x0c, 0x01, 1);
  f1c100s_phy_write(0x20, 0x14, 5);
  f1c100s_phy_write(0x2a, 0x03, 2);

  /* 6. Route USB FIFO to SRAM */

  reg = getreg32(F1C100S_SYS_CTRL1);
  reg |= 1;
  putreg32(reg, F1C100S_SYS_CTRL1);

  /* 7. Configure ISCR for Host mode */

  reg = USB_GETREG32(USB_ISCR_OFFSET);
  reg |= USB_ISCR_DPDM_PULLUP_EN;     /* Enable DP/DM pullup */
  reg |= USB_ISCR_ID_PULLUP_EN;       /* Enable ID pullup */
  reg = (reg & ~0xc000) | USB_ISCR_FORCE_ID_LOW;   /* Force ID low (Host) */
  reg = (reg & ~0x3000) | USB_ISCR_FORCE_VBUS_HIGH; /* Force VBUS high */
  USB_PUTREG32(reg, USB_ISCR_OFFSET);

  /* 8. Clear all interrupts */

  USB_PUTREG8(0, USB_BUS_IE_OFFSET);
  USB_PUTREG8(0xff, USB_BUS_IS_OFFSET);
  USB_PUTREG32(0, USB_EP_IE_OFFSET);
  USB_PUTREG32(0xffffffff, USB_EP_IS_OFFSET);

  /* 9. Initialize controller registers */

  USB_PUTREG8(0, USB_DEVCTL_OFFSET);
  USB_PUTREG8(0, USB_EP_IDX_OFFSET);
  USB_PUTREG8(0, USB_VEND0_OFFSET);

  /* 10. Start Host mode */

  USB_PUTREG8(USB_POWER_HSENAB | USB_POWER_SOFTCONN, USB_POWER_OFFSET);
  USB_PUTREG8(USB_DEVCTL_SESSION | USB_DEVCTL_HOSTREQ, USB_DEVCTL_OFFSET);
  up_udelay(100);
  USB_PUTREG8(USB_POWER_HSENAB | USB_POWER_SOFTCONN | USB_POWER_ISOUPDATE,
              USB_POWER_OFFSET);

  /* 11. Enable connection interrupt (only if GPIO detection not used) */

#ifndef CONFIG_F1C100S_USBHOST_VBUS_DETECT
  USB_PUTREG8(USB_INT_CONNECT | USB_INT_DISCONNECT, USB_BUS_IE_OFFSET);
#endif

  uinfo("USB Host PHY initialized\n");
}

/****************************************************************************
 * Name: f1c100s_usb_reset
 *
 * Description:
 *   Send USB bus reset to connected device.
 *
 ****************************************************************************/

static void f1c100s_usb_reset(void)
{
  uint8_t power;

  /* Assert reset */

  power = USB_GETREG8(USB_POWER_OFFSET);
  power |= USB_POWER_RESET;
  USB_PUTREG8(power, USB_POWER_OFFSET);

  /* Hold reset for 50ms (USB spec requires 10-20ms) */

  nxsig_usleep(50000);

  /* De-assert reset */

  power &= ~USB_POWER_RESET;
  USB_PUTREG8(power, USB_POWER_OFFSET);

  /* Wait for device to recover */

  nxsig_usleep(10000);

  uinfo("USB bus reset complete\n");
}

/****************************************************************************
 * Name: f1c100s_get_speed
 *
 * Description:
 *   Detect connected device speed.
 *
 ****************************************************************************/

static uint8_t f1c100s_get_speed(void)
{
  uint8_t devctl = USB_GETREG8(USB_DEVCTL_OFFSET);
  uint8_t power = USB_GETREG8(USB_POWER_OFFSET);

  if (power & USB_POWER_HSMODE)
    {
      uinfo("High-speed device detected\n");
      return USB_SPEED_HIGH;
    }
  else if (devctl & USB_DEVCTL_FSDEV)
    {
      uinfo("Full-speed device detected\n");
      return USB_SPEED_FULL;
    }
  else if (devctl & USB_DEVCTL_LSDEV)
    {
      uinfo("Low-speed device detected\n");
      return USB_SPEED_LOW;
    }

  uinfo("Unknown speed, assuming full-speed\n");
  return USB_SPEED_FULL;
}

/****************************************************************************
 * Name: f1c100s_usbhost_interrupt
 *
 * Description:
 *   USB Host interrupt handler.
 *
 ****************************************************************************/

static int f1c100s_usbhost_interrupt(int irq, void *context, void *arg)
{
  struct f1c100s_usbhost_s *priv = &g_usbhost;
  uint8_t bus_is;
  uint32_t ep_is;
#ifdef CONFIG_USBHOST_ASYNCH
  int epndx;
#endif

  /* Read and clear interrupt status */

  bus_is = USB_GETREG8(USB_BUS_IS_OFFSET);
  ep_is = USB_GETREG32(USB_EP_IS_OFFSET);

  USB_PUTREG8(bus_is, USB_BUS_IS_OFFSET);
  USB_PUTREG32(ep_is, USB_EP_IS_OFFSET);

  /* Device connected */

  if (bus_is & USB_INT_CONNECT)
    {
      uinfo("USB device connected\n");
      priv->connected = true;
      priv->change = true;
      nxsem_post(&priv->pscsem);
    }

  /* Device disconnected */

  if (bus_is & (USB_INT_DISCONNECT | USB_INT_VBUSERR))
    {
      uinfo("USB device disconnected\n");
      priv->connected = false;
      priv->change = true;
      nxsem_post(&priv->pscsem);
    }

#ifdef CONFIG_USBHOST_ASYNCH
  /* Process endpoint interrupts for async transfers */

  for (epndx = 1; epndx < F1C100S_USB_NENDPOINTS; epndx++)
    {
      struct f1c100s_ep_s *privep = &priv->ep[epndx];

      if (privep->callback == NULL)
        {
          continue;
        }

      /* Check RX interrupt (IN endpoint) */

      if ((ep_is & USB_EP_INT_RX(epndx)) && privep->in)
        {
          USB_PUTREG8(epndx, USB_EP_IDX_OFFSET);
          uint16_t rxcsr = USB_GETREG16(USB_RXCSR_OFFSET);

          if (rxcsr & USB_RXCSR_RXPKTRDY)
            {
              /* Read data from FIFO */

              uint16_t count = USB_GETREG16(USB_RXCOUNT_OFFSET);
              if (count > privep->buflen)
                {
                  count = privep->buflen;
                }

              if (count > 0 && privep->buffer != NULL)
                {
                  f1c100s_read_fifo(epndx, privep->buffer, count);
                }

              /* Clear RxPktRdy */

              rxcsr &= ~USB_RXCSR_RXPKTRDY;
              USB_PUTREG16(rxcsr, USB_RXCSR_OFFSET);

              /* Invoke callback */

              usbhost_asynch_t callback = privep->callback;
              void *cbarg = privep->arg;
              privep->callback = NULL;
              privep->arg = NULL;
              privep->buffer = NULL;
              privep->buflen = 0;

              callback(cbarg, count);
            }
        }

      /* Check TX interrupt (OUT endpoint) */

      if ((ep_is & USB_EP_INT_TX(epndx)) && !privep->in)
        {
          USB_PUTREG8(epndx, USB_EP_IDX_OFFSET);
          uint16_t txcsr = USB_GETREG16(USB_TXCSR_OFFSET);

          /* TX complete when TxPktRdy is cleared */

          if (!(txcsr & USB_TXCSR_TXPKTRDY))
            {
              /* Invoke callback with bytes sent */

              usbhost_asynch_t callback = privep->callback;
              void *cbarg = privep->arg;
              size_t sent = privep->buflen;
              privep->callback = NULL;
              privep->arg = NULL;
              privep->buffer = NULL;
              privep->buflen = 0;

              callback(cbarg, sent);
            }
        }
    }
#endif

  return OK;
}

/****************************************************************************
 * Connection Interface Methods
 ****************************************************************************/

/****************************************************************************
 * Name: f1c100s_wait
 *
 * Description:
 *   Wait for a device to connect or disconnect.
 *
 ****************************************************************************/

static int f1c100s_wait(struct usbhost_connection_s *conn,
                        struct usbhost_hubport_s **hport)
{
  struct f1c100s_usbhost_s *priv = &g_usbhost;
  irqstate_t flags;
  bool connected;
  bool change;
  int ret;

  /* Loop until a connection change is detected */

  for (; ; )
    {
      /* Wait for connection state change */

      ret = nxsem_wait(&priv->pscsem);
      if (ret < 0)
        {
          return ret;
        }

      /* Read flags with interrupts disabled */

      flags = enter_critical_section();
      change = priv->change;
      connected = priv->connected;
      if (change)
        {
          priv->change = false;
        }
      leave_critical_section(flags);

      /* Check if state actually changed */

      if (change)
        {
          /* Update root hub port status */

          priv->rhport.hport.connected = connected;

          if (connected)
            {
              /* Send USB reset and detect speed */

              nxmutex_lock(&priv->lock);
              f1c100s_usb_reset();
              priv->speed = f1c100s_get_speed();
              priv->rhport.hport.speed = priv->speed;
              nxmutex_unlock(&priv->lock);
            }

          *hport = &priv->rhport.hport;
          return OK;
        }
    }
}

/****************************************************************************
 * Name: f1c100s_enumerate
 *
 * Description:
 *   Enumerate the connected device.
 *
 ****************************************************************************/

static int f1c100s_enumerate(struct usbhost_connection_s *conn,
                             struct usbhost_hubport_s *hport)
{
  /* Call NuttX enumeration logic */

  return usbhost_enumerate(hport, &hport->devclass);
}

/****************************************************************************
 * Driver Interface Methods - Stubs for Phase 1
 ****************************************************************************/

static int f1c100s_ep0configure(struct usbhost_driver_s *drvr,
                                usbhost_ep_t ep0, uint8_t funcaddr,
                                uint8_t speed, uint16_t maxpacketsize)
{
  struct f1c100s_usbhost_s *priv = (struct f1c100s_usbhost_s *)drvr;
  struct f1c100s_ep_s *ep = (struct f1c100s_ep_s *)ep0;

  DEBUGASSERT(priv != NULL && ep != NULL);

  /* Configure EP0 for the target device */

  ep->funcaddr = funcaddr;
  ep->maxpacket = maxpacketsize;

  /* Set target device address */

  USB_PUTREG8(0, USB_EP_IDX_OFFSET);
  USB_PUTREG8(funcaddr, USB_TXFUNCADDR_OFFSET);

  /* Set speed in TXTYPE register */

  uint8_t txtype = 0;
  switch (speed)
    {
      case USB_SPEED_LOW:
        txtype = 0xc0;  /* Low-speed */
        break;
      case USB_SPEED_FULL:
        txtype = 0x80;  /* Full-speed */
        break;
      case USB_SPEED_HIGH:
        txtype = 0x40;  /* High-speed */
        break;
    }
  USB_PUTREG8(txtype, USB_TXTYPE_OFFSET);

  uinfo("EP0 configured: addr=%d speed=%d maxpkt=%d\n",
        funcaddr, speed, maxpacketsize);

  return OK;
}

static int f1c100s_epalloc(struct usbhost_driver_s *drvr,
                           const struct usbhost_epdesc_s *epdesc,
                           usbhost_ep_t *ep)
{
  struct f1c100s_usbhost_s *priv = (struct f1c100s_usbhost_s *)drvr;
  struct f1c100s_ep_s *privep;
  int epno;

  DEBUGASSERT(priv != NULL && epdesc != NULL && ep != NULL);

  uinfo("EP alloc: addr=%02x in=%d type=%d interval=%d maxpkt=%d\n",
        epdesc->addr, epdesc->in, epdesc->xfrtype,
        epdesc->interval, epdesc->mxpacketsize);

  /* Find a free endpoint (skip EP0 which is reserved for control) */

  nxmutex_lock(&priv->lock);

  for (epno = 1; epno < F1C100S_USB_NENDPOINTS; epno++)
    {
      if (priv->ep[epno].xfrtype == 0)
        {
          break;
        }
    }

  if (epno >= F1C100S_USB_NENDPOINTS)
    {
      uerr("No free endpoints\n");
      nxmutex_unlock(&priv->lock);
      return -ENOMEM;
    }

  /* Configure the endpoint */

  privep = &priv->ep[epno];
  privep->epno = epdesc->addr & 0x0f;
  privep->in = epdesc->in;
  privep->xfrtype = epdesc->xfrtype;
  privep->maxpacket = epdesc->mxpacketsize;
  privep->interval = epdesc->interval;
  privep->funcaddr = epdesc->hport->funcaddr;

  /* Configure hardware endpoint */

  USB_PUTREG8(epno, USB_EP_IDX_OFFSET);

  /* Set target function address */

  USB_PUTREG8(privep->funcaddr, USB_TXFUNCADDR_OFFSET);

  /* Configure TXTYPE/RXTYPE based on direction and transfer type */

  uint8_t type = 0;

  /* Set speed bits [7:6] */

  switch (epdesc->hport->speed)
    {
      case USB_SPEED_LOW:
        type = 0xc0;
        break;
      case USB_SPEED_FULL:
        type = 0x80;
        break;
      case USB_SPEED_HIGH:
        type = 0x40;
        break;
    }

  /* Set protocol bits [5:4] */

  switch (epdesc->xfrtype)
    {
      case USB_EP_ATTR_XFER_BULK:
        type |= (2 << 4);  /* Bulk */
        break;
      case USB_EP_ATTR_XFER_INT:
        type |= (3 << 4);  /* Interrupt */
        break;
      case USB_EP_ATTR_XFER_ISOC:
        type |= (1 << 4);  /* Isochronous */
        break;
    }

  /* Set target endpoint number [3:0] */

  type |= (privep->epno & 0x0f);

  if (epdesc->in)
    {
      /* IN endpoint - configure RX side */

      USB_PUTREG8(type, USB_RXTYPE_OFFSET);
      USB_PUTREG8(epdesc->interval, USB_RXINTERVAL_OFFSET);
      USB_PUTREG16(privep->maxpacket, USB_RXMAXP_OFFSET);

      /* Configure RX FIFO - 512 bytes per endpoint (matches Linux musb/sunxi.c) */

      USB_PUTREG8(USB_FIFOSZ_512, USB_RXFIFOSZ_OFFSET);
      USB_PUTREG16((64 + 512 * 5 + (epno - 1) * 512) / 8, USB_RXFIFOADDR_OFFSET);

      /* Clear data toggle */

      uint16_t rxcsr = USB_GETREG16(USB_RXCSR_OFFSET);
      rxcsr |= USB_RXCSR_CLRDATATOG;
      USB_PUTREG16(rxcsr, USB_RXCSR_OFFSET);
    }
  else
    {
      /* OUT endpoint - configure TX side */

      USB_PUTREG8(type, USB_TXTYPE_OFFSET);
      USB_PUTREG8(epdesc->interval, USB_TXINTERVAL_OFFSET);
      USB_PUTREG16(privep->maxpacket, USB_TXMAXP_OFFSET);

      /* Configure TX FIFO - 512 bytes per endpoint (matches Linux musb/sunxi.c) */

      USB_PUTREG8(USB_FIFOSZ_512, USB_TXFIFOSZ_OFFSET);
      USB_PUTREG16((64 + (epno - 1) * 512) / 8, USB_TXFIFOADDR_OFFSET);

      /* Clear data toggle */

      uint16_t txcsr = USB_GETREG16(USB_TXCSR_OFFSET);
      txcsr |= USB_TXCSR_CLRDATATOG;
      USB_PUTREG16(txcsr, USB_TXCSR_OFFSET);
    }

  nxmutex_unlock(&priv->lock);

  *ep = (usbhost_ep_t)privep;
  uinfo("EP%d allocated for %s\n", epno, epdesc->in ? "IN" : "OUT");

  return OK;
}

static int f1c100s_epfree(struct usbhost_driver_s *drvr, usbhost_ep_t ep)
{
  struct f1c100s_usbhost_s *priv = (struct f1c100s_usbhost_s *)drvr;
  struct f1c100s_ep_s *privep = (struct f1c100s_ep_s *)ep;

  DEBUGASSERT(priv != NULL && privep != NULL);

  uinfo("EP free: epno=%d\n", privep->epno);

  nxmutex_lock(&priv->lock);

  /* Clear endpoint configuration */

  privep->epno = 0;
  privep->in = false;
  privep->xfrtype = 0;
  privep->maxpacket = 0;
  privep->interval = 0;
  privep->funcaddr = 0;

#ifdef CONFIG_USBHOST_ASYNCH
  privep->callback = NULL;
  privep->arg = NULL;
  privep->buffer = NULL;
  privep->buflen = 0;
#endif

  nxmutex_unlock(&priv->lock);

  return OK;
}

static int f1c100s_alloc(struct usbhost_driver_s *drvr,
                         uint8_t **buffer, size_t *maxlen)
{
  DEBUGASSERT(buffer != NULL && maxlen != NULL);

  *maxlen = 256;
  *buffer = kmm_malloc(*maxlen);

  return *buffer ? OK : -ENOMEM;
}

static int f1c100s_free(struct usbhost_driver_s *drvr, uint8_t *buffer)
{
  DEBUGASSERT(buffer != NULL);
  kmm_free(buffer);
  return OK;
}

static int f1c100s_ioalloc(struct usbhost_driver_s *drvr,
                           uint8_t **buffer, size_t buflen)
{
  DEBUGASSERT(buffer != NULL && buflen > 0);

  *buffer = kmm_malloc(buflen);
  return *buffer ? OK : -ENOMEM;
}

static int f1c100s_iofree(struct usbhost_driver_s *drvr, uint8_t *buffer)
{
  DEBUGASSERT(buffer != NULL);
  kmm_free(buffer);
  return OK;
}

/****************************************************************************
 * Name: f1c100s_wait_csr
 *
 * Description:
 *   Wait for CSR register to match expected value.
 *
 ****************************************************************************/

static int f1c100s_wait_csr(uint8_t ep, uint16_t mask, uint16_t expected,
                            uint32_t timeout_ticks)
{
  clock_t start = clock_systime_ticks();
  clock_t elapsed;
  uint16_t csr;

  USB_PUTREG8(ep, USB_EP_IDX_OFFSET);

  while (1)
    {
      if (ep == 0)
        {
          csr = USB_GETREG16(USB_TXCSR_OFFSET);

          /* Check for errors */

          if (csr & USB_CSR0_H_RXSTALL)
            {
              uerr("EP0 STALL received\n");
              USB_PUTREG16(csr & ~USB_CSR0_H_RXSTALL, USB_TXCSR_OFFSET);
              return -EPERM;
            }

          if (csr & USB_CSR0_H_ERROR)
            {
              uerr("EP0 error\n");
              USB_PUTREG16(csr & ~USB_CSR0_H_ERROR, USB_TXCSR_OFFSET);
              return -EIO;
            }

          if (csr & USB_CSR0_H_NAKTIMEOUT)
            {
              uerr("EP0 NAK timeout\n");
              USB_PUTREG16(csr & ~USB_CSR0_H_NAKTIMEOUT, USB_TXCSR_OFFSET);
              return -ETIMEDOUT;
            }
        }
      else
        {
          /* For non-EP0, check TX or RX CSR based on direction */

          csr = USB_GETREG16(USB_TXCSR_OFFSET);
        }

      /* Check if condition met */

      if ((csr & mask) == expected)
        {
          return OK;
        }

      /* Check timeout */

      elapsed = clock_systime_ticks() - start;
      if (elapsed >= timeout_ticks)
        {
          uerr("CSR wait timeout: csr=0x%04x mask=0x%04x expected=0x%04x\n",
               csr, mask, expected);
          return -ETIMEDOUT;
        }

      /* Small delay to avoid busy-waiting */

      up_udelay(10);
    }
}

/****************************************************************************
 * Name: f1c100s_wait_txcsr
 *
 * Description:
 *   Wait for TXCSR register to match expected value (for non-EP0).
 *
 ****************************************************************************/

static int f1c100s_wait_txcsr(uint8_t ep, uint16_t mask, uint16_t expected,
                              uint32_t timeout_ticks)
{
  clock_t start = clock_systime_ticks();
  clock_t elapsed;
  uint16_t txcsr;

  USB_PUTREG8(ep, USB_EP_IDX_OFFSET);

  while (1)
    {
      txcsr = USB_GETREG16(USB_TXCSR_OFFSET);

      /* Check for errors */

      if (txcsr & USB_TXCSR_H_RXSTALL)
        {
          uerr("TX STALL received\n");
          USB_PUTREG16(txcsr & ~USB_TXCSR_H_RXSTALL, USB_TXCSR_OFFSET);
          return -EPERM;
        }

      if (txcsr & USB_TXCSR_H_ERROR)
        {
          uerr("TX error\n");
          USB_PUTREG16(txcsr & ~USB_TXCSR_H_ERROR, USB_TXCSR_OFFSET);
          return -EIO;
        }

      if (txcsr & USB_TXCSR_H_NAKTIMEOUT)
        {
          uerr("TX NAK timeout\n");
          USB_PUTREG16(txcsr & ~USB_TXCSR_H_NAKTIMEOUT, USB_TXCSR_OFFSET);
          return -ETIMEDOUT;
        }

      /* Check if condition met */

      if ((txcsr & mask) == expected)
        {
          return OK;
        }

      /* Check timeout */

      elapsed = clock_systime_ticks() - start;
      if (elapsed >= timeout_ticks)
        {
          uerr("TXCSR wait timeout\n");
          return -ETIMEDOUT;
        }

      up_udelay(10);
    }
}

/****************************************************************************
 * Name: f1c100s_wait_rxcsr
 *
 * Description:
 *   Wait for RXCSR register to match expected value (for non-EP0).
 *
 ****************************************************************************/

static int f1c100s_wait_rxcsr(uint8_t ep, uint16_t mask, uint16_t expected,
                              uint32_t timeout_ticks)
{
  clock_t start = clock_systime_ticks();
  clock_t elapsed;
  uint16_t rxcsr;

  USB_PUTREG8(ep, USB_EP_IDX_OFFSET);

  while (1)
    {
      rxcsr = USB_GETREG16(USB_RXCSR_OFFSET);

      /* Check for errors */

      if (rxcsr & USB_RXCSR_H_RXSTALL)
        {
          uerr("RX STALL received\n");
          USB_PUTREG16(rxcsr & ~USB_RXCSR_H_RXSTALL, USB_RXCSR_OFFSET);
          return -EPERM;
        }

      if (rxcsr & USB_RXCSR_H_ERROR)
        {
          uerr("RX error\n");
          USB_PUTREG16(rxcsr & ~USB_RXCSR_H_ERROR, USB_RXCSR_OFFSET);
          return -EIO;
        }

      if (rxcsr & USB_RXCSR_H_NAKTIMEOUT)
        {
          uerr("RX NAK timeout\n");
          USB_PUTREG16(rxcsr & ~USB_RXCSR_H_NAKTIMEOUT, USB_RXCSR_OFFSET);
          return -ETIMEDOUT;
        }

      /* Check if condition met */

      if ((rxcsr & mask) == expected)
        {
          return OK;
        }

      /* Check timeout */

      elapsed = clock_systime_ticks() - start;
      if (elapsed >= timeout_ticks)
        {
          uerr("RXCSR wait timeout\n");
          return -ETIMEDOUT;
        }

      up_udelay(10);
    }
}

/****************************************************************************
 * Name: f1c100s_read_fifo
 *
 * Description:
 *   Read data from endpoint FIFO.
 *
 ****************************************************************************/

static void f1c100s_read_fifo(uint8_t ep, uint8_t *buffer, size_t len)
{
  volatile uint32_t *fifo = (volatile uint32_t *)(F1C100S_USB_BASE +
                                                   USB_FIFO_OFFSET(ep));
  size_t i;

  /* Read 32-bit words (only if buffer is aligned) */

  for (i = 0; i + 3 < len; i += 4)
    {
      uint32_t tmp = *fifo;
      if (((uintptr_t)(buffer + i) & 3) == 0)
        {
          *(uint32_t *)(buffer + i) = tmp;
        }
      else
        {
          buffer[i] = tmp & 0xff;
          buffer[i + 1] = (tmp >> 8) & 0xff;
          buffer[i + 2] = (tmp >> 16) & 0xff;
          buffer[i + 3] = (tmp >> 24) & 0xff;
        }
    }

  /* Read remaining bytes */

  if (i < len)
    {
      uint32_t tmp = *fifo;
      while (i < len)
        {
          buffer[i++] = tmp & 0xff;
          tmp >>= 8;
        }
    }
}

/****************************************************************************
 * Name: f1c100s_write_fifo
 *
 * Description:
 *   Write data to endpoint FIFO.
 *
 ****************************************************************************/

static void f1c100s_write_fifo(uint8_t ep, const uint8_t *buffer, size_t len)
{
  size_t i = 0;

  /* Write 32-bit words */

  while (i + 4 <= len)
    {
      uint32_t tmp = (uint32_t)buffer[i] |
                     ((uint32_t)buffer[i + 1] << 8) |
                     ((uint32_t)buffer[i + 2] << 16) |
                     ((uint32_t)buffer[i + 3] << 24);

      USB_PUTREG32(tmp, USB_FIFO_OFFSET(ep));
      i += 4;
    }

  /* Tail: byte writes only. A 4-byte MUSB FIFO write always advances the
   * FIFO write pointer by a full word - padding a 1-3 byte tail up to
   * 4 bytes via USB_PUTREG32 would make the packet appear longer to the
   * peer than it actually is.
   */

  for (; i < len; i++)
    {
      USB_PUTREG8(buffer[i], USB_FIFO_OFFSET(ep));
    }
}

/****************************************************************************
 * Name: f1c100s_ctrlin
 *
 * Description:
 *   Process a control IN transfer (device to host).
 *   Three phases: SETUP -> DATA IN -> STATUS OUT
 *
 ****************************************************************************/

static int f1c100s_ctrlin(struct usbhost_driver_s *drvr, usbhost_ep_t ep0,
                          const struct usb_ctrlreq_s *req, uint8_t *buffer)
{
  struct f1c100s_usbhost_s *priv = (struct f1c100s_usbhost_s *)drvr;
  uint16_t len = GETUINT16(req->len);
  uint16_t received = 0;
  uint16_t count;
  int ret;

  DEBUGASSERT(priv != NULL && req != NULL);

  nxmutex_lock(&priv->lock);

  uinfo("CTRL IN: type=%02x req=%02x value=%04x index=%04x len=%d\n",
        req->type, req->req, GETUINT16(req->value),
        GETUINT16(req->index), len);

  /* Select EP0 */

  USB_PUTREG8(0, USB_EP_IDX_OFFSET);

  /* === SETUP Phase === */

  /* Write 8-byte SETUP packet to FIFO */

  f1c100s_write_fifo(0, (const uint8_t *)req, 8);

  /* Send SETUP packet: SetupPkt | TxPktRdy */

  USB_PUTREG16(USB_CSR0_TXPKTRDY | USB_CSR0_H_SETUPPKT, USB_TXCSR_OFFSET);

  /* Wait for SETUP to complete (TxPktRdy cleared) */

  ret = f1c100s_wait_csr(0, USB_CSR0_TXPKTRDY, 0, CTRL_TIMEOUT);
  if (ret < 0)
    {
      uerr("SETUP phase failed: %d\n", ret);
      goto errout;
    }

  /* === DATA IN Phase === */

  while (received < len)
    {
      /* Request IN packet */

      USB_PUTREG16(USB_CSR0_H_REQPKT, USB_TXCSR_OFFSET);

      /* Wait for RxPktRdy */

      ret = f1c100s_wait_csr(0, USB_CSR0_RXPKTRDY, USB_CSR0_RXPKTRDY,
                             CTRL_TIMEOUT);
      if (ret < 0)
        {
          uerr("DATA IN phase failed: %d\n", ret);
          goto errout;
        }

      /* Read received data count */

      count = USB_GETREG16(USB_RXCOUNT_OFFSET);
      if (count > (len - received))
        {
          count = len - received;
        }

      /* Read data from FIFO */

      if (count > 0 && buffer != NULL)
        {
          f1c100s_read_fifo(0, buffer + received, count);
        }

      received += count;

      /* Clear RxPktRdy */

      USB_PUTREG16(0, USB_TXCSR_OFFSET);

      /* Short packet means end of transfer */

      if (count < F1C100S_EP0_MAXPACKET)
        {
          break;
        }
    }

  /* === STATUS OUT Phase === */

  /* Send zero-length OUT packet: StatusPkt | TxPktRdy */

  USB_PUTREG16(USB_CSR0_TXPKTRDY | USB_CSR0_H_STATUSPKT, USB_TXCSR_OFFSET);

  /* Wait for STATUS to complete */

  ret = f1c100s_wait_csr(0, USB_CSR0_TXPKTRDY, 0, CTRL_TIMEOUT);
  if (ret < 0)
    {
      uerr("STATUS phase failed: %d\n", ret);
      goto errout;
    }

  uinfo("CTRL IN complete: received %d bytes\n", received);
  nxmutex_unlock(&priv->lock);
  return received;

errout:
  /* Clear any error status */

  USB_PUTREG16(0, USB_TXCSR_OFFSET);
  nxmutex_unlock(&priv->lock);
  return ret;
}

/****************************************************************************
 * Name: f1c100s_ctrlout
 *
 * Description:
 *   Process a control OUT transfer (host to device).
 *   Three phases: SETUP -> DATA OUT -> STATUS IN
 *
 ****************************************************************************/

static int f1c100s_ctrlout(struct usbhost_driver_s *drvr, usbhost_ep_t ep0,
                           const struct usb_ctrlreq_s *req,
                           const uint8_t *buffer)
{
  struct f1c100s_usbhost_s *priv = (struct f1c100s_usbhost_s *)drvr;
  uint16_t len = GETUINT16(req->len);
  uint16_t sent = 0;
  uint16_t count;
  int ret;

  DEBUGASSERT(priv != NULL && req != NULL);

  nxmutex_lock(&priv->lock);

  uinfo("CTRL OUT: type=%02x req=%02x value=%04x index=%04x len=%d\n",
        req->type, req->req, GETUINT16(req->value),
        GETUINT16(req->index), len);

  /* Select EP0 */

  USB_PUTREG8(0, USB_EP_IDX_OFFSET);

  /* === SETUP Phase === */

  /* Write 8-byte SETUP packet to FIFO */

  f1c100s_write_fifo(0, (const uint8_t *)req, 8);

  /* Send SETUP packet: SetupPkt | TxPktRdy */

  USB_PUTREG16(USB_CSR0_TXPKTRDY | USB_CSR0_H_SETUPPKT, USB_TXCSR_OFFSET);

  /* Wait for SETUP to complete */

  ret = f1c100s_wait_csr(0, USB_CSR0_TXPKTRDY, 0, CTRL_TIMEOUT);
  if (ret < 0)
    {
      uerr("SETUP phase failed: %d\n", ret);
      goto errout;
    }

  /* === DATA OUT Phase === */

  while (sent < len)
    {
      /* Calculate bytes to send in this packet */

      count = len - sent;
      if (count > F1C100S_EP0_MAXPACKET)
        {
          count = F1C100S_EP0_MAXPACKET;
        }

      /* Write data to FIFO */

      if (count > 0 && buffer != NULL)
        {
          f1c100s_write_fifo(0, buffer + sent, count);
        }

      sent += count;

      /* Send packet: TxPktRdy */

      USB_PUTREG16(USB_CSR0_TXPKTRDY, USB_TXCSR_OFFSET);

      /* Wait for packet to be sent */

      ret = f1c100s_wait_csr(0, USB_CSR0_TXPKTRDY, 0, CTRL_TIMEOUT);
      if (ret < 0)
        {
          uerr("DATA OUT phase failed: %d\n", ret);
          goto errout;
        }
    }

  /* === STATUS IN Phase === */

  /* Request IN packet for status: StatusPkt | ReqPkt */

  USB_PUTREG16(USB_CSR0_H_STATUSPKT | USB_CSR0_H_REQPKT, USB_TXCSR_OFFSET);

  /* Wait for RxPktRdy (status received) */

  ret = f1c100s_wait_csr(0, USB_CSR0_RXPKTRDY, USB_CSR0_RXPKTRDY,
                         CTRL_TIMEOUT);
  if (ret < 0)
    {
      uerr("STATUS phase failed: %d\n", ret);
      goto errout;
    }

  /* Clear RxPktRdy */

  USB_PUTREG16(0, USB_TXCSR_OFFSET);

  uinfo("CTRL OUT complete: sent %d bytes\n", sent);
  nxmutex_unlock(&priv->lock);
  return sent;

errout:
  /* Clear any error status */

  USB_PUTREG16(0, USB_TXCSR_OFFSET);
  nxmutex_unlock(&priv->lock);
  return ret;
}

/****************************************************************************
 * Name: f1c100s_in_transfer
 *
 * Description:
 *   Perform a bulk/interrupt IN transfer.
 *
 ****************************************************************************/

static ssize_t f1c100s_in_transfer(struct f1c100s_usbhost_s *priv,
                                   struct f1c100s_ep_s *ep,
                                   uint8_t *buffer, size_t buflen)
{
  size_t received = 0;
  uint16_t count;
  uint16_t rxcsr;
  int ret;
  int epndx;

  /* Find hardware endpoint index for this endpoint */

  for (epndx = 1; epndx < F1C100S_USB_NENDPOINTS; epndx++)
    {
      if (&priv->ep[epndx] == ep)
        {
          break;
        }
    }

  if (epndx >= F1C100S_USB_NENDPOINTS)
    {
      return -EINVAL;
    }

#ifdef CONFIG_F1C100S_USB_DMA
  /* Try DMA for large transfers */

  if (buflen >= CONFIG_F1C100S_USB_DMA_THRESHOLD)
    {
      USB_PUTREG8(epndx, USB_EP_IDX_OFFSET);

      /* Request IN packet first */

      rxcsr = USB_GETREG16(USB_RXCSR_OFFSET);
      rxcsr |= USB_RXCSR_H_REQPKT;
      USB_PUTREG16(rxcsr, USB_RXCSR_OFFSET);

      /* Start DMA */

      ret = f1c100s_usb_dma_start(epndx, USB_DMA_DIR_RX, buffer, buflen,
                                  NULL, NULL);
      if (ret == OK)
        {
          ret = f1c100s_usb_dma_wait(epndx, 5000);  /* 5 second timeout */
          return ret;
        }

      /* DMA failed, clear REQPKT before falling through to PIO */

      rxcsr = USB_GETREG16(USB_RXCSR_OFFSET);
      rxcsr &= ~USB_RXCSR_H_REQPKT;
      USB_PUTREG16(rxcsr, USB_RXCSR_OFFSET);
    }
#endif

  USB_PUTREG8(epndx, USB_EP_IDX_OFFSET);

  while (received < buflen)
    {
      /* Request IN packet */

      rxcsr = USB_GETREG16(USB_RXCSR_OFFSET);
      rxcsr |= USB_RXCSR_H_REQPKT;
      USB_PUTREG16(rxcsr, USB_RXCSR_OFFSET);

      /* Wait for RxPktRdy */

      ret = f1c100s_wait_rxcsr(epndx, USB_RXCSR_RXPKTRDY, USB_RXCSR_RXPKTRDY,
                               BULK_TIMEOUT);
      if (ret < 0)
        {
          uerr("IN transfer failed: %d\n", ret);
          return ret;
        }

      /* Read received data count */

      count = USB_GETREG16(USB_RXCOUNT_OFFSET);
      if (count > (buflen - received))
        {
          count = buflen - received;
        }

      /* Read data from FIFO */

      if (count > 0)
        {
          f1c100s_read_fifo(epndx, buffer + received, count);
        }

      received += count;

      /* Clear RxPktRdy */

      rxcsr = USB_GETREG16(USB_RXCSR_OFFSET);
      rxcsr &= ~USB_RXCSR_RXPKTRDY;
      USB_PUTREG16(rxcsr, USB_RXCSR_OFFSET);

      /* Short packet means end of transfer */

      if (count < ep->maxpacket)
        {
          break;
        }
    }

  return received;
}

/****************************************************************************
 * Name: f1c100s_out_transfer
 *
 * Description:
 *   Perform a bulk/interrupt OUT transfer.
 *
 ****************************************************************************/

static ssize_t f1c100s_out_transfer(struct f1c100s_usbhost_s *priv,
                                    struct f1c100s_ep_s *ep,
                                    uint8_t *buffer, size_t buflen)
{
  size_t sent = 0;
  uint16_t count;
  uint16_t txcsr;
  int ret;
  int epndx;

  /* Find hardware endpoint index for this endpoint */

  for (epndx = 1; epndx < F1C100S_USB_NENDPOINTS; epndx++)
    {
      if (&priv->ep[epndx] == ep)
        {
          break;
        }
    }

  if (epndx >= F1C100S_USB_NENDPOINTS)
    {
      return -EINVAL;
    }

#ifdef CONFIG_F1C100S_USB_DMA
  /* Try DMA for large transfers */

  if (buflen >= CONFIG_F1C100S_USB_DMA_THRESHOLD)
    {
      ret = f1c100s_usb_dma_start(epndx, USB_DMA_DIR_TX, buffer, buflen,
                                  NULL, NULL);
      if (ret == OK)
        {
          ret = f1c100s_usb_dma_wait(epndx, 5000);  /* 5 second timeout */
          return ret;
        }

      /* DMA failed, fall through to PIO */
    }
#endif

  USB_PUTREG8(epndx, USB_EP_IDX_OFFSET);

  while (sent < buflen)
    {
      /* Calculate bytes to send in this packet */

      count = buflen - sent;
      if (count > ep->maxpacket)
        {
          count = ep->maxpacket;
        }

      /* Write data to FIFO */

      f1c100s_write_fifo(epndx, buffer + sent, count);
      sent += count;

      /* Set TxPktRdy */

      txcsr = USB_GETREG16(USB_TXCSR_OFFSET);
      txcsr |= USB_TXCSR_TXPKTRDY;
      USB_PUTREG16(txcsr, USB_TXCSR_OFFSET);

      /* Wait for TxPktRdy to clear */

      ret = f1c100s_wait_txcsr(epndx, USB_TXCSR_TXPKTRDY, 0, BULK_TIMEOUT);
      if (ret < 0)
        {
          uerr("OUT transfer failed: %d\n", ret);
          return ret;
        }
    }

  return sent;
}

/****************************************************************************
 * Name: f1c100s_transfer
 *
 * Description:
 *   Process a bulk or interrupt transfer request.
 *
 ****************************************************************************/

static ssize_t f1c100s_transfer(struct usbhost_driver_s *drvr,
                                usbhost_ep_t ep, uint8_t *buffer,
                                size_t buflen)
{
  struct f1c100s_usbhost_s *priv = (struct f1c100s_usbhost_s *)drvr;
  struct f1c100s_ep_s *privep = (struct f1c100s_ep_s *)ep;
  ssize_t ret;

  DEBUGASSERT(priv != NULL && privep != NULL);

  uinfo("Transfer: ep=%d in=%d len=%zu\n", privep->epno, privep->in, buflen);

  nxmutex_lock(&priv->lock);

  if (privep->in)
    {
      ret = f1c100s_in_transfer(priv, privep, buffer, buflen);
    }
  else
    {
      ret = f1c100s_out_transfer(priv, privep, buffer, buflen);
    }

  nxmutex_unlock(&priv->lock);

  if (ret >= 0)
    {
      uinfo("Transfer complete: %zd bytes\n", ret);
    }

  return ret;
}

#ifdef CONFIG_USBHOST_ASYNCH
/****************************************************************************
 * Name: f1c100s_asynch
 *
 * Description:
 *   Process a request to handle a transfer asynchronously. This method
 *   will enqueue the transfer request and return immediately. The callback
 *   will be invoked when the transfer completes.
 *
 ****************************************************************************/

static int f1c100s_asynch(struct usbhost_driver_s *drvr, usbhost_ep_t ep,
                          uint8_t *buffer, size_t buflen,
                          usbhost_asynch_t callback, void *arg)
{
  struct f1c100s_usbhost_s *priv = (struct f1c100s_usbhost_s *)drvr;
  struct f1c100s_ep_s *privep = (struct f1c100s_ep_s *)ep;
  int epndx;

  DEBUGASSERT(priv != NULL && privep != NULL && callback != NULL);

  uinfo("Asynch: ep=%d in=%d len=%zu\n", privep->epno, privep->in, buflen);

  nxmutex_lock(&priv->lock);

  /* Find hardware endpoint index */

  for (epndx = 1; epndx < F1C100S_USB_NENDPOINTS; epndx++)
    {
      if (&priv->ep[epndx] == privep)
        {
          break;
        }
    }

  if (epndx >= F1C100S_USB_NENDPOINTS)
    {
      nxmutex_unlock(&priv->lock);
      return -EINVAL;
    }

  /* Check if there's already a pending async transfer */

  if (privep->callback != NULL)
    {
      uerr("Async transfer already pending\n");
      nxmutex_unlock(&priv->lock);
      return -EBUSY;
    }

  /* Save async transfer parameters */

  privep->callback = callback;
  privep->arg = arg;
  privep->buffer = buffer;
  privep->buflen = buflen;

  /* Configure hardware endpoint */

  USB_PUTREG8(epndx, USB_EP_IDX_OFFSET);

  if (privep->in)
    {
      /* Enable RX interrupt for this endpoint */

      uint32_t ep_ie = USB_GETREG32(USB_EP_IE_OFFSET);
      ep_ie |= USB_EP_INT_RX(epndx);
      USB_PUTREG32(ep_ie, USB_EP_IE_OFFSET);

      /* Request first IN packet */

      uint16_t rxcsr = USB_GETREG16(USB_RXCSR_OFFSET);
      rxcsr |= USB_RXCSR_H_REQPKT;
      USB_PUTREG16(rxcsr, USB_RXCSR_OFFSET);
    }
  else
    {
      /* Enable TX interrupt for this endpoint */

      uint32_t ep_ie = USB_GETREG32(USB_EP_IE_OFFSET);
      ep_ie |= USB_EP_INT_TX(epndx);
      USB_PUTREG32(ep_ie, USB_EP_IE_OFFSET);

      /* Start sending data */

      uint16_t count = buflen;
      if (count > privep->maxpacket)
        {
          count = privep->maxpacket;
        }

      f1c100s_write_fifo(epndx, buffer, count);

      uint16_t txcsr = USB_GETREG16(USB_TXCSR_OFFSET);
      txcsr |= USB_TXCSR_TXPKTRDY;
      USB_PUTREG16(txcsr, USB_TXCSR_OFFSET);
    }

  nxmutex_unlock(&priv->lock);
  return OK;
}
#endif

/****************************************************************************
 * Name: f1c100s_cancel
 *
 * Description:
 *   Cancel a pending transfer on an endpoint.
 *
 ****************************************************************************/

static int f1c100s_cancel(struct usbhost_driver_s *drvr, usbhost_ep_t ep)
{
  struct f1c100s_usbhost_s *priv = (struct f1c100s_usbhost_s *)drvr;
  struct f1c100s_ep_s *privep = (struct f1c100s_ep_s *)ep;
  int epndx;
#ifdef CONFIG_USBHOST_ASYNCH
  usbhost_asynch_t callback;
  void *arg;
#endif

  DEBUGASSERT(priv != NULL && privep != NULL);

  uinfo("Cancel: ep=%d\n", privep->epno);

  nxmutex_lock(&priv->lock);

  /* Find hardware endpoint index */

  for (epndx = 1; epndx < F1C100S_USB_NENDPOINTS; epndx++)
    {
      if (&priv->ep[epndx] == privep)
        {
          break;
        }
    }

  if (epndx >= F1C100S_USB_NENDPOINTS)
    {
      nxmutex_unlock(&priv->lock);
      return -EINVAL;
    }

  /* Disable endpoint interrupts */

  USB_PUTREG8(epndx, USB_EP_IDX_OFFSET);

  uint32_t ep_ie = USB_GETREG32(USB_EP_IE_OFFSET);
  ep_ie &= ~(USB_EP_INT_TX(epndx) | USB_EP_INT_RX(epndx));
  USB_PUTREG32(ep_ie, USB_EP_IE_OFFSET);

  /* Flush the FIFO */

  if (privep->in)
    {
      uint16_t rxcsr = USB_GETREG16(USB_RXCSR_OFFSET);
      rxcsr |= USB_RXCSR_FLUSHFIFO;
      USB_PUTREG16(rxcsr, USB_RXCSR_OFFSET);
    }
  else
    {
      uint16_t txcsr = USB_GETREG16(USB_TXCSR_OFFSET);
      txcsr |= USB_TXCSR_FLUSHFIFO;
      USB_PUTREG16(txcsr, USB_TXCSR_OFFSET);
    }

#ifdef CONFIG_USBHOST_ASYNCH
  /* Save and clear callback */

  callback = privep->callback;
  arg = privep->arg;

  privep->callback = NULL;
  privep->arg = NULL;
  privep->buffer = NULL;
  privep->buflen = 0;

  nxmutex_unlock(&priv->lock);

  /* Invoke callback with -ESHUTDOWN */

  if (callback != NULL)
    {
      callback(arg, -ESHUTDOWN);
    }
#else
  nxmutex_unlock(&priv->lock);
#endif

  return OK;
}

#ifdef CONFIG_USBHOST_HUB
/****************************************************************************
 * Name: f1c100s_connect
 *
 * Description:
 *   New connections may be detected by an attached hub. This method is the
 *   mechanism that is used by the hub class to introduce a new connection
 *   and port description to the system.
 *
 ****************************************************************************/

static int f1c100s_connect(struct usbhost_driver_s *drvr,
                           struct usbhost_hubport_s *hport, bool connected)
{
  struct f1c100s_usbhost_s *priv = (struct f1c100s_usbhost_s *)drvr;

  DEBUGASSERT(priv != NULL && hport != NULL);

  uinfo("Hub port %d %s\n", hport->port,
        connected ? "connected" : "disconnected");

  /* Update the hub port connection status */

  hport->connected = connected;

  /* Signal the connection change to wake up any waiting thread */

  priv->change = true;
  nxsem_post(&priv->pscsem);

  return OK;
}
#endif

static void f1c100s_disconnect(struct usbhost_driver_s *drvr,
                               struct usbhost_hubport_s *hport)
{
  struct f1c100s_usbhost_s *priv = (struct f1c100s_usbhost_s *)drvr;

  DEBUGASSERT(priv != NULL && hport != NULL);

  /* Clear device class */

  hport->devclass = NULL;

  uinfo("Device disconnected\n");
}

#ifdef CONFIG_F1C100S_USBHOST_VBUS_DETECT
/****************************************************************************
 * Name: f1c100s_usbhost_vbus_worker
 *
 * Description:
 *   Work queue handler for VBUS detection GPIO interrupt.
 *
 ****************************************************************************/

static void f1c100s_usbhost_vbus_worker(void *arg)
{
  struct f1c100s_usbhost_s *priv = (struct f1c100s_usbhost_s *)arg;
  bool value;
  bool present;
  int ret;

  /* Read current GPIO state */

  ret = ioctl(priv->vbus_fd, GPIOC_READ, (unsigned long)&value);
  if (ret < 0)
    {
      uerr("ERROR: Failed to read VBUS GPIO: %d\n", ret);
      return;
    }

  /* Determine device present state (consider inversion) */

#ifdef CONFIG_F1C100S_USBHOST_VBUS_INVERTED
  present = !value;  /* Low = connected */
#else
  present = value;   /* High = connected (normal) */
#endif

  uinfo("VBUS detect: GPIO=%d present=%d (was %d)\n",
        value, present, priv->connected);

  /* Check if state changed */

  if (present != priv->connected)
    {
      priv->connected = present;
      priv->change = true;

      /* Signal the connection change */

      nxsem_post(&priv->pscsem);
    }
}

/****************************************************************************
 * Name: f1c100s_usbhost_vbus_interrupt
 *
 * Description:
 *   GPIO interrupt handler for VBUS detection.
 *
 ****************************************************************************/

static int f1c100s_usbhost_vbus_interrupt(FAR struct gpio_dev_s *dev,
                                          uint8_t pin)
{
  struct f1c100s_usbhost_s *priv = &g_usbhost;

  /* Schedule work to handle the VBUS detect event */

  work_queue(HPWORK, &priv->vbus_work, f1c100s_usbhost_vbus_worker, priv, 0);

  return OK;
}

/****************************************************************************
 * Name: f1c100s_usbhost_vbus_init
 *
 * Description:
 *   Initialize VBUS detection GPIO.
 *
 ****************************************************************************/

static int f1c100s_usbhost_vbus_init(struct f1c100s_usbhost_s *priv)
{
  bool value;
  int ret;

  /* Open the GPIO device */

  priv->vbus_fd = open(CONFIG_F1C100S_USBHOST_VBUS_DEVPATH, O_RDWR);
  if (priv->vbus_fd < 0)
    {
      uerr("ERROR: Failed to open VBUS GPIO %s: %d\n",
           CONFIG_F1C100S_USBHOST_VBUS_DEVPATH, errno);
      return -errno;
    }

  /* Set pin type to interrupt on both edges */

  ret = ioctl(priv->vbus_fd, GPIOC_SETPINTYPE,
              (unsigned long)GPIO_INTERRUPT_BOTH_PIN);
  if (ret < 0)
    {
      uerr("ERROR: Failed to set VBUS GPIO pin type: %d\n", ret);
      close(priv->vbus_fd);
      priv->vbus_fd = -1;
      return ret;
    }

  /* Register interrupt callback */

  ret = ioctl(priv->vbus_fd, GPIOC_REGISTER,
              (unsigned long)f1c100s_usbhost_vbus_interrupt);
  if (ret < 0)
    {
      uerr("ERROR: Failed to register VBUS GPIO callback: %d\n", ret);
      close(priv->vbus_fd);
      priv->vbus_fd = -1;
      return ret;
    }

  /* Read initial state */

  ret = ioctl(priv->vbus_fd, GPIOC_READ, (unsigned long)&value);
  if (ret < 0)
    {
      uerr("ERROR: Failed to read initial VBUS GPIO state: %d\n", ret);
      close(priv->vbus_fd);
      priv->vbus_fd = -1;
      return ret;
    }

#ifdef CONFIG_F1C100S_USBHOST_VBUS_INVERTED
  priv->connected = !value;
#else
  priv->connected = value;
#endif

  uinfo("VBUS GPIO initialized: initial state=%d\n", priv->connected);

  return OK;
}
#endif /* CONFIG_F1C100S_USBHOST_VBUS_DETECT */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: f1c100s_usbhost_initialize
 *
 * Description:
 *   Initialize the USB host controller.
 *
 * Input Parameters:
 *   controller - USB host controller index (0 for F1C100s)
 *
 * Returned Value:
 *   A non-NULL pointer to the USB host connection instance on success;
 *   NULL on failure.
 *
 ****************************************************************************/

struct usbhost_connection_s *f1c100s_usbhost_initialize(int controller)
{
  struct f1c100s_usbhost_s *priv = &g_usbhost;
  int ret;

  DEBUGASSERT(controller == 0);

  uinfo("Initializing USB Host controller\n");

  /* Initialize driver structure */

  memset(priv, 0, sizeof(struct f1c100s_usbhost_s));

  /* Initialize mutex and semaphore */

  nxmutex_init(&priv->lock);
  nxsem_init(&priv->pscsem, 0, 0);

  /* Initialize connection interface */

  priv->conn.wait = f1c100s_wait;
  priv->conn.enumerate = f1c100s_enumerate;

  /* Initialize driver interface */

  priv->drvr.ep0configure = f1c100s_ep0configure;
  priv->drvr.epalloc = f1c100s_epalloc;
  priv->drvr.epfree = f1c100s_epfree;
  priv->drvr.alloc = f1c100s_alloc;
  priv->drvr.free = f1c100s_free;
  priv->drvr.ioalloc = f1c100s_ioalloc;
  priv->drvr.iofree = f1c100s_iofree;
  priv->drvr.ctrlin = f1c100s_ctrlin;
  priv->drvr.ctrlout = f1c100s_ctrlout;
  priv->drvr.transfer = f1c100s_transfer;
#ifdef CONFIG_USBHOST_ASYNCH
  priv->drvr.asynch = f1c100s_asynch;
#endif
  priv->drvr.cancel = f1c100s_cancel;
#ifdef CONFIG_USBHOST_HUB
  priv->drvr.connect = f1c100s_connect;
#endif
  priv->drvr.disconnect = f1c100s_disconnect;

  /* Initialize root hub port */

  priv->rhport.hport.drvr = &priv->drvr;
#ifdef CONFIG_USBHOST_HUB
  priv->rhport.hport.parent = NULL;
#endif
  priv->rhport.hport.ep0 = &priv->ep[0];
  priv->rhport.hport.port = 0;
  priv->rhport.hport.speed = USB_SPEED_FULL;
  priv->rhport.pdevgen = &priv->devgen;

  /* Initialize device address generator */

  usbhost_devaddr_initialize(&priv->devgen);

  /* Initialize EP0 */

  priv->ep[0].epno = 0;
  priv->ep[0].in = false;
  priv->ep[0].xfrtype = USB_EP_ATTR_XFER_CONTROL;
  priv->ep[0].maxpacket = F1C100S_EP0_MAXPACKET;

  /* Initialize PHY for Host mode */

  f1c100s_phy_init_host();

#ifdef CONFIG_F1C100S_USB_DMA
  /* Initialize USB DMA */

  ret = f1c100s_usb_dma_init();
  if (ret < 0)
    {
      uerr("USB DMA init failed: %d\n", ret);
      /* Continue without DMA - it's optional */
    }
#endif

  /* Attach interrupt handler */

  ret = irq_attach(F1C100S_USB_IRQ, f1c100s_usbhost_interrupt, priv);
  if (ret < 0)
    {
      uerr("Failed to attach USB interrupt: %d\n", ret);
      nxmutex_destroy(&priv->lock);
      nxsem_destroy(&priv->pscsem);
      return NULL;
    }

  /* Enable USB interrupt */

  up_enable_irq(F1C100S_USB_IRQ);

#ifdef CONFIG_F1C100S_USBHOST_VBUS_DETECT
  /* Initialize VBUS detection GPIO */

  priv->vbus_fd = -1;
  ret = f1c100s_usbhost_vbus_init(priv);
  if (ret < 0)
    {
      uwarn("VBUS GPIO init failed: %d, using controller detection\n", ret);
      /* Continue without GPIO detection - use controller's internal detection */
    }
#endif

  uinfo("USB Host controller initialized\n");

  return &priv->conn;
}

#endif /* CONFIG_F1C100S_USBHOST */
