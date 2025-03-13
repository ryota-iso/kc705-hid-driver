#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/hrtimer.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/version.h>

#define DRV_NAME "pciehid"
#define DRV_VERSION "0.1"

static const struct pci_device_id pciehid_pci_tbl[] = {
    {0x3776, 0x8020, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
    {0},
};
MODULE_DEVICE_TABLE(pci, pciehid_pci_tbl);

static struct pci_dev *pcidev;
static unsigned char *mmio0_ptr;
static unsigned long mmio0_start, mmio0_len;

static struct input_dev *pciehid_inputdev;

static unsigned int last_val;
static int last_key;

static struct hrtimer poll_timer;
static ktime_t poll_interval_ns;

static enum hrtimer_restart pciehid_hrtimer_callback(struct hrtimer *timer) {
  unsigned int val = 0;

  if (mmio0_ptr) {
    val = readl(mmio0_ptr);
  }

  if (val != last_val) {
    if (last_key) {
      input_report_key(pciehid_inputdev, last_key, 0);
      input_sync(pciehid_inputdev);
      last_key = 0;
    }

    if (val != 0) {
      if (val == 0xffffffffU) {
        input_report_key(pciehid_inputdev, KEY_1, 1);
        input_sync(pciehid_inputdev);
        last_key = KEY_1;
      } else {
        dev_warn(&pcidev->dev, "Unknown key code: 0x%08x\n", val);
      }
    }
    last_val = val;
  }

  hrtimer_forward_now(timer, poll_interval_ns);
  return HRTIMER_RESTART;
}

// MARK: probe
static int pciehid_probe(struct pci_dev *pdev, const struct pci_device_id *ent) {
  int rc;

  rc = pci_enable_device(pdev);
  if (rc) return rc;

  rc = pci_request_regions(pdev, DRV_NAME);
  if (rc) {
    pci_disable_device(pdev);
    return rc;
  }

  mmio0_start = pci_resource_start(pdev, 0);
  mmio0_len = pci_resource_len(pdev, 0);
  mmio0_ptr = ioremap(mmio0_start, mmio0_len);
  if (!mmio0_ptr) {
    dev_err(&pdev->dev, "ioremap failed for BAR0\n");
    rc = -ENOMEM;
    goto err_release_regions;
  }

  pcidev = pdev;
  pci_set_master(pdev);

  pciehid_inputdev = input_allocate_device();
  if (!pciehid_inputdev) {
    dev_err(&pdev->dev, "Failed to allocate input device\n");
    rc = -ENOMEM;
    goto err_unmap;
  }

  pciehid_inputdev->name = "pciehid-keyboard";
  pciehid_inputdev->phys = "pciehid/input0";
  pciehid_inputdev->id.bustype = BUS_PCI;
  pciehid_inputdev->id.vendor = 0x3776;
  pciehid_inputdev->id.product = 0x8020;
  pciehid_inputdev->id.version = 1;

  __set_bit(EV_KEY, pciehid_inputdev->evbit);
  __set_bit(KEY_1, pciehid_inputdev->keybit);
  __set_bit(KEY_2, pciehid_inputdev->keybit);
  __set_bit(KEY_3, pciehid_inputdev->keybit);
  __set_bit(KEY_4, pciehid_inputdev->keybit);
  __set_bit(KEY_5, pciehid_inputdev->keybit);

  rc = input_register_device(pciehid_inputdev);
  if (rc) {
    dev_err(&pdev->dev, "Failed to register input device\n");
    input_free_device(pciehid_inputdev);
    pciehid_inputdev = NULL;
    goto err_unmap;
  }

  last_val = 0;
  last_key = 0;

  hrtimer_init(&poll_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
  poll_timer.function = pciehid_hrtimer_callback;

  poll_interval_ns = ktime_set(0, 1000);  // 1μs

  hrtimer_start(&poll_timer, poll_interval_ns, HRTIMER_MODE_REL);

  dev_info(&pdev->dev, "pciehid probe success (hrtimer polling)\n");
  return 0;

err_unmap:
  if (mmio0_ptr) {
    iounmap(mmio0_ptr);
    mmio0_ptr = NULL;
  }
err_release_regions:
  pci_release_regions(pdev);
  pci_disable_device(pdev);
  return rc;
}

// MARK: remove
static void pciehid_remove(struct pci_dev *pdev) {
  hrtimer_cancel(&poll_timer);

  if (pciehid_inputdev) {
    input_unregister_device(pciehid_inputdev);
    pciehid_inputdev = NULL;
  }

  if (mmio0_ptr) {
    iounmap(mmio0_ptr);
    mmio0_ptr = NULL;
  }
  pci_release_regions(pdev);
  pci_disable_device(pdev);

  dev_info(&pdev->dev, "pciehid removed\n");
}

static struct pci_driver pciehid_pci_driver = {
    .name = DRV_NAME,
    .id_table = pciehid_pci_tbl,
    .probe = pciehid_probe,
    .remove = pciehid_remove,
};

static int __init pciehid_init(void) {
  pr_info("%s: %s\n", DRV_NAME, DRV_VERSION);
  return pci_register_driver(&pciehid_pci_driver);
}

static void __exit pciehid_exit(void) { pci_unregister_driver(&pciehid_pci_driver); }

module_init(pciehid_init);
module_exit(pciehid_exit);

MODULE_DESCRIPTION("PCIe HID Keyboard driver");
MODULE_AUTHOR("ryota-iso");
MODULE_LICENSE("GPL");
