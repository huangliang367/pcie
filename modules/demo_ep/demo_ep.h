#ifndef _DEMO_EP_H
#define _DEMO_EP_H

#define DRIVER_NAME "demo_pcie_ep"

#define VENDOR_ID 0x2026
#define DEVICE_ID 0x0904

#define BAR0_SIZE	0x1000

#define REG_VERSION 	0x000
#define REG_DEVICE_ID 	0x004
#define REG_REVISION 	0x008
#define REG_CAPABILITY 	0x00C

#define REG_CONTROL	0x010
#define REG_STATUS	0x014
#define REG_RESET	0x018

#define REG_IRQ_ENABLE	0x020
#define REG_IRQ_STATUS	0x024

#define REG_DOORBELL	0x030
#define REG_DRROBELL_STATUS	0x034

#define REG_DMA_ADDR_LO	0x040
#define REG_DMA_ADDR_HI	0x044
#define REG_DMA_LEN	0x048
#define REG_DMA_CONTROL 0x04C
#define REG_DMA_STATUS	0x050
#define REG_DMA_CHECKSUM	0x054

#define REG_SCRATCH0	0x100
#define REG_SCRATCH1	0x104

#define CONTROL_ENABLE	BIT(0)

#define DMA_CONTROL_START	BIT(0)
#define DMA_CONTROL_MEM_TO_DEV	BIT(1)

#define DMA_STATUS_BUSY	BIT(0)
#define DMA_STATUS_DONE	BIT(1)
#define DMA_STATUS_ERROR	BIT(2)

#define DMA_BUF_SIZE	4096

struct demo_pcie_ep {
	struct pci_dev *pdev;
	void __iomem *bar0;
	void *dma_cpu_addr;
	dma_addr_t dma_handle;
	size_t dma_size;
};

#endif /* _DEMO_EP_H */