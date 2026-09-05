/*
 * QEMU PCIe Endpoint - Lab 3
 *
 * Realistic device register model
 *
 * Features:
 *   - PCIe Endpoint
 *   - BAR0 4KB MMIO
 *   - RO/RW/WO registers
 *   - W1C interrupt status
 *   - Control/status registers
 *   - Doorbell
 *   - Reset
 *   - DMA register model
 *   - Scratch registers
 */

#include "qemu/osdep.h"

#include "hw/pci/pci_bus.h"
#include "hw/pci/pci_device.h"

#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define TYPE_DEMO_PCIE_EP "demo-pcie-ep"

OBJECT_DECLARE_SIMPLE_TYPE(DemoPCIEEPState, DEMO_PCIE_EP)

#define DEMO_PCIE_EP_VENDOR_ID 0x2026
#define DEMO_PCIE_EP_DEVICE_ID 0x0904

#define DEMO_PCIE_EP_BAR0_SIZE 0x1000

/*
 * BAR0 registers
 */
#define REG_VERSION 0x000
#define REG_DEVICE_ID 0x004
#define REG_REVISION 0x008
#define REG_CAPABILITY 0x00c

#define REG_CONTROL 0x010
#define REG_STATUS 0x014
#define REG_RESET 0x018

#define REG_IRQ_ENABLE 0x020
#define REG_IRQ_STATUS 0x024

#define REG_DOORBELL 0x030
#define REG_DOORBELL_STATUS 0x034

#define REG_DMA_ADDR_LO 0x040
#define REG_DMA_ADDR_HI 0x044
#define REG_DMA_LEN 0x048
#define REG_DMA_CONTROL 0x04c
#define REG_DMA_STATUS 0x050

#define REG_SCRATCH0 0x100
#define REG_SCRATCH1 0x104
#define REG_SCRATCH2 0x108
#define REG_SCRATCH3 0x10c

/** constant register values */
#define VERSION_VALUE 0x00010000
#define DEVICE_ID_VALUE 0x20260904
#define REVISION_VALUE 0x00000001

#define CAP_DMA BIT(0)
#define CAP_MSI BIT(1)
#define CAP_MSIX BIT(2)

/**
 * control register
 * bit0: enable
 * bit1: start
 * bit2: reset
 */
#define CONTROL_ENABLE BIT(0)
#define CONTROL_START BIT(1)
#define CONTROL_RESET BIT(2)

/**
 * status register
 */
#define STATUS_READY BIT(0)
#define STATUS_ENABLED BIT(1)
#define STATUS_BUSY BIT(2)
#define STATUS_ERROR BIT(3)

/** irq */
#define IRQ_DMA_DONE BIT(0)
#define IRQ_DOORBELL BIT(1)
#define IRQ_ERROR BIT(2)

/** doorbell */
#define DOORBELL_RX BIT(0)
#define DOORBELL_TX BIT(1)
#define DOORBELL_DMA BIT(2)

/* DMA control */
#define DMA_CONTROL_START BIT(0)
#define DMA_CONTROL_DIR_READ BIT(1)
#define DMA_CONTROL_IRQ_ENABLE BIT(2)

/** DMA status */
#define DMA_STATUS_IDLE 0
#define DMA_STATUS_BUSY BIT(0)
#define DMA_STATUS_DONE BIT(1)
#define DMA_STATUS_ERROR BIT(2)

/** reset */
#define RESET_DEVICE BIT(0)
#define RESET_DMA BIT(1)
#define RESET_IRQ BIT(2)

typedef struct DemoPCIEEPState {
  PCIDevice parent_obj;

  /*
   * BAR0
   */
  MemoryRegion bar0;

  /*
   * control /status
   */
  uint32_t control;
  uint32_t status;

  /** irq */
  uint32_t irq_enable;
  uint32_t irq_status;

  /** doorbell */
  uint32_t doorbell_status;

  /** dma */
  uint64_t dma_addr;
  uint64_t dma_len;

  uint64_t dma_control;
  uint64_t dma_status;

  /** scratch */
  uint32_t scratch0;
  uint32_t scratch1;
  uint32_t scratch2;
  uint32_t scratch3;
} DemoPCIEEPState;

/** device reset */
static void demo_pcie_ep_reset_devcie(DemoPCIEEPState *s) {
  qemu_log_mask(LOG_TRACE, TYPE_DEMO_PCIE_EP ": device reset");

  s->control = 0;
  s->status = STATUS_READY;
  s->irq_enable = 0;
  s->irq_status = 0;
  s->doorbell_status = 0;
  s->dma_addr = 0;
  s->dma_len = 0;
  s->dma_control = 0;
  s->dma_status = DMA_STATUS_IDLE;

  s->scratch0 = 0;
  s->scratch1 = 0;
  s->scratch2 = 0;
  s->scratch3 = 0;
}

/*
 * BAR0 read
 */
static uint64_t demo_pcie_ep_read(void *opaque, hwaddr addr, unsigned size) {
  DemoPCIEEPState *s = opaque;

  uint64_t value = 0;

  if (size != 4) {
    qemu_log_mask(LOG_GUEST_ERROR, TYPE_DEMO_PCIE_EP
                  ": ivalid read size = %u, addr = 0x%" HWADDR_PRIx "\n",
				size, addr);
    return 0;
  }

  switch (addr) {
    /** identification */
  case REG_VERSION:
    value = VERSION_VALUE;
    break;

  case REG_DEVICE_ID:
    value = DEVICE_ID_VALUE;
    break;

  case REG_REVISION:
    value = REVISION_VALUE;
    break;

  case REG_CAPABILITY:
    value = CAP_DMA | CAP_MSI | CAP_MSIX;
    break;

  /** control */
  case REG_CONTROL:
    value = s->control;
    break;
  case REG_STATUS:
    value = s->status;
    break;

  /** reset is write-only*/
  case REG_RESET:
    qemu_log_mask(LOG_GUEST_ERROR, TYPE_DEMO_PCIE_EP ": reset is write-only");
    value = 0;
    break;

  /** irq */
  case REG_IRQ_ENABLE:
    value = s->irq_enable;
    break;
  case REG_IRQ_STATUS:
    value = s->irq_status;
    break;

  /** doorbell */
  case REG_DOORBELL:
    qemu_log_mask(LOG_GUEST_ERROR,
                  TYPE_DEMO_PCIE_EP ": read from write-only DOORBELL\n");

    value = 0;
    break;
  case REG_DOORBELL_STATUS:
    value = s->doorbell_status;
    break;

  /** DMA */
  case REG_DMA_ADDR_LO:
    value = (uint32_t)(s->dma_addr & 0xFFFFFFFF);
    break;
  case REG_DMA_ADDR_HI:
    value = (uint32_t)(s->dma_addr >> 32);
    break;
  case REG_DMA_CONTROL:
    value = s->dma_control;
    break;
  case REG_DMA_STATUS:
    value = s->dma_status;
    break;

  /** scratch */
  case REG_SCRATCH0:
    value = s->scratch0;
    break;
  case REG_SCRATCH1:
    value = s->scratch1;
    break;
  case REG_SCRATCH2:
    value = s->scratch2;
    break;
  case REG_SCRATCH3:
    value = s->scratch3;
    break;
  /** invalid register */
  default:
    qemu_log_mask(LOG_GUEST_ERROR,
                  TYPE_DEMO_PCIE_EP ": invalid read addr=0x%" HWADDR_PRIx "\n",
                  addr);

    value = 0;
    break;
  }

  qemu_log_mask(LOG_TRACE,
                TYPE_DEMO_PCIE_EP ": BAR0 READ "
                                  " addr=0x%" HWADDR_PRIx " value=0x%" PRIx64 "\n",
                addr, value);

  return value;
}

/*
 * BAR0 write
 */
static void demo_pcie_ep_write(void *opaque, hwaddr addr, uint64_t val,
                             unsigned size) {
  DemoPCIEEPState *s = opaque;
  uint32_t value = (uint32_t)val;

  if (size != 4) {
    qemu_log_mask(LOG_GUEST_ERROR,
                  TYPE_DEMO_PCIE_EP ": invalid write size=%u "
                                    "addr=0x%" HWADDR_PRIx "\n",
                  size, addr);
    return;
  }

  qemu_log_mask(LOG_TRACE,
                TYPE_DEMO_PCIE_EP ": BAR0 WRITE "
                                  "addr=0x%" HWADDR_PRIx " value=0x%08x\n",
                addr, value);

  switch (addr) {
  /*
   * --------------------------------------------------------
   * Read-only registers
   * --------------------------------------------------------
   */
  case REG_VERSION:
  case REG_DEVICE_ID:
  case REG_REVISION:
  case REG_CAPABILITY:
    qemu_log_mask(LOG_GUEST_ERROR,
                  TYPE_DEMO_PCIE_EP ": write to read-only register "
                                    "addr=0x%" HWADDR_PRIx "\n",
                  addr);
    break;
  /*
   * --------------------------------------------------------
   * CONTROL
   * --------------------------------------------------------
   */
  case REG_CONTROL:
    /*
     * Only defined bits are accepted.
     */
    s->control = value & (CONTROL_ENABLE | CONTROL_START);
    if (s->control & CONTROL_ENABLE) {
      s->status |= STATUS_ENABLED;
    } else {
      s->status &= ~STATUS_ENABLED;
    }

    if (s->control & CONTROL_START) {
      s->status |= STATUS_BUSY;
    }
    break;
  /*
   * --------------------------------------------------------
   * STATUS is read-only
   * --------------------------------------------------------
   */
  case REG_STATUS:
    qemu_log_mask(LOG_GUEST_ERROR,
                  TYPE_DEMO_PCIE_EP ": write to read-only STATUS\n");
    break;
  /*
   * --------------------------------------------------------
   * RESET
   * --------------------------------------------------------
   */
  case REG_RESET:
    if (value & RESET_DEVICE) {
      demo_pcie_ep_reset_devcie(s);
      break;
    }

    if (value & RESET_DMA) {
      s->dma_addr = 0;
      s->dma_len = 0;
      s->dma_control = 0;
      s->dma_status = DMA_STATUS_IDLE;
    }

    if (value & RESET_IRQ) {
      s->irq_enable = 0;
      s->irq_status = 0;
    }
    break;
  /*
   * --------------------------------------------------------
   * IRQ ENABLE
   * --------------------------------------------------------
   */
  case REG_IRQ_ENABLE:
    s->irq_enable = value & (IRQ_DMA_DONE | IRQ_DOORBELL | IRQ_ERROR);
    break;
  /*
   * --------------------------------------------------------
   * IRQ STATUS
   *
   * W1C:
   *
   *   write 1 -> clear
   *   write 0 -> keep
   * --------------------------------------------------------
   */
  case REG_IRQ_STATUS:
    s->irq_status &= ~value;
    break;
  /*
   * --------------------------------------------------------
   * DOORBELL
   * --------------------------------------------------------
   */
  case REG_DOORBELL:
    /*
     * Guest writes a doorbell.
     */
    if (value & DOORBELL_RX) {
      s->doorbell_status |= DOORBELL_RX;
      s->irq_status |= IRQ_DOORBELL;
    }

    if (value & DOORBELL_TX) {
      s->doorbell_status |= DOORBELL_TX;
      s->irq_status |= IRQ_DOORBELL;
    }

    if (value & DOORBELL_DMA) {
      s->doorbell_status |= DOORBELL_DMA;
      s->irq_status |= IRQ_DMA_DONE;
    }
    break;
  /*
   * --------------------------------------------------------
   * DOORBELL STATUS
   *
   * Write 1 to clear.
   * --------------------------------------------------------
   */
  case REG_DOORBELL_STATUS:
    s->doorbell_status &= ~value;
    break;
  /*
   * --------------------------------------------------------
   * DMA ADDRESS
   * --------------------------------------------------------
   */
  case REG_DMA_ADDR_LO:
    s->dma_addr = (s->dma_addr & 0xffffffff00000000ULL) | (uint64_t)value;
    break;

  case REG_DMA_ADDR_HI:
    s->dma_addr =
        (s->dma_addr & 0x00000000ffffffffULL) | ((uint64_t)value << 32);
    break;
  /*
   * --------------------------------------------------------
   * DMA LENGTH
   * --------------------------------------------------------
   */
  case REG_DMA_LEN:
    s->dma_len = value;
    break;
  /*
   * --------------------------------------------------------
   * DMA CONTROL
   * --------------------------------------------------------
   */
  case REG_DMA_CONTROL:
    s->dma_control = value & (DMA_CONTROL_START | DMA_CONTROL_DIR_READ |
                              DMA_CONTROL_IRQ_ENABLE);

    if (value & DMA_CONTROL_START) {
      s->dma_status = DMA_STATUS_BUSY;
      s->status |= STATUS_BUSY;
      /*
       * Lab 3:
       *
       * We don't actually perform DMA yet.
       *
       * Lab 4 will call:
       *
       *     my_pcie_ep_start_dma(s);
       *
       */
    }
    break;

  /*
   * DMA STATUS is read-only
   */
  case REG_DMA_STATUS:
    qemu_log_mask(LOG_GUEST_ERROR, TYPE_DEMO_PCIE_EP ": write to read-only "
                                                     "DMA_STATUS\n");

    break;

    /*
     * --------------------------------------------------------
     * SCRATCH
     * --------------------------------------------------------
     */

  case REG_SCRATCH0:

    s->scratch0 = value;

    break;

  case REG_SCRATCH1:

    s->scratch1 = value;

    break;

  case REG_SCRATCH2:

    s->scratch2 = value;

    break;

  case REG_SCRATCH3:

    s->scratch3 = value;

    break;

    /*
     * --------------------------------------------------------
     * Invalid register
     * --------------------------------------------------------
     */

  default:

    qemu_log_mask(LOG_GUEST_ERROR,
                  TYPE_DEMO_PCIE_EP ": invalid write "
                                    "addr=0x%" HWADDR_PRIx " value=0x%08x\n",
                  addr, value);

    break;
  }
}

/*
 * MMIO operations
 */
static const MemoryRegionOps demo_pcie_ep_ops = {
    .read = demo_pcie_ep_read,
    .write = demo_pcie_ep_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl =
        {
            .min_access_size = 4,
            .max_access_size = 4,
        },
};

/*
 * PCI device realize
 */
static void demo_pcie_ep_realize(PCIDevice *pdev, Error **errp) {
  DemoPCIEEPState *s = DEMO_PCIE_EP(pdev);

  /*
   * PCI Config Space
   */
  pci_config_set_vendor_id(pdev->config, DEMO_PCIE_EP_VENDOR_ID);

  pci_config_set_device_id(pdev->config, DEMO_PCIE_EP_DEVICE_ID);

  /*
   * Generic / experimental device class
   *
   * Base class = 0xff
   * Subclass  = 0x00
   */
  pci_config_set_class(pdev->config, PCI_CLASS_OTHERS);

  /*
   * BAR0
   *
   * 4 KiB MMIO
   */
  memory_region_init_io(&s->bar0, OBJECT(s), &demo_pcie_ep_ops, s,
                        "demo-pcie-ep-bar0", DEMO_PCIE_EP_BAR0_SIZE);

  pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar0);

  demo_pcie_ep_reset_devcie(s);

  qemu_log_mask(LOG_TRACE, TYPE_DEMO_PCIE_EP ": realize\n");
}

/*
 * PCI device exit
 */
static void demo_pcie_ep_exit(PCIDevice *pdev) {
	qemu_log_mask(LOG_TRACE, TYPE_DEMO_PCIE_EP ": exit\n");
}

/*
 * Instance initialization
 */
static void demo_pcie_ep_init(Object *obj) {
  DemoPCIEEPState *s = DEMO_PCIE_EP(obj);

	s->control = 0;
	s->status = 0;
	s->irq_enable = 0;
	s->irq_status = 0;
	s->doorbell_status = 0;
	s->dma_addr = 0;
	s->dma_len = 0;
	s->dma_control = 0;
	s->dma_status = DMA_STATUS_IDLE;
	s->scratch0 = 0;
	s->scratch0 = 0;
	s->scratch1 = 0;
	s->scratch2 = 0;
	s->scratch3 = 0;
}

/*
 * Class initialization
 */
static void demo_pcie_ep_class_init(ObjectClass *klass, const void *data) {
  DeviceClass *dc = DEVICE_CLASS(klass);
  PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

  k->realize = demo_pcie_ep_realize;
  k->exit = demo_pcie_ep_exit;

  /*
   * No hotplug for this lab device.
   */
  dc->desc = "Demo PCIe Endpoint Device";
}

/*
 * QEMU type information
 */
static const TypeInfo demo_pcie_ep_info = {
    .name = TYPE_DEMO_PCIE_EP,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(DemoPCIEEPState),

    .instance_init = demo_pcie_ep_init,
    .class_init = demo_pcie_ep_class_init,

    .interfaces =
        (InterfaceInfo[]){
            {INTERFACE_PCIE_DEVICE},
            {},
        },
};

/*
 * Register device
 */
static void demo_pcie_ep_register_types(void) {
  type_register_static(&demo_pcie_ep_info);
}

type_init(demo_pcie_ep_register_types);