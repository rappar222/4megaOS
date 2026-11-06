#ifndef E1000_H
#define E1000_H

#include "types.h"
#include "pci.h"

#define E1000_NUM_RX_DESC 32
#define E1000_NUM_TX_DESC 8
#define E1000_PKT_BUF_SIZE 2048

bool e1000_init(struct pci_device* pci_dev);
bool e1000_is_available(void);
void e1000_get_mac(uint8_t* mac);
bool e1000_send(const void* data, uint16_t len);
uint16_t e1000_receive(void* buffer, uint16_t max_len);

#endif
