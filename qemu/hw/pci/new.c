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

#include "hw/pci/pci_device.h"
#include "hw/pci/pci_bus.h"

#include "qemu/module.h"
#include "qemu/log.h"
#include "qapi/error.h"

#include "exec/memory.h"

#define TYPE_MY_PCIE_EP "my-pcie-ep"

OBJECT_DECLARE_SIMPLE_TYPE(
    MyPCIEEPState,
    MY_PCIE_EP
)


/*
 * ------------------------------------------------------------
 * PCI IDs
 * ------------------------------------------------------------
 */

#define MY_PCIE_EP_VENDOR_ID       0x1234
#define MY_PCIE_EP_DEVICE_ID       0x5678

#define MY_PCIE_EP_BAR0_SIZE       0x1000


/*
 * ------------------------------------------------------------
 * BAR0 Register Map
 * ------------------------------------------------------------
 */

#define REG_VERSION               0x000
#define REG_DEVICE_ID             0x004
#define REG_REVISION              0x008
#define REG_CAPABILITY            0x00c

#define REG_CONTROL               0x010
#define REG_STATUS                0x014
#define REG_RESET                 0x018

#define REG_IRQ_ENABLE            0x020
#define REG_IRQ_STATUS            0x024

#define REG_DOORBELL              0x030
#define REG_DOORBELL_STATUS       0x034

#define REG_DMA_ADDR_LO           0x040
#define REG_DMA_ADDR_HI           0x044
#define REG_DMA_LEN               0x048
#define REG_DMA_CONTROL           0x04c
#define REG_DMA_STATUS            0x050

#define REG_SCRATCH0              0x100
#define REG_SCRATCH1              0x104
#define REG_SCRATCH2              0x108
#define REG_SCRATCH3              0x10c


/*
 * ------------------------------------------------------------
 * Constant Register Values
 * ------------------------------------------------------------
 */

#define VERSION_VALUE             0x00010000
#define DEVICE_ID_VALUE           0x12345678
#define REVISION_VALUE            0x00000001

#define CAP_DMA                   BIT(0)
#define CAP_MSI                   BIT(1)
#define CAP_MSIX                  BIT(2)


/*
 * ------------------------------------------------------------
 * CONTROL
 * ------------------------------------------------------------
 *
 * bit 0:
 *   ENABLE
 *
 * bit 1:
 *   START
 *
 * bit 2:
 *   RESET
 */

#define CONTROL_ENABLE             BIT(0)
#define CONTROL_START              BIT(1)


/*
 * ------------------------------------------------------------
 * STATUS
 * ------------------------------------------------------------
 */

#define STATUS_READY               BIT(0)
#define STATUS_ENABLED             BIT(1)
#define STATUS_BUSY                BIT(2)
#define STATUS_ERROR               BIT(3)


/*
 * ------------------------------------------------------------
 * IRQ
 * ------------------------------------------------------------
 */

#define IRQ_DMA_DONE               BIT(0)
#define IRQ_DOORBELL               BIT(1)
#define IRQ_ERROR                  BIT(2)


/*
 * ------------------------------------------------------------
 * DOORBELL
 * ------------------------------------------------------------
 */

#define DOORBELL_RX                BIT(0)
#define DOORBELL_TX                BIT(1)
#define DOORBELL_DMA               BIT(2)


/*
 * ------------------------------------------------------------
 * DMA_CONTROL
 * ------------------------------------------------------------
 */

#define DMA_CONTROL_START          BIT(0)
#define DMA_CONTROL_DIR_READ       BIT(1)
#define DMA_CONTROL_IRQ_ENABLE     BIT(2)


/*
 * ------------------------------------------------------------
 * DMA_STATUS
 * ------------------------------------------------------------
 */

#define DMA_STATUS_IDLE            0
#define DMA_STATUS_BUSY            BIT(0)
#define DMA_STATUS_DONE            BIT(1)
#define DMA_STATUS_ERROR           BIT(2)


/*
 * ------------------------------------------------------------
 * RESET
 * ------------------------------------------------------------
 */

#define RESET_DEVICE               BIT(0)
#define RESET_DMA                  BIT(1)
#define RESET_IRQ                  BIT(2)


/*
 * ------------------------------------------------------------
 * Device State
 * ------------------------------------------------------------
 */

typedef struct MyPCIEEPState {

    PCIDevice parent_obj;

    /*
     * BAR0
     */
    MemoryRegion bar0;


    /*
     * CONTROL / STATUS
     */
    uint32_t control;
    uint32_t status;


    /*
     * IRQ
     */
    uint32_t irq_enable;
    uint32_t irq_status;


    /*
     * Doorbell
     */
    uint32_t doorbell_status;


    /*
     * DMA
     */
    uint64_t dma_addr;
    uint32_t dma_len;

    uint32_t dma_control;
    uint32_t dma_status;


    /*
     * Scratch registers
     */
    uint32_t scratch0;
    uint32_t scratch1;
    uint32_t scratch2;
    uint32_t scratch3;

} MyPCIEEPState;


/*
 * ------------------------------------------------------------
 * Device Reset
 * ------------------------------------------------------------
 */

static void my_pcie_ep_reset_device(
        MyPCIEEPState *s)
{
    qemu_log_mask(
        LOG_GUEST,
        TYPE_MY_PCIE_EP ": device reset\n");


    /*
     * CONTROL
     */
    s->control = 0;


    /*
     * STATUS
     */
    s->status =
        STATUS_READY;


    /*
     * IRQ
     */
    s->irq_enable = 0;
    s->irq_status = 0;


    /*
     * Doorbell
     */
    s->doorbell_status = 0;


    /*
     * DMA
     */
    s->dma_addr = 0;
    s->dma_len = 0;
    s->dma_control = 0;
    s->dma_status = DMA_STATUS_IDLE;


    /*
     * Scratch
     */
    s->scratch0 = 0;
    s->scratch1 = 0;
    s->scratch2 = 0;
    s->scratch3 = 0;
}


/*
 * ------------------------------------------------------------
 * BAR0 Read
 * ------------------------------------------------------------
 */

static uint64_t my_pcie_ep_read(
        void *opaque,
        hwaddr addr,
        unsigned size)
{
    MyPCIEEPState *s = opaque;

    uint32_t value = 0;


    /*
     * This device only supports 32-bit register access.
     */
    if (size != 4) {

        qemu_log_mask(
            LOG_GUEST_ERROR,
            TYPE_MY_PCIE_EP
            ": invalid read size=%u addr=0x%"
            HWADDR_PRIx "\n",
            size,
            addr);

        return 0;
    }


    switch (addr) {

    /*
     * --------------------------------------------------------
     * Identification
     * --------------------------------------------------------
     */

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

        value =
            CAP_DMA |
            CAP_MSI |
            CAP_MSIX;

        break;


    /*
     * --------------------------------------------------------
     * CONTROL / STATUS
     * --------------------------------------------------------
     */

    case REG_CONTROL:

        value = s->control;

        break;


    case REG_STATUS:

        value = s->status;

        break;


    /*
     * RESET is write-only
     */

    case REG_RESET:

        qemu_log_mask(
            LOG_GUEST_ERROR,
            TYPE_MY_PCIE_EP
            ": read from write-only RESET\n");

        value = 0;

        break;


    /*
     * --------------------------------------------------------
     * IRQ
     * --------------------------------------------------------
     */

    case REG_IRQ_ENABLE:

        value = s->irq_enable;

        break;


    case REG_IRQ_STATUS:

        value = s->irq_status;

        break;


    /*
     * --------------------------------------------------------
     * Doorbell
     * --------------------------------------------------------
     */

    case REG_DOORBELL:

        qemu_log_mask(
            LOG_GUEST_ERROR,
            TYPE_MY_PCIE_EP
            ": read from write-only DOORBELL\n");

        value = 0;

        break;


    case REG_DOORBELL_STATUS:

        value = s->doorbell_status;

        break;


    /*
     * --------------------------------------------------------
     * DMA
     * --------------------------------------------------------
     */

    case REG_DMA_ADDR_LO:

        value =
            (uint32_t)
            (s->dma_addr & 0xffffffff);

        break;


    case REG_DMA_ADDR_HI:

        value =
            (uint32_t)
            (s->dma_addr >> 32);

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


    /*
     * --------------------------------------------------------
     * Scratch
     * --------------------------------------------------------
     */

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


    /*
     * --------------------------------------------------------
     * Invalid register
     * --------------------------------------------------------
     */

    default:

        qemu_log_mask(
            LOG_GUEST_ERROR,
            TYPE_MY_PCIE_EP
            ": invalid read addr=0x%"
            HWADDR_PRIx "\n",
            addr);

        value = 0;

        break;
    }


    qemu_log_mask(
        LOG_TRACE,
        TYPE_MY_PCIE_EP
        ": BAR0 READ "
        "addr=0x%" HWADDR_PRIx
        " value=0x%08x\n",
        addr,
        value);


    return value;
}


/*
 * ------------------------------------------------------------
 * BAR0 Write
 * ------------------------------------------------------------
 */

static void my_pcie_ep_write(
        void *opaque,
        hwaddr addr,
        uint64_t val,
        unsigned size)
{
    MyPCIEEPState *s = opaque;

    uint32_t value = (uint32_t)val;


    if (size != 4) {

        qemu_log_mask(
            LOG_GUEST_ERROR,
            TYPE_MY_PCIE_EP
            ": invalid write size=%u "
            "addr=0x%" HWADDR_PRIx "\n",
            size,
            addr);

        return;
    }


    qemu_log_mask(
        LOG_TRACE,
        TYPE_MY_PCIE_EP
        ": BAR0 WRITE "
        "addr=0x%" HWADDR_PRIx
        " value=0x%08x\n",
        addr,
        value);


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

        qemu_log_mask(
            LOG_GUEST_ERROR,
            TYPE_MY_PCIE_EP
            ": write to read-only register "
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
        s->control =
            value &
            (CONTROL_ENABLE |
             CONTROL_START);


        if (s->control & CONTROL_ENABLE) {

            s->status |=
                STATUS_ENABLED;

        } else {

            s->status &=
                ~STATUS_ENABLED;
        }


        if (s->control & CONTROL_START) {

            s->status |=
                STATUS_BUSY;
        }

        break;


    /*
     * --------------------------------------------------------
     * STATUS is read-only
     * --------------------------------------------------------
     */

    case REG_STATUS:

        qemu_log_mask(
            LOG_GUEST_ERROR,
            TYPE_MY_PCIE_EP
            ": write to read-only STATUS\n");

        break;


    /*
     * --------------------------------------------------------
     * RESET
     * --------------------------------------------------------
     */

    case REG_RESET:

        if (value & RESET_DEVICE) {

            my_pcie_ep_reset_device(s);

            break;
        }


        if (value & RESET_DMA) {

            s->dma_addr = 0;
            s->dma_len = 0;
            s->dma_control = 0;
            s->dma_status =
                DMA_STATUS_IDLE;
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

        s->irq_enable =
            value &
            (IRQ_DMA_DONE |
             IRQ_DOORBELL |
             IRQ_ERROR);

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

        s->irq_status &=
            ~value;

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

            s->doorbell_status |=
                DOORBELL_RX;

            s->irq_status |=
                IRQ_DOORBELL;
        }


        if (value & DOORBELL_TX) {

            s->doorbell_status |=
                DOORBELL_TX;

            s->irq_status |=
                IRQ_DOORBELL;
        }


        if (value & DOORBELL_DMA) {

            s->doorbell_status |=
                DOORBELL_DMA;

            s->irq_status |=
                IRQ_DMA_DONE;
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

        s->doorbell_status &=
            ~value;

        break;


    /*
     * --------------------------------------------------------
     * DMA ADDRESS
     * --------------------------------------------------------
     */

    case REG_DMA_ADDR_LO:

        s->dma_addr =
            (s->dma_addr &
             0xffffffff00000000ULL) |
            (uint64_t)value;

        break;


    case REG_DMA_ADDR_HI:

        s->dma_addr =
            (s->dma_addr &
             0x00000000ffffffffULL) |
            ((uint64_t)value << 32);

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

        s->dma_control =
            value &
            (DMA_CONTROL_START |
             DMA_CONTROL_DIR_READ |
             DMA_CONTROL_IRQ_ENABLE);


        if (value & DMA_CONTROL_START) {

            s->dma_status =
                DMA_STATUS_BUSY;

            s->status |=
                STATUS_BUSY;


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

        qemu_log_mask(
            LOG_GUEST_ERROR,
            TYPE_MY_PCIE_EP
            ": write to read-only "
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

        qemu_log_mask(
            LOG_GUEST_ERROR,
            TYPE_MY_PCIE_EP
            ": invalid write "
            "addr=0x%" HWADDR_PRIx
            " value=0x%08x\n",
            addr,
            value);

        break;
    }
}


/*
 * ------------------------------------------------------------
 * MemoryRegion Operations
 * ------------------------------------------------------------
 */

static const MemoryRegionOps my_pcie_ep_ops = {

    .read = my_pcie_ep_read,

    .write = my_pcie_ep_write,

    .endianness =
        DEVICE_LITTLE_ENDIAN,

    .impl = {

        .min_access_size = 4,

        .max_access_size = 4,
    },
};


/*
 * ------------------------------------------------------------
 * PCI Realize
 * ------------------------------------------------------------
 */

static void my_pcie_ep_realize(
        PCIDevice *pdev,
        Error **errp)
{
    MyPCIEEPState *s =
        MY_PCIE_EP(pdev);


    /*
     * PCI Vendor ID
     */
    pci_config_set_vendor_id(
        pdev->config,
        MY_PCIE_EP_VENDOR_ID);


    /*
     * PCI Device ID
     */
    pci_config_set_device_id(
        pdev->config,
        MY_PCIE_EP_DEVICE_ID);


    /*
     * Generic device class
     */
    pci_config_set_class(
        pdev->config,
        PCI_CLASS_OTHERS);


    /*
     * Initialize BAR0
     */
    memory_region_init_io(
        &s->bar0,
        OBJECT(s),
        &my_pcie_ep_ops,
        s,
        "my-pcie-ep-bar0",
        MY_PCIE_EP_BAR0_SIZE);


    /*
     * Register BAR0
     */
    pci_register_bar(
        pdev,
        0,
        PCI_BASE_ADDRESS_SPACE_MEMORY,
        &s->bar0);


    /*
     * Device reset state
     */
    my_pcie_ep_reset_device(s);


    qemu_log_mask(
        LOG_GUEST,
        TYPE_MY_PCIE_EP
        ": realized\n");
}


/*
 * ------------------------------------------------------------
 * PCI Exit
 * ------------------------------------------------------------
 */

static void my_pcie_ep_exit(
        PCIDevice *pdev)
{
    MyPCIEEPState *s =
        MY_PCIE_EP(pdev);


    memory_region_destroy(
        &s->bar0);


    qemu_log_mask(
        LOG_GUEST,
        TYPE_MY_PCIE_EP
        ": exit\n");
}


/*
 * ------------------------------------------------------------
 * Instance Init
 * ------------------------------------------------------------
 */

static void my_pcie_ep_init(
        Object *obj)
{
    MyPCIEEPState *s =
        MY_PCIE_EP(obj);


    s->control = 0;

    s->status = 0;

    s->irq_enable = 0;

    s->irq_status = 0;

    s->doorbell_status = 0;

    s->dma_addr = 0;

    s->dma_len = 0;

    s->dma_control = 0;

    s->dma_status =
        DMA_STATUS_IDLE;

    s->scratch0 = 0;

    s->scratch1 = 0;

    s->scratch2 = 0;

    s->scratch3 = 0;
}


/*
 * ------------------------------------------------------------
 * Class Init
 * ------------------------------------------------------------
 */

static void my_pcie_ep_class_init(
        ObjectClass *klass,
        const void *data)
{
    DeviceClass *dc =
        DEVICE_CLASS(klass);

    PCIDeviceClass *k =
        PCI_DEVICE_CLASS(klass);


    k->realize =
        my_pcie_ep_realize;

    k->exit =
        my_pcie_ep_exit;


    /*
     * PCI Express device
     */
    k->is_express = true;


    dc->desc =
        "QEMU PCIe Endpoint Lab Device";
}


/*
 * ------------------------------------------------------------
 * Type Information
 * ------------------------------------------------------------
 */

static const TypeInfo my_pcie_ep_info = {

    .name =
        TYPE_MY_PCIE_EP,

    .parent =
        TYPE_PCI_DEVICE,

    .instance_size =
        sizeof(MyPCIEEPState),

    .instance_init =
        my_pcie_ep_init,

    .class_init =
        my_pcie_ep_class_init,
};


/*
 * ------------------------------------------------------------
 * Type Registration
 * ------------------------------------------------------------
 */

static void my_pcie_ep_register_types(void)
{
    type_register_static(
        &my_pcie_ep_info);
}


type_init(
    my_pcie_ep_register_types);