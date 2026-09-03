/*
 * Simple PCIe Endpoint device for QEMU
 *
 * Lab 1:
 *   - Vendor ID : 0x1234
 *   - Device ID : 0x5678
 *   - Class      : 0xff0000
 *   - BAR0       : 4 KiB MMIO
 *
 * BAR0 register map:
 *
 *   0x000 VERSION
 *   0x004 CONTROL
 *   0x008 STATUS
 *   0x00c SCRATCH
 *
 *   0x010 DEVICE_ID
 *   0x014 MAGIC
 */

#include "qemu/osdep.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/pci_bus.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "qapi/error.h"

#define TYPE_DEMO_PCIE_EP "demo-pcie-ep"

OBJECT_DECLARE_SIMPLE_TYPE(DemoPCIEEPState, DEMO_PCIE_EP)

#define DEMO_PCIE_EP_VENDOR_ID       0x1234
#define DEMO_PCIE_EP_DEVICE_ID       0x5678

#define DEMO_PCIE_EP_BAR0_SIZE       0x1000

/*
 * BAR0 registers
 */
#define REG_VERSION                0x000
#define REG_CONTROL                0x004
#define REG_STATUS                 0x008
#define REG_SCRATCH                0x00c
#define REG_DEVICE_ID              0x010
#define REG_MAGIC                  0x014

#define VERSION_VALUE              0x00010000
#define DEVICE_ID_VALUE            0x12345678
#define MAGIC_VALUE                0x50434945  /* "PCIE" */

#define CONTROL_ENABLE             BIT(0)

#define STATUS_READY               BIT(0)
#define STATUS_ENABLE              BIT(1)


typedef struct DemoPCIEEPState {
    PCIDevice parent_obj;

    /*
     * BAR0 MMIO region
     */
    MemoryRegion bar0;

    /*
     * Device registers
     */
    uint32_t control;
    uint32_t status;
    uint32_t scratch;

} DemoPCIEEPState;


/*
 * BAR0 read
 */
static uint64_t demo_pcie_ep_read(
        void *opaque,
        hwaddr addr,
        unsigned size)
{
    DemoPCIEEPState *s = opaque;

    uint64_t value = 0;

    switch (addr) {

    case REG_VERSION:
        value = VERSION_VALUE;
        break;

    case REG_CONTROL:
        value = s->control;
        break;

    case REG_STATUS:
        value = s->status;
        break;

    case REG_SCRATCH:
        value = s->scratch;
        break;

    case REG_DEVICE_ID:
        value = DEVICE_ID_VALUE;
        break;

    case REG_MAGIC:
        value = MAGIC_VALUE;
        break;

    default:
        qemu_log_mask(
            LOG_GUEST_ERROR,
            "%s: invalid read addr=0x%" HWADDR_PRIx
            " size=%u\n",
            TYPE_DEMO_PCIE_EP,
            addr,
            size);

        value = 0;
        break;
    }

    qemu_log_mask(
        LOG_TRACE,
        "%s: BAR0 read addr=0x%" HWADDR_PRIx
        " value=0x%" PRIx64 "\n",
        TYPE_DEMO_PCIE_EP,
        addr,
        value);

    return value;
}


/*
 * BAR0 write
 */
static void my_pcie_ep_write(
        void *opaque,
        hwaddr addr,
        uint64_t val,
        unsigned size)
{
    DemoPCIEEPState *s = opaque;

    qemu_log_mask(
        LOG_TRACE,
        "%s: BAR0 write addr=0x%" HWADDR_PRIx
        " value=0x%" PRIx64 "\n",
        TYPE_DEMO_PCIE_EP,
        addr,
        val);

    switch (addr) {

    case REG_CONTROL:

        s->control = val;

        if (val & CONTROL_ENABLE) {
            s->status |= STATUS_ENABLE;
        } else {
            s->status &= ~STATUS_ENABLE;
        }

        break;

    case REG_SCRATCH:

        s->scratch = val;

        break;

    case REG_VERSION:
    case REG_STATUS:
    case REG_DEVICE_ID:
    case REG_MAGIC:

        /*
         * Read-only registers.
         */
        qemu_log_mask(
            LOG_GUEST_ERROR,
            "%s: write to read-only register "
            "addr=0x%" HWADDR_PRIx "\n",
            TYPE_DEMO_PCIE_EP,
            addr);

        break;

    default:

        qemu_log_mask(
            LOG_GUEST_ERROR,
            "%s: invalid write addr=0x%" HWADDR_PRIx
            " value=0x%" PRIx64 "\n",
            TYPE_DEMO_PCIE_EP,
            addr,
            val);

        break;
    }
}


/*
 * MMIO operations
 */
static const MemoryRegionOps demo_pcie_ep_ops = {
    .read = demo_pcie_ep_read,
    .write = my_pcie_ep_write,

    .endianness = DEVICE_LITTLE_ENDIAN,

    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};


/*
 * PCI device realize
 */
static void demo_pcie_ep_realize(
        PCIDevice *pdev,
        Error **errp)
{
    DemoPCIEEPState *s = DEMO_PCIE_EP(pdev);

    /*
     * PCI Config Space
     */
    pci_config_set_vendor_id(
        pdev->config,
        DEMO_PCIE_EP_VENDOR_ID);

    pci_config_set_device_id(
        pdev->config,
        DEMO_PCIE_EP_DEVICE_ID);

    /*
     * Generic / experimental device class
     *
     * Base class = 0xff
     * Subclass  = 0x00
     */
    pci_config_set_class(
        pdev->config,
        PCI_CLASS_OTHERS);


    /*
     * BAR0
     *
     * 4 KiB MMIO
     */
    memory_region_init_io(
        &s->bar0,
        OBJECT(s),
        &demo_pcie_ep_ops,
        s,
        "demo-pcie-ep-bar0",
        DEMO_PCIE_EP_BAR0_SIZE);

    pci_register_bar(
        pdev,
        0,
        PCI_BASE_ADDRESS_SPACE_MEMORY,
        &s->bar0);


    /*
     * Initial device status
     */
    s->status = STATUS_READY;

    s->control = 0;
    s->scratch = 0;

    qemu_log_mask(
        LOG_GUEST_ERROR,
        "%s: realize\n",
        TYPE_DEMO_PCIE_EP);
}


/*
 * PCI device exit
 */
static void demo_pcie_ep_exit(
        PCIDevice *pdev)
{
    qemu_log_mask(
        LOG_GUEST_ERROR,
        "%s: exit\n",
        TYPE_DEMO_PCIE_EP);
}


/*
 * Instance initialization
 */
static void demo_pcie_ep_init(
        Object *obj)
{
    DemoPCIEEPState *s = DEMO_PCIE_EP(obj);

    s->control = 0;
    s->status = 0;
    s->scratch = 0;
}


/*
 * Class initialization
 */
static void demo_pcie_ep_class_init(
        ObjectClass *klass,
        const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = demo_pcie_ep_realize;
    k->exit = demo_pcie_ep_exit;

    /*
     * No hotplug for this lab device.
     */
    dc->desc = "Simple PCIe Endpoint Device";
}


/*
 * QEMU type information
 */
static const TypeInfo demo_pcie_ep_info = {
    .name          = TYPE_DEMO_PCIE_EP,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(DemoPCIEEPState),

    .instance_init = demo_pcie_ep_init,
    .class_init    = demo_pcie_ep_class_init,

    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { },
    },
};


/*
 * Register device
 */
static void demo_pcie_ep_register_types(void)
{
    type_register_static(&demo_pcie_ep_info);
}

type_init(demo_pcie_ep_register_types);