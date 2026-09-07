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

#include "hw/pci/demo_pcie_ep.h"
#include "hw/pci/pcie.h"
#include "hw/pci/msi.h"

#include "qemu/module.h"
#include "qemu/log.h"
#include "qemu/timer.h"

#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"

#define TYPE_DEMO_PCIE_EP "demo-pcie-ep"
OBJECT_DECLARE_SIMPLE_TYPE(DemoPCIEEPState, DEMO_PCIE_EP)

/** device reset */
static void demo_pcie_ep_reset_devcie(DemoPCIEEPState *s) {
  qemu_log_mask(LOG_TRACE, TYPE_DEMO_PCIE_EP ": device reset");
}

/*
 * BAR0 read
 */
static uint64_t demo_pcie_ep_read(void *opaque, hwaddr addr, unsigned size) {
  DemoPCIEEPState *s = opaque;

  uint32_t value = 0;

  if (size != 4) {
    qemu_log_mask(LOG_GUEST_ERROR, TYPE_DEMO_PCIE_EP
                  ": ivalid read size = %u, addr = 0x%" HWADDR_PRIx "\n",
				size, addr);
    return 0xffffffff;
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
    value = s->enabled ? CONTROL_ENABLE : 0;
    break;
  case REG_STATUS:
    value = s->enabled ? 1 : 0;
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
    value = s->regs[REG_DOORBELL_STATUS / 4];
    break;

  /** DMA */
  case REG_DMA_ADDR_LO:
    value = (uint32_t)(s->dma_addr & 0xFFFFFFFF);
    break;
  case REG_DMA_ADDR_HI:
    value = (uint32_t)(s->dma_addr >> 32);
    break;
  case REG_DMA_LEN:
    value = s->dma_len;
    break;
  case REG_DMA_CONTROL:
    value = s->dma_control;
    break;
  case REG_DMA_STATUS:
    value = s->dma_status;
    break;
  case REG_DMA_CHECKSUM:
    value = s->dma_checksum;
    break;

  /** scratch */
  case REG_SCRATCH0:
  case REG_SCRATCH1:
    value = s->regs[addr / 4];
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
                                  " addr=0x%" HWADDR_PRIx " value=0x%" PRIx32 "\n",
                addr, value);

  return value;
}

static void demo_pcie_ep_dma_complete(void *opaque);

static void demo_pcie_ep_dma_start(DemoPCIEEPState *s)
{
  if (!s->enabled) {
	s->dma_status = DMA_STATUS_ERROR;
	return;
  }

  if ((s->dma_len == 0) || (s->dma_len > 4096)) {
	s->dma_status = DMA_STATUS_ERROR;
	return;
  }

  if (s->dma_status & DMA_STATUS_BUSY) {
	return;
  }

  s->dma_status = DMA_STATUS_BUSY;
  timer_mod_ns(s->dma_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000);
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
	s->enabled = value & CONTROL_ENABLE;
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
    if (value & 1) {
        demo_pcie_ep_reset_devcie(s);
        s->enabled = false;
    }
    break;
  /*
   * --------------------------------------------------------
   * IRQ ENABLE
   * --------------------------------------------------------
   */
  case REG_IRQ_ENABLE:
    s->irq_enable = value;
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
    s->regs[REG_DOORBELL_STATUS / 4] = value;
    break;
  /*
   * --------------------------------------------------------
   * DMA ADDRESS
   * --------------------------------------------------------
   */
  case REG_DMA_ADDR_LO:
    s->dma_addr = (s->dma_addr & 0xffffffff00000000ULL) | (uint32_t)value;
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
	s->dma_control = value;
	if (value & DMA_CONTROL_START) {
		demo_pcie_ep_dma_start(s);
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
  case REG_SCRATCH1:
	s->regs[addr / 4] = value;
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

static void demo_pcie_ep_dma_complete(void *opaque)
{
	DemoPCIEEPState *s = opaque;
	uint8_t *buf;
	uint32_t i;

	if (!(s->dma_status & DMA_STATUS_BUSY)) {
      return;
	}

	buf = g_malloc(s->dma_len);
	if (s->dma_control & DMA_CONTROL_MEM_TO_DEV) {
		MemTxResult result;
		result = pci_dma_read(&s->parent_obj, s->dma_addr, buf, s->dma_len);
		if (result != MEMTX_OK) {
			s->dma_status = DMA_STATUS_ERROR;
			g_free(buf);
			return;
		}

		s->dma_checksum = 0;
		for (i = 0; i <s->dma_len; i++) {
			s->dma_checksum += buf[i];
		}
	} else {
		MemTxResult result;
		result = pci_dma_write(&s->parent_obj, s->dma_addr, buf, s->dma_len);
		if (result != MEMTX_OK) {
			s->dma_status = DMA_STATUS_ERROR;
			g_free(buf);
			return;
		}
	}

	g_free(buf);
	s->dma_status = DMA_STATUS_DONE;
	s->irq_status |= IRQ_DMA_DONE;
}

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

  pci_config_set_revision(pdev->config, 0x01);
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

  s->dma_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, demo_pcie_ep_dma_complete, s);

  qemu_log_mask(LOG_TRACE, TYPE_DEMO_PCIE_EP ": realize\n");
}

/*
 * PCI device exit
 */
static void demo_pcie_ep_exit(PCIDevice *pdev) {
	DemoPCIEEPState *s = DEMO_PCIE_EP(pdev);

	if (s->dma_timer) {
		timer_del(s->dma_timer);
		timer_free(s->dma_timer);
		s->dma_timer = NULL;
	}
	qemu_log_mask(LOG_TRACE, TYPE_DEMO_PCIE_EP ": exit\n");
}

/*
 * Class initialization
 */
static void demo_pcie_ep_class_init(ObjectClass *klass, const void *data) {
  DeviceClass *dc = DEVICE_CLASS(klass);
  PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

  k->realize = demo_pcie_ep_realize;
  k->exit = demo_pcie_ep_exit;

  set_bit(DEVICE_CATEGORY_MISC, dc->categories);
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