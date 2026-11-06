#include "e1000.h"
#include "string.h"
#include "vga.h"
#include "timer.h"

// E1000 Register Offsets
#define REG_CTRL     0x00000
#define REG_STATUS   0x00008
#define REG_EECD     0x00018
#define REG_EERD     0x00014
#define REG_ICR      0x000D0
#define REG_IMS      0x000D8
#define REG_IMC      0x000E0
#define REG_RCTRL    0x00100
#define REG_RDBAL    0x02800
#define REG_RDBAH    0x02804
#define REG_RDLEN    0x02808
#define REG_RDH      0x02810
#define REG_RDT      0x02818
#define REG_TCTRL    0x00400
#define REG_TDBAL    0x03800
#define REG_TDBAH    0x03804
#define REG_TDLEN    0x03808
#define REG_TDH      0x03810
#define REG_TDT      0x03818
#define REG_RAL      0x05400
#define REG_RAH      0x05404

#define RCTRL_EN        (1 << 1)
#define RCTRL_SBP       (1 << 2)
#define RCTRL_UPE       (1 << 3)
#define RCTRL_MPE       (1 << 4)
#define RCTRL_BAM       (1 << 15)
#define RCTRL_SECRC     (1 << 26)

#define TCTRL_EN        (1 << 1)
#define TCTRL_PSP       (1 << 3)

struct e1000_rx_desc {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} __attribute__((packed, aligned(16)));

struct e1000_tx_desc {
    uint64_t addr;
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t special;
} __attribute__((packed, aligned(16)));

static uintptr_t mmio_base = 0;
static bool has_e1000 = false;
static uint8_t mac_addr[6] = {0};

static struct e1000_rx_desc rx_descs[E1000_NUM_RX_DESC] __attribute__((aligned(16)));
static uint8_t rx_buffers[E1000_NUM_RX_DESC][E1000_PKT_BUF_SIZE] __attribute__((aligned(16)));
static uint16_t rx_tail = 0;

static struct e1000_tx_desc tx_descs[E1000_NUM_TX_DESC] __attribute__((aligned(16)));
static uint8_t tx_buffers[E1000_NUM_TX_DESC][E1000_PKT_BUF_SIZE] __attribute__((aligned(16)));
static uint16_t tx_tail = 0;

static inline void write_reg(uint16_t reg, uint32_t val) {
    *(volatile uint32_t*)(mmio_base + reg) = val;
}

static inline uint32_t read_reg(uint16_t reg) {
    return *(volatile uint32_t*)(mmio_base + reg);
}

static uint16_t read_eeprom(uint8_t addr) {
    write_reg(REG_EERD, 1 | ((uint32_t)addr << 8));
    uint32_t timeout = 50000;
    while (!(read_reg(REG_EERD) & (1 << 4)) && --timeout);
    return (uint16_t)((read_reg(REG_EERD) >> 16) & 0xFFFF);
}

bool e1000_init(struct pci_device* pci_dev) {
    pci_enable_bus_mastering(pci_dev->bus, pci_dev->slot, pci_dev->func);

    mmio_base = pci_dev->bar0 & ~0xF;
    if (mmio_base == 0) {
        return false;
    }

    // Reset device
    write_reg(REG_CTRL, read_reg(REG_CTRL) | (1 << 26)); // Device Reset
    timer_sleep(2);

    // Disable interrupts (we use polling for ultra-low latency & zero-jitter)
    write_reg(REG_IMC, 0xFFFFFFFF);
    write_reg(REG_CTRL, read_reg(REG_CTRL) | (1 << 5)); // Set Link Up (SLU)

    // Read MAC address from RAL0/RAH0
    uint32_t ral = read_reg(REG_RAL);
    uint32_t rah = read_reg(REG_RAH);

    if (ral != 0 && (rah & (1 << 31))) {
        mac_addr[0] = (uint8_t)(ral & 0xFF);
        mac_addr[1] = (uint8_t)((ral >> 8) & 0xFF);
        mac_addr[2] = (uint8_t)((ral >> 16) & 0xFF);
        mac_addr[3] = (uint8_t)((ral >> 24) & 0xFF);
        mac_addr[4] = (uint8_t)(rah & 0xFF);
        mac_addr[5] = (uint8_t)((rah >> 8) & 0xFF);
    } else {
        // Read from EEPROM
        uint16_t w0 = read_eeprom(0);
        uint16_t w1 = read_eeprom(1);
        uint16_t w2 = read_eeprom(2);
        mac_addr[0] = (uint8_t)(w0 & 0xFF);
        mac_addr[1] = (uint8_t)(w0 >> 8);
        mac_addr[2] = (uint8_t)(w1 & 0xFF);
        mac_addr[3] = (uint8_t)(w1 >> 8);
        mac_addr[4] = (uint8_t)(w2 & 0xFF);
        mac_addr[5] = (uint8_t)(w2 >> 8);
    }

    // Initialize RX descriptors
    for (int i = 0; i < E1000_NUM_RX_DESC; i++) {
        rx_descs[i].addr = (uint64_t)(uintptr_t)&rx_buffers[i][0];
        rx_descs[i].status = 0;
        rx_descs[i].errors = 0;
    }
    write_reg(REG_RDBAL, (uint32_t)(uintptr_t)&rx_descs[0]);
    write_reg(REG_RDBAH, 0);
    write_reg(REG_RDLEN, E1000_NUM_RX_DESC * sizeof(struct e1000_rx_desc));
    write_reg(REG_RDH, 0);
    write_reg(REG_RDT, E1000_NUM_RX_DESC - 1);
    rx_tail = 0;

    // Enable RX
    write_reg(REG_RCTRL, RCTRL_EN | RCTRL_SBP | RCTRL_UPE | RCTRL_MPE | RCTRL_BAM | RCTRL_SECRC);

    // Initialize TX descriptors
    for (int i = 0; i < E1000_NUM_TX_DESC; i++) {
        tx_descs[i].addr = (uint64_t)(uintptr_t)&tx_buffers[i][0];
        tx_descs[i].cmd = 0;
        tx_descs[i].status = 1; // Mark as done initially
    }
    write_reg(REG_TDBAL, (uint32_t)(uintptr_t)&tx_descs[0]);
    write_reg(REG_TDBAH, 0);
    write_reg(REG_TDLEN, E1000_NUM_TX_DESC * sizeof(struct e1000_tx_desc));
    write_reg(REG_TDH, 0);
    write_reg(REG_TDT, 0);
    tx_tail = 0;

    // Enable TX: Pad Short Packets (PSP) + Enable (EN) + Collisions
    write_reg(REG_TCTRL, TCTRL_EN | TCTRL_PSP | (15 << 4) | (64 << 12));

    has_e1000 = true;
    return true;
}

bool e1000_is_available(void) {
    return has_e1000;
}

void e1000_get_mac(uint8_t* mac) {
    memcpy(mac, mac_addr, 6);
}

bool e1000_send(const void* data, uint16_t len) {
    if (!has_e1000 || len > E1000_PKT_BUF_SIZE) return false;

    // Wait if descriptor is not yet done
    uint32_t timeout = 50000;
    while (!(tx_descs[tx_tail].status & 0x01) && --timeout);

    memcpy(&tx_buffers[tx_tail][0], data, len);
    tx_descs[tx_tail].length = len;
    tx_descs[tx_tail].cmd = (1 << 0) /* EOP */ | (1 << 3) /* RS */;
    tx_descs[tx_tail].status = 0;

    uint16_t prev = tx_tail;
    tx_tail = (tx_tail + 1) % E1000_NUM_TX_DESC;
    write_reg(REG_TDT, tx_tail);

    (void)prev;
    return true;
}

uint16_t e1000_receive(void* buffer, uint16_t max_len) {
    if (!has_e1000) return 0;

    if (!(rx_descs[rx_tail].status & 0x01)) {
        return 0; // No packet ready
    }

    uint16_t len = rx_descs[rx_tail].length;
    if (len > max_len) len = max_len;

    memcpy(buffer, &rx_buffers[rx_tail][0], len);
    rx_descs[rx_tail].status = 0;

    uint16_t cur = rx_tail;
    rx_tail = (rx_tail + 1) % E1000_NUM_RX_DESC;
    write_reg(REG_RDT, cur);

    return len;
}
