/*
 * pci_bus.h
 *
 *  Created on: 4.10.2016 ã.
 *      Author: Anton Angelov
 */

#ifndef DRIVERS_PCI_BUS_H_
#define DRIVERS_PCI_BUS_H_

#include <types.h>

/* IO ports */
#define PCI_CONFIG_ADDRESS		0xCF8
#define PCI_CONFIG_DATA 		0xCFC

#define PCI_MAX_BUSES			256
#define PCI_MAX_DEVICES			32
#define PCI_MAX_FUNCTIONS		8
#define PCI_CONFIGHEADER_SIZE	256

/* Header types */
#define PCI_HEADERTYPE_NORMAL	0x00
#define PCI_HEADERTYPE_BRIDGE	0x01
#define PCI_HEADERTYPE_CARDBUS	0x02
#define PCI_HEADERTYPE_MULTI	0x80

/* Configuration space fields */
#define PCI_VENDOR_ID			0x00 //2
#define PCI_DEVICE_ID			0x02 //2
#define PCI_COMMAND				0x04 //2
#define PCI_STATUS				0x06 //2
#define PCI_REVISION_ID			0x08 //1
#define PCI_PROG_IF				0x09 //1
#define PCI_SUBCLASS			0x0a //1
#define PCI_CLASS				0x0b //1
#define PCI_CACHE_LINE_SIZE		0x0c //1
#define PCI_LATENCY_TIMER		0x0d //1
#define PCI_HEADER_TYPE			0x0e //1
#define PCI_BIST				0x0f //1
#define PCI_BAR0				0x10 //4
#define PCI_BAR1				0x14 //4
#define PCI_BAR2				0x18 //4
#define PCI_BAR3				0x1C //4
#define PCI_BAR4				0x20 //4
#define PCI_BAR5				0x24 //4
#define PCI_SECONADRY_BUS		0x19 //1

/* PCI class */
#define PCI_CLASS_BRIDGE		0x06

/* Class/subclass selectors */
#define PCI_CLASS_SELECTOR_ALL		0xFFFFFFFF
#define PCI_SUBCLASS_SELECTOR_ALL 	0xFFFFFFFF

/**
 * Describes the config address register used for requesting access
 * to particular register inside the configuration space.
 */
typedef struct K_PCI_CONFIG_ADDRESS K_PCI_CONFIG_ADDRESS;
struct K_PCI_CONFIG_ADDRESS {
	uint8_t	zero_bits 	: 2;
	uint8_t register_id : 6;
	uint8_t function_id : 3;
	uint8_t device_id 	: 5;
	uint8_t bus_id 		: 8;
	uint8_t reserved 	: 7;
	uint8_t enable_bit 	: 1;
} __packed;

/**
 * Callback function for scanning routines.
 */
typedef HRESULT (*K_PCI_SCAN_CALLBACK)(K_PCI_CONFIG_ADDRESS addr, uint32_t vendor_id, uint32_t device_id, void *user);

/**
 * Callback structure, holding a function and userdata pointer.
 */
typedef struct K_PCI_SCAN_PARAMS K_PCI_SCAN_PARAMS;
struct K_PCI_SCAN_PARAMS {
	K_PCI_SCAN_CALLBACK cb;
	void 		*user;
	uint32_t 	class_selector;
	uint32_t 	subclass_selector;
};

K_PCI_CONFIG_ADDRESS MAKE_CONFIG_ADDRESS(uint32_t bus, uint32_t device, uint32_t func, uint32_t offset);

/*
 * Read/write routines for accessing the configuration space.
 */
void __nxapi pci_read_config_u32(K_PCI_CONFIG_ADDRESS addr, uint32_t *out);
void __nxapi pci_write_config_u32(K_PCI_CONFIG_ADDRESS addr, uint32_t value);
void __nxapi pci_read_config_u16(K_PCI_CONFIG_ADDRESS addr, uint16_t *out);
void __nxapi pci_write_config_u16(K_PCI_CONFIG_ADDRESS addr, uint16_t value);
void __nxapi pci_read_config_u8(K_PCI_CONFIG_ADDRESS addr, uint8_t *out);
void __nxapi pci_write_config_u8(K_PCI_CONFIG_ADDRESS addr, uint8_t value);

uint16_t __nxapi pci_get_vendor_id(uint32_t bus, uint32_t device, uint32_t func);
uint16_t __nxapi pci_get_device_id(uint32_t bus, uint32_t device, uint32_t func);

/*
 * Name lookup routines
 */
const char *pci_get_vendor_name(uint16_t vendor_id);
const char *pci_get_device_name(uint16_t vendor_id, uint16_t device_id);

/*
 * Scanning routines.
 */
HRESULT __nxapi pci_scan_bus(uint32_t bus_id, K_PCI_SCAN_PARAMS *params);
HRESULT __nxapi pci_scan_slot(uint32_t bus_id, uint32_t device_id, K_PCI_SCAN_PARAMS *params);
HRESULT __nxapi pci_scan_function(uint32_t bus_id, uint32_t device_id, uint32_t func_id, K_PCI_SCAN_PARAMS *params);
HRESULT __nxapi pci_scan(K_PCI_SCAN_PARAMS *params);
HRESULT __nxapi pci_find_device(uint32_t vendor_id, uint32_t device_id, K_PCI_CONFIG_ADDRESS *addr_out);

#endif /* DRIVERS_PCI_BUS_H_ */
