#include <linux/module.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include "demo_ep.h"

static const struct pci_device_id demo_pcie_ep_ids[] = {
	{ PCI_DEVICE(VENDOR_ID, DEVICE_ID) },
	{ },
};

MODULE_DEVICE_TABLE(
	pci,
	demo_pcie_ep_ids
);

static inline u32 demo_pcie_ep_readl(struct demo_pcie_ep *ep, u32 reg)
{
	return readl(ep->bar0 + reg);
}

static inline void demo_pcie_ep_writel(struct demo_pcie_ep *ep, u32 reg, u32 value)
{
	writel(value, ep->bar0 + reg);
}

static irqreturn_t demo_pcie_ep_irq(int irq, void *data)
{
	struct demo_pcie_ep *ep = data;
	u32 irq_status;
	u32 dma_status;

	irq_status = demo_pcie_ep_readl(ep, REG_IRQ_STATUS);
	if (!(irq_status & IRQ_DMA_DONE)) {
		return IRQ_NONE;
	}

	dma_status = demo_pcie_ep_readl(ep, REG_DMA_STATUS);
	dev_info(&ep->pdev->dev, "IRQ: irq_status = 0x%08x, dma_status = 0x%08x\n", irq_status, dma_status);

	demo_pcie_ep_writel(ep, REG_IRQ_STATUS, IRQ_DMA_DONE);

	complete(&ep->dma_completion);

	return IRQ_HANDLED;
}

static void demo_print_dma_info(struct demo_pcie_ep *ep)
{
	dev_info(&ep->pdev->dev, "dma_cpu_addr = %p\n", ep->dma_cpu_addr);
	dev_info(&ep->pdev->dev, "dma_handle = 0x%08x\n", ep->dma_handle);
	dev_info(&ep->pdev->dev, "dma_size = %d\n", ep->dma_size);
}


static void demo_program_dma_addr(struct demo_pcie_ep *ep, dma_addr_t addr)
{
	demo_pcie_ep_writel(ep, REG_DMA_ADDR_LO, lower_32_bits(addr));
	demo_pcie_ep_writel(ep, REG_DMA_ADDR_HI, upper_32_bits(addr));
}

static int demo_wait_dma(struct demo_pcie_ep *ep)
{
	int timeout = 1000;
	u32 status;

	timeout = wait_for_completion_timeout(&ep->dma_completion, msecs_to_jiffies(1000));
	if (!timeout) {
		dev_err(&ep->pdev->dev, "DMA timeout\n");
		return -ETIMEDOUT;
	}

	status = demo_pcie_ep_readl(ep, REG_DMA_STATUS);
	if (status & DMA_STATUS_ERROR) {
		dev_err(&ep->pdev->dev, "DMA error, status = 0x%08x\n", status);
		return -EIO;
	}

	if (!(status & DMA_STATUS_DONE)) {
		dev_err(&ep->pdev->dev, "DMA not done, status = 0x%08x\n", status);
		return -EIO;
	}
	

	return 0;
}

static int demo_dma_device_to_memory(struct demo_pcie_ep *ep)
{
	size_t i;
	int ret;

	dev_info(&ep->pdev->dev, "DMA test: DEVICE -> MEMORY\n");

	memset(ep->dma_cpu_addr, 0x0, ep->dma_size);
	reinit_completion(&ep->dma_completion);

	demo_program_dma_addr(ep, ep->dma_handle);
	demo_pcie_ep_writel(ep, REG_DMA_LEN, ep->dma_size);
	demo_pcie_ep_writel(ep, REG_IRQ_ENABLE, IRQ_DMA_DONE);
	demo_pcie_ep_writel(ep, REG_DMA_CONTROL, DMA_CONTROL_START);

	ret = demo_wait_dma(ep);
	if (ret) {
		return ret;
	}

	for (i = 0; i < ep->dma_size; i++) {
		u8 expected = i & 0xff;
		u8 actual;

		actual = ((u8 *)ep->dma_cpu_addr)[i];
		if (actual != expected) {
			dev_err(&ep->pdev->dev, "DMA test: DEVICE -> MEMORY failed, i = %d, expected = 0x%02x, actual = 0x%02x\n", i, expected, actual);
			return -EIO;
		}
	}

	dev_info(&ep->pdev->dev, "DMA test: DEVICE -> MEMORY passed\n");

	return 0;
}

static int demo_dma_memory_to_device(struct demo_pcie_ep *ep)
{
	size_t i;
	u32 checksum = 0;
	u32 device_checksum;
	int ret;

	dev_info(&ep->pdev->dev, "DMA test: MEMORY -> DEVICE\n");

	for (i = 0; i < ep->dma_size; i++) {
		((u8 *)ep->dma_cpu_addr)[i] = i & 0xff;
		checksum += (i & 0xff);
	}

	wmb();
	reinit_completion(&ep->dma_completion);

	demo_program_dma_addr(ep, ep->dma_handle);
	demo_pcie_ep_writel(ep, REG_DMA_LEN, ep->dma_size);
	demo_pcie_ep_writel(ep, REG_DMA_CONTROL, DMA_CONTROL_START | DMA_CONTROL_MEM_TO_DEV);

	ret = demo_wait_dma(ep);
	if (ret) {
		return ret;
	}

	device_checksum = demo_pcie_ep_readl(ep, REG_DMA_CHECKSUM);
	dev_info(&ep->pdev->dev, "DMA checksum: device = 0x%08x, expected = 0x%08x\n", device_checksum, checksum);

	if (device_checksum != checksum) {
		dev_err(&ep->pdev->dev, "DMA checksum mismatch\n");
		return -EIO;
	}

	dev_info(&ep->pdev->dev, "DMA MEMORY -> DEVICE PASS\n");

	return 0;
}

static int demo_pcie_ep_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct demo_pcie_ep *ep;
	unsigned int nr_vecs;
	int ret;

	u32 version;
	u32 device_id;
	u32 revision;
	u32 capability;

	dev_info(&pdev->dev, "demo pcie ep probe\n");

	ep = devm_kzalloc(&pdev->dev, sizeof(*ep), GFP_KERNEL);
	if (!ep) {
		return -ENOMEM;
	}

	ep->pdev = pdev;
	pci_set_drvdata(pdev, ep);

	ret = pci_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "pci_enable_device failed\n");
		return ret;
	}

	pci_set_master(pdev);
	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (ret) {
		dev_info(&pdev->dev, "64bit DMA not available, try 32bit\n");
		ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
		if (ret) {
			dev_err(&pdev->dev, "32bit DMA not available\n");
			return ret;
		}
	}

	ret = pcim_iomap_regions(
		pdev,
		BIT(0),
		DRIVER_NAME
	);

	if (ret) {
		dev_err(&pdev->dev, "pci_request_region failed\n");
		goto disable_device;
	}

	ep->bar0 = pcim_iomap_table(pdev)[0];
	if (!ep->bar0) {
		dev_err(&pdev->dev, "pci_iomap failed\n");
		goto release_region;
	}

	version = demo_pcie_ep_readl(ep, REG_VERSION);
	revision = demo_pcie_ep_readl(ep, REG_REVISION);
	capability = demo_pcie_ep_readl(ep, REG_CAPABILITY);

	dev_info(&pdev->dev, "VERSION = 0x%08x\n", version);
	dev_info(&pdev->dev, "REVISION = 0x%08x\n", revision);
	dev_info(&pdev->dev, "CAPABILITY = 0x%08x\n", capability);
	
	nr_vecs = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSI);
	if ((int)nr_vecs < 0) {
		dev_err(&pdev->dev, "pci_alloc_irq_vectors failed: %d\n", nr_vecs);
		return nr_vecs;
	}
	dev_info(&pdev->dev, "MSI vectors = %u\n", nr_vecs);

	ep->irq = pci_irq_vector(pdev, 0);
	dev_info(&pdev->dev, "MSI IRQ = %u\n", ep->irq);	

	init_completion(&ep->dma_completion);
	ret = request_irq(ep->irq, demo_pcie_ep_irq, 0, DRIVER_NAME, ep);
	if (ret) {
		dev_err(&pdev->dev, "request_irq failed: %d\n", ret);
		pci_free_irq_vectors(pdev);
		return ret;
	}

	demo_pcie_ep_writel(ep, REG_CONTROL, CONTROL_ENABLE);
	
	ep->dma_size = DMA_BUF_SIZE;
	ep->dma_cpu_addr = dma_alloc_coherent(&pdev->dev, ep->dma_size, &ep->dma_handle, GFP_KERNEL);
	if (!ep->dma_cpu_addr) {
		dev_err(&pdev->dev, "dma_alloc_coherent failed\n");
		goto release_region;
	}
	demo_print_dma_info(ep);

	ret = demo_dma_device_to_memory(ep);
	if (ret) {
		goto dma_err;
	}
	dev_info(&pdev->dev, "DMA test: DEVICE -> MEMORY passed\n");

	ret = demo_dma_memory_to_device(ep);
	if (ret) {
		goto dma_err;
	}
	dev_info(&pdev->dev, "DMA test: MEMORY -> DEVICE passed\n");

	return 0;

dma_err:
	dma_free_coherent(&pdev->dev, ep->dma_size, ep->dma_cpu_addr, ep->dma_handle);
	ep->dma_cpu_addr = NULL;
	ep->dma_handle = 0;

release_region:
	pci_release_region(pdev, 0);

disable_device:
	pci_disable_device(pdev);

	return ret;
}

static void demo_pcie_ep_remove(struct pci_dev *pdev)
{
	struct demo_pcie_ep *ep = pci_get_drvdata(pdev);

	if (!ep) {
		return;
	}

	if (ep->bar0) {
		pci_iounmap(pdev, ep->bar0);
		ep->bar0 = NULL;
	}

	pci_release_region(pdev, 0);
	pci_disable_device(pdev);

	dev_info(&pdev->dev, "demo_pcie_ep removed\n");
}

static struct pci_driver demo_pcie_ep_driver = {
	.name = "demo_pcie_ep",
	.id_table = demo_pcie_ep_ids,
	.probe = demo_pcie_ep_probe,
	.remove = demo_pcie_ep_remove,
};

module_driver(demo_pcie_ep_driver, pci_register_driver, pci_unregister_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("HL");
MODULE_DESCRIPTION("Demo PCIE EP Driver");