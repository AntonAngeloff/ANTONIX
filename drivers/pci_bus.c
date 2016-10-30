/*
 * pci_bus.c
 *
 *	Someone has to drive the bus!
 *
 *	Generic Peripheral Component Interconnect (PCI) bus driver.
 *
 *  Created on: 4.10.2016 ã.
 *      Author: Anton Angelov
 */
#include <hal.h>
#include "pci_bus.h"
#include "pci_list.h"

K_PCI_CONFIG_ADDRESS MAKE_CONFIG_ADDRESS(uint32_t bus, uint32_t device, uint32_t func, uint32_t offset)
{
	K_PCI_CONFIG_ADDRESS r = { 0 };

	r.bus_id = bus;
	r.device_id = device;
	r.function_id = func;
	r.register_id = offset >> 2;
	r.enable_bit = 1;
	r.zero_bits = offset & 0x2; //zero bits are cleared on r/w

	return r;
}

void __nxapi pci_read_config_u32(K_PCI_CONFIG_ADDRESS addr, uint32_t *out)
{
	addr.zero_bits = 0;

	WRITE_PORT_ULONG(PCI_CONFIG_ADDRESS, *((uint32_t*)&addr));
	*out = READ_PORT_ULONG(PCI_CONFIG_DATA);
}

void __nxapi pci_write_config_u32(K_PCI_CONFIG_ADDRESS addr, uint32_t value)
{
	addr.zero_bits = 0;

	WRITE_PORT_ULONG(PCI_CONFIG_ADDRESS, *((uint32_t*)&addr));
	WRITE_PORT_ULONG(PCI_CONFIG_DATA, value);
}

void __nxapi pci_read_config_u16(K_PCI_CONFIG_ADDRESS addr, uint16_t *out)
{
	uint8_t offs = addr.zero_bits;
	addr.zero_bits = 0;

	WRITE_PORT_ULONG(PCI_CONFIG_ADDRESS, *((uint32_t*)&addr));
	*out = READ_PORT_USHORT(PCI_CONFIG_DATA + offs);
}

void __nxapi pci_write_config_u16(K_PCI_CONFIG_ADDRESS addr, uint16_t value)
{
	uint8_t offs = addr.zero_bits;
	addr.zero_bits = 0;

	WRITE_PORT_ULONG(PCI_CONFIG_ADDRESS, *((uint32_t*)&addr));
	WRITE_PORT_USHORT(PCI_CONFIG_DATA + offs, value);
}

void __nxapi pci_read_config_u8(K_PCI_CONFIG_ADDRESS addr, uint8_t *out)
{
	uint8_t offs = addr.zero_bits;
	addr.zero_bits = 0;

	WRITE_PORT_ULONG(PCI_CONFIG_ADDRESS, *((uint32_t*)&addr));
	*out = READ_PORT_UCHAR(PCI_CONFIG_DATA + offs);
}

void __nxapi pci_write_config_u8(K_PCI_CONFIG_ADDRESS addr, uint8_t value)
{
	uint8_t offs = addr.zero_bits;
	addr.zero_bits = 0;

	WRITE_PORT_ULONG(PCI_CONFIG_ADDRESS, *((uint32_t*)&addr));
	WRITE_PORT_UCHAR(PCI_CONFIG_DATA + offs, value);
}

uint16_t __nxapi pci_get_vendor_id(uint32_t bus, uint32_t device, uint32_t func)
{
	uint16_t result;
	pci_read_config_u16(MAKE_CONFIG_ADDRESS(bus, device, func, PCI_VENDOR_ID), &result);

	return result;
}

uint16_t __nxapi pci_get_device_id(uint32_t bus, uint32_t device, uint32_t func)
{
	uint16_t result;
	pci_read_config_u16(MAKE_CONFIG_ADDRESS(bus, device, func, PCI_DEVICE_ID), &result);

	return result;
}

const char *pci_get_vendor_name(uint16_t vendor_id)
{
	uint32_t i;

	for (i=0; i<PCI_VENTABLE_LEN; i++) {
		if (PciVenTable[i].VenId == vendor_id) {
			return PciVenTable[i].VenFull;
		}
	}

	return "Unknown";
}

const char *pci_get_device_name(uint16_t vendor_id, uint16_t device_id)
{
	uint32_t i;

	for (i=0; i<PCI_DEVTABLE_LEN; i++) {
		if (PciDevTable[i].VenId == vendor_id && PciDevTable[i].DevId == device_id) {
			return PciDevTable[i].ChipDesc;
		}
	}

	return "Unknown";
}

HRESULT __nxapi pci_scan_bus(uint32_t bus_id, K_PCI_SCAN_PARAMS *params)
{
	uint32_t 	i;
	HRESULT 	hr;

	for (i=0; i<PCI_MAX_DEVICES; i++) {
		hr = pci_scan_slot(bus_id, i, params);
		if (FAILED(hr)) return hr;
	}

	return S_OK;
}

HRESULT __nxapi pci_scan_slot(uint32_t bus_id, uint32_t device_id, K_PCI_SCAN_PARAMS *params)
{
	HRESULT 	hr;
	uint32_t 	i;
	uint8_t 	hdr_type;

	if (pci_get_vendor_id(bus_id, device_id, 0) == 0xFFFF) {
		/* Device doesn't exist */
		return S_FALSE;
	}

	/* Device exists */
	hr = pci_scan_function(bus_id, device_id, 0, params);
	if (FAILED(hr)) return hr;

	/* Get header type */
	pci_read_config_u8(MAKE_CONFIG_ADDRESS(bus_id, device_id, 0, PCI_HEADER_TYPE), &hdr_type);

	if ((hdr_type & 0x80) == 0) {
		/* If bit 7 is cleared, the devices doesn't have multiple functions */
		return S_OK;
	}

	/* Check remaining functions */
	for (i=1; i<PCI_MAX_FUNCTIONS; i++) {
		if (pci_get_vendor_id(bus_id, device_id, i) != 0xFFFF) {
			hr = pci_scan_function(bus_id, device_id, i, params);
			if (FAILED(hr)) return hr;
		}
	}

	return S_OK;
}

HRESULT __nxapi pci_scan_function(uint32_t bus_id, uint32_t device_id, uint32_t func_id, K_PCI_SCAN_PARAMS *params)
{
	HRESULT hr;
	uint8_t class;
	uint8_t subclass;
	uint8_t secondary_bus;

	/* Get class/subclass */
	pci_read_config_u8(MAKE_CONFIG_ADDRESS(bus_id, device_id, func_id, PCI_CLASS), &class);
	pci_read_config_u8(MAKE_CONFIG_ADDRESS(bus_id, device_id, func_id, PCI_SUBCLASS), &subclass);

	/* Get vendor/device id */
	uint16_t cas_vendor_id = pci_get_vendor_id(bus_id, device_id, func_id);
	uint16_t cas_device_id = pci_get_device_id(bus_id, device_id, func_id);

	/* Match class/subclass */
	if ((params->class_selector == PCI_CLASS_SELECTOR_ALL || params->class_selector == class) &&
			(params->subclass_selector == PCI_SUBCLASS_SELECTOR_ALL || params->subclass_selector == subclass)) {
		/* Invoke CB */
		hr = params->cb(MAKE_CONFIG_ADDRESS(bus_id, device_id, func_id, 0), cas_vendor_id, cas_device_id, params->user);
		if (FAILED(hr)) return hr;
	}

	/* If is a PCI bridge, scan it recursively. */
	if (class == PCI_CLASS_BRIDGE && subclass == 0x04) {
		pci_read_config_u8(MAKE_CONFIG_ADDRESS(bus_id, device_id, func_id, PCI_SECONADRY_BUS), &secondary_bus);

		hr = pci_scan_bus(secondary_bus, params);
		if (FAILED(hr)) return hr;
	}

	return S_OK;
}

HRESULT __nxapi pci_scan(K_PCI_SCAN_PARAMS *params)
{
	HRESULT 	hr;
	uint8_t 	hdr_type;
	uint32_t 	i;

	/* Get header type */
	pci_read_config_u8(MAKE_CONFIG_ADDRESS(0, 0, 0, PCI_HEADER_TYPE), &hdr_type);

	if ((hdr_type & 0x80) == 0) {
		/* Single PCI host controller */
		hr = pci_scan_bus(0, params);
		if (FAILED(hr)) return hr;
	} else {
		/* Multiple PCI host controllers */
		for (i=0; i<PCI_MAX_FUNCTIONS; i++) {
			/* If vendor id is 0xFFFF, then devise doesn't exist. */
			if (pci_get_vendor_id(0, 0, i) == 0xFFFF) {
				break;
			}

			/* Scan */
			hr = pci_scan_bus(i, params);
			if (FAILED(hr)) return hr;
		}
	}

	return S_OK;
}

static HRESULT __nxapi pci_find_device_cb(K_PCI_CONFIG_ADDRESS addr, uint32_t vendor_id, uint32_t device_id, void *user)
{
	struct {
		K_PCI_CONFIG_ADDRESS *addr;
		uint16_t vid;
		uint16_t did;
	} *data = user;

	if (data->vid == vendor_id && data->did == device_id) {
		/* Found */
		*data->addr = addr;
		return E_TERMINATED;
	}

	return S_OK;
}

HRESULT __nxapi pci_find_device(uint32_t vendor_id, uint32_t device_id, K_PCI_CONFIG_ADDRESS *addr_out)
{
	K_PCI_SCAN_PARAMS p;
	HRESULT hr;

	struct {
		K_PCI_CONFIG_ADDRESS *addr;
		uint16_t vid;
		uint16_t did;
	} user_data = {
		addr_out,
		vendor_id,
		device_id
	};

	p.cb = pci_find_device_cb;
	p.user = &user_data;
	p.class_selector = PCI_CLASS_SELECTOR_ALL;
	p.subclass_selector = PCI_SUBCLASS_SELECTOR_ALL;

	hr = pci_scan(&p);
	return hr == E_TERMINATED ? S_OK : E_NOTFOUND;
}
