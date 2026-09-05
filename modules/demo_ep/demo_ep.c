#include <linux/module.h>
#include <linux/pci.h>
#include <linux/io.h>

#define VENDOR_ID 0x2026
#define DEVICE_ID 0x0904

#define REG_VERSION 	0x000
#define REG_DEVICE_ID 	0x004
#define REG_REVISION 	0x008

struct demo_pcie_ep {
	struct pci_dev *pdev;
	void __iomem *bar0;
};

static const struct pci_device_id demo_pcie_ep_ids[] = {
	{ PCI_DEVICE(VENDOR_ID, DEVICE_ID) },
	{ },
};

MODULE_DEVICE_TABLE(
	pci,
	demo_pcie_ep_ids
);

static int demo_pcie_ep_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct demo_pcie_ep *ep;

	u32 version;
	u32 device_id;
	u32 revision;

	int ret;

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

	ret = pci_request_region(
		pdev,
		0,
		"demo_pcie_ep"
	);

	if (ret) {
		dev_err(&pdev->dev, "pci_request_region failed\n");
		goto disable_device;
	}

	ep->bar0 = pci_iomap(pdev, 0, 0);
	if (!ep->bar0) {
		dev_err(&pdev->dev, "pci_iomap failed\n");
		goto release_region;
	}

	version = readl(ep->bar0 + REG_VERSION);
	device_id = readl(ep->bar0 + REG_DEVICE_ID);
	revision = readl(ep->bar0 + REG_REVISION);

	dev_info(&pdev->dev, "VERSION = 0x%08x\n", version);
	dev_info(&pdev->dev, "DEVICE_ID = 0x%08x\n", device_id);
	dev_info(&pdev->dev, "REVISION = 0x%08x\n", revision);
	
	return 0;

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