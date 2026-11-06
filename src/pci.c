#include "pci.h"
#include "io.h"
#include "vga.h"

uint32_t pci_read_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((1 << 31)
                                | ((uint32_t)bus << 16)
                                | ((uint32_t)slot << 11)
                                | ((uint32_t)func << 8)
                                | (offset & 0xFC));
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

void pci_write_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    uint32_t address = (uint32_t)((1 << 31)
                                | ((uint32_t)bus << 16)
                                | ((uint32_t)slot << 11)
                                | ((uint32_t)func << 8)
                                | (offset & 0xFC));
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, val);
}

void pci_enable_bus_mastering(uint8_t bus, uint8_t slot, uint8_t func) {
    uint32_t cmd = pci_read_config(bus, slot, func, 0x04);
    // Enable I/O (bit 0), Memory Space (bit 1), Bus Master (bit 2)
    cmd |= 0x07;
    pci_write_config(bus, slot, func, 0x04, cmd);
}

bool pci_find_network_card(struct pci_device* dev) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint32_t reg0 = pci_read_config((uint8_t)bus, slot, func, 0);
                uint16_t vendor_id = (uint16_t)(reg0 & 0xFFFF);
                uint16_t device_id = (uint16_t)(reg0 >> 16);

                if (vendor_id == 0xFFFF || vendor_id == 0x0000) {
                    if (func == 0) break; // No device in this slot
                    continue;
                }

                uint32_t class_reg = pci_read_config((uint8_t)bus, slot, func, 0x08);
                uint8_t class_id = (uint8_t)(class_reg >> 24);
                uint8_t subclass = (uint8_t)(class_reg >> 16);

                // Check for Intel E1000, RTL8139, AMD PCnet, or Network Controller (Class 0x02)
                bool is_net = (class_id == 0x02);
                if (vendor_id == 0x8086) {
                    // Intel E1000 family
                    if (device_id == 0x100E || device_id == 0x1004 || device_id == 0x100F ||
                        device_id == 0x1079 || device_id == 0x107C || device_id == 0x10D3 ||
                        device_id == 0x153A || device_id == 0x10EA) {
                        is_net = true;
                    }
                } else if (vendor_id == 0x10EC && device_id == 0x8139) {
                    is_net = true;
                } else if (vendor_id == 0x1022 && device_id == 0x2000) {
                    is_net = true;
                }

                if (is_net) {
                    dev->bus = (uint8_t)bus;
                    dev->slot = slot;
                    dev->func = func;
                    dev->vendor_id = vendor_id;
                    dev->device_id = device_id;
                    dev->class_id = class_id;
                    dev->subclass = subclass;
                    dev->bar0 = pci_read_config((uint8_t)bus, slot, func, 0x10);
                    uint32_t irq_reg = pci_read_config((uint8_t)bus, slot, func, 0x3C);
                    dev->irq = (uint8_t)(irq_reg & 0xFF);
                    return true;
                }
            }
        }
    }
    return false;
}
