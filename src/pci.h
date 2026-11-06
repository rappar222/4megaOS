#ifndef PCI_H
#define PCI_H

#include "types.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

struct pci_device {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_id;
    uint8_t subclass;
    uint32_t bar0;
    uint8_t irq;
};

void pci_init(void);
uint32_t pci_read_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_write_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val);
void pci_enable_bus_mastering(uint8_t bus, uint8_t slot, uint8_t func);
bool pci_find_device(uint16_t vendor_id, uint16_t device_id, struct pci_device* dev);
bool pci_find_network_card(struct pci_device* dev);

#endif
