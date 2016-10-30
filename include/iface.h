/*
 * iface.h
 *
 *	Definitions of CORBA-like interfaces.
 *
 *  Created on: 2.10.2016 ã.
 *      Author: Anton Angelov
 */

#ifndef INCLUDE_IFACE_H_
#define INCLUDE_IFACE_H_

#include <types.h>

typedef GUID *LPGUID;
typedef GUID CLSID,	*LPCLSID;
typedef GUID IID,	*LPIID;
typedef GUID FMTID,	*LPFMTID;

/**
 * Calling convention for interface methods.
 */
#define __iface_method __nxapi

/**
 * Defines a GUID constant
 */
#define DEFINE_GUID(id, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
        	const GUID id = { l, w1, w2, { b1, b2,  b3,  b4,  b5,  b6,  b7,  b8 } }

/*
 * NULL GUID
 */
//DEFINE_GUID(GUID_NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
//DEFINE_GUID(IID_IUnknown, 0x00000000, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ,0x46);

/*
 * IUnknown virtual method table.
 */
typedef struct IUnknown IUnknown;
struct IUnknown {
	HRESULT 	__iface_method (*QueryInterface)(IUnknown *this, GUID riid, void **ppObj);
	uint32_t 	__iface_method (*AddRef)();
	uint32_t 	__iface_method (*Release)();
};

#endif /* INCLUDE_IFACE_H_ */
