#ifndef __HW_PCI_DEMO_PCIE_EP_H__
#define __HW_PCI_DEMO_PCIE_EP_H__

#include "hw/pci/pci_device.h"
#include "system/memory.h"
#include "qemu/timer.h"

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
#define REG_DMA_CHECKSUM 0x054

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
#define DMA_CONTROL_MEM_TO_DEV BIT(1)
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

/** irq */
#define IRQ_DMA_DONE BIT(0)
#define IRQ_DOORBELL BIT(1)

typedef struct DemoPCIEEPState {
  PCIDevice parent_obj;
  MemoryRegion bar0;
  QEMUTimer *dma_timer;
  uint32_t regs[DEMO_PCIE_EP_BAR0_SIZE / sizeof(uint32_t)];
  
  uint64_t dma_addr;
  uint32_t dma_len;
  uint32_t dma_control;
  uint32_t dma_status;
  uint32_t dma_checksum;
  
  uint32_t irq_enable;
  uint32_t irq_status;

  bool enabled;

} DemoPCIEEPState;

#endif /* __HW_PCI_DEMO_PCIE_EP_H__ */