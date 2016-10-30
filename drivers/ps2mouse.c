/*
 * ps2mouse.c
 *
 *	It is originally a good idea to use the routines defined in ps2.h, but
 *	since they need a major overhaul, we will reimplement everything here.
 *
 *  Created on: 5.10.2016 ã.
 *      Author: Anton Angelov
 */
#include <types.h>
#include <hal.h>
#include <desctables.h>
#include <devices.h>
#include <vfs.h>
#include <mm.h>
#include "ps2mouse.h"

/*
 * Prototypes
 */
static HRESULT __nxapi mouse_wait(K_MOUSE_WAIT_REASON rsn);
static HRESULT __nxapi mouse_write(uint8_t data);
static uint8_t	__nxapi mouse_read(HRESULT *hr_out);
static HRESULT __nxapi mouse_enable_ps2_port2(BOOL enabled);
static HRESULT __nxapi mouse_reset();
static HRESULT __nxapi mouse_init(K_DEVICE *self);
static HRESULT __nxapi mouse_fini(K_DEVICE *self);

static void __nxapi mouse_decode_packet(uint8_t *packet, uint8_t size, int16_t *x_movement, int16_t *y_movement, BOOL *button_states);
static HRESULT mouse_register_cb(PS2MOUSE_DRV_CONTEXT *ctx, IOCTL_POINTING_REGISTER_STRUCT *arg);
static HRESULT mouse_unregister_cb(PS2MOUSE_DRV_CONTEXT *ctx, IOCTL_POINTING_REGISTER_STRUCT *arg);
static HRESULT mouse_parse_packet(PS2MOUSE_DRV_CONTEXT *ctx, uint8_t *packet, uint32_t size);
static HRESULT mouse_emit_event(PS2MOUSE_DRV_CONTEXT *ctx, K_MOUSE_EVENT *ev);

static VOID __cdecl mouse_isr(K_REGISTERS regs);


/*
 * Define pointer to the single mouse device's context.
 * Removed later and use datapointer in the ISR. */
static PS2MOUSE_DRV_CONTEXT *__ctx = NULL;

/*
 * Implementation
 */
static HRESULT __nxapi mouse_wait(K_MOUSE_WAIT_REASON rsn)
{
	volatile uint32_t timeout = 100000;
	HRESULT hr = E_TIMEDOUT;

	switch (rsn) {
		case WAIT_READ:
			while (timeout--) {
				/* Wait for bit 0 to get lifted */
				if ((READ_PORT_UCHAR(MOUSE_STATUS_PORT) & 1) == 1) {
					return S_OK;
				}
			}
			break;

		case WAIT_WRITE:
			while (timeout--) {
				/* Wait for bit 1 */
				if ((READ_PORT_UCHAR(MOUSE_STATUS_PORT) & 2) == 0) {
					return S_OK;
				}
			}
			break;

		default:
			return E_FAIL;
	}

	return hr;
}

static HRESULT __nxapi mouse_write(uint8_t data)
{
	HRESULT hr;

	hr = mouse_wait(WAIT_WRITE);
	if (FAILED(hr)) return hr;

	WRITE_PORT_UCHAR(MOUSE_COMMAND_PORT, 0xD4);

	hr = mouse_wait(WAIT_WRITE);
	if (FAILED(hr)) return hr;

	WRITE_PORT_UCHAR(MOUSE_DATA_PORT, data);
	return S_OK;
}

static uint8_t	__nxapi mouse_read(HRESULT *hr_out)
{
	HRESULT hr;

	hr = mouse_wait(WAIT_READ);
	if (hr_out) *hr_out = hr;
	if (FAILED(hr)) return 0;

	return READ_PORT_UCHAR(MOUSE_DATA_PORT);
}

static void __nxapi mouse_decode_packet(uint8_t *packet, uint8_t size, int16_t *x_movement, int16_t *y_movement, BOOL *button_states)
{
	if (size != 3) {
		/* Unsupported packet type */
		*x_movement = 0;
		*y_movement = 0;
		button_states[MOUSE_BUTTON_LEFT] = FALSE;
		button_states[MOUSE_BUTTON_RIGHT] = FALSE;
		button_states[MOUSE_BUTTON_MIDDLE] = FALSE;
		button_states[MOUSE_BUTTON_4] = FALSE;
		button_states[MOUSE_BUTTON_5] = FALSE;

		return;
	}

	*x_movement = (packet[0] >> 4 & 1) ? packet[1] - 256 : packet[1];
	*y_movement = (packet[0] >> 5 & 1) ? packet[2] - 256 : packet[2];

	button_states[MOUSE_BUTTON_LEFT] 	= packet[0] & 1 ? TRUE : FALSE;
	button_states[MOUSE_BUTTON_RIGHT] 	= packet[0] >> 1 & 1 ? TRUE : FALSE;
	button_states[MOUSE_BUTTON_MIDDLE] 	= packet[0] >> 2 & 1 ? TRUE : FALSE;
	button_states[MOUSE_BUTTON_4] 		= FALSE;
	button_states[MOUSE_BUTTON_5] 		= FALSE;
}

/**
 * IRQ handler.
 *
 * NOTE: There is a possibility that the PS/2 output buffer gets
 * overflown (for obscure reasons), in that case maybe it will be a good idea
 * to iteratively read from the buffer, until status bit 0 gets dropped.
 */
static void __cdecl mouse_isr(K_REGISTERS regs)
{
	uint8_t mouse_byte;
	UNUSED_ARG(regs);

	if ((READ_PORT_UCHAR(MOUSE_STATUS_PORT) & 0x20) == 0) {
		/* The following bit was not sent from the mouse */
		HalKernelPanic("mouse_isr(): Unexpected");
	}

	mouse_byte = READ_PORT_UCHAR(MOUSE_DATA_PORT);

	/* We don't support 4-byte packets as for now. */
	switch (__ctx->read_counter) {
		case 0:
			if (!(mouse_byte & 0x08)) {
				return;
			}
			__ctx->packet[__ctx->read_counter] = mouse_byte;
			break;

		case 1:
		case 2:
			__ctx->packet[__ctx->read_counter] = mouse_byte;
			break;

		default:
			HalKernelPanic("PS/2 Mouse: mouse_isr(): unexpected read counter value.");
	}

	/* Parse packet */
	if (__ctx->read_counter == 2) {
		HRESULT 		hr;
		int16_t		dummy;

		/* Parse packet and emit event(s) */
		hr = mouse_parse_packet(__ctx, __ctx->packet, 3);
		if (FAILED(hr)) {
			HalKernelPanic("mouse_isr(): Unable to parse mouse packet.");
		}

		/* Update state */
		mouse_decode_packet(__ctx->packet, 3, &dummy, &dummy, __ctx->button_state);
	}

	/* Move counter */
	__ctx->read_counter = (__ctx->read_counter + 1) % 3;
}

/*
 * Sends 0xA8 command to enable the second PS/2 port then
 * modifies the Controller Configuration Byte to enable interrupts
 * for second PS/2 port.
 */
static HRESULT __nxapi mouse_enable_ps2_port2(BOOL enabled)
{
	uint8_t status;

	/* Enable PS/2 port 2. This command doesn't return AKWG code. */
	mouse_wait(WAIT_WRITE);
	WRITE_PORT_UCHAR(MOUSE_COMMAND_PORT, enabled ? 0xA8 : 0xA7);

	/* Read Controller Configuration Byte */
	mouse_wait(WAIT_WRITE);
	WRITE_PORT_UCHAR(MOUSE_COMMAND_PORT, 0x20);

	mouse_wait(WAIT_READ);
	status = READ_PORT_UCHAR(MOUSE_DATA_PORT);

	/* Modify status */
	if (enabled) {
		status |= 2;
		status &= ~0x20;
	} else {
		status &= ~2;
	}

	/* Set CCB to enable mouse interrupts */
	mouse_wait(WAIT_WRITE);
	WRITE_PORT_UCHAR(MOUSE_COMMAND_PORT, 0x60);

	mouse_wait(WAIT_WRITE);
	WRITE_PORT_UCHAR(MOUSE_DATA_PORT, status);

	return S_OK;
}

//static HRESULT __nxapi mouse_enable_ps2_port1(BOOL enabled)
//{
//	uint8_t status;
//
//	/* Enable PS/2 port 2. This command doesn't return AKWG code. */
//	mouse_wait(WAIT_WRITE);
//	WRITE_PORT_UCHAR(MOUSE_COMMAND_PORT, enabled ? 0xAE : 0xAD);
//
//	/* Read Controller Configuration Byte */
//	mouse_wait(WAIT_WRITE);
//	WRITE_PORT_UCHAR(MOUSE_COMMAND_PORT, 0x20);
//
//	mouse_wait(WAIT_READ);
//	status = READ_PORT_UCHAR(MOUSE_DATA_PORT);
//
//	/* Modify status */
//	if (enabled) {
//		status |= 1;
//	} else {
//		status &= ~1;
//	}
//
//	/* Set CCB to enable mouse interrupts */
//	mouse_wait(WAIT_WRITE);
//	WRITE_PORT_UCHAR(MOUSE_COMMAND_PORT, 0x60);
//
//	mouse_wait(WAIT_WRITE);
//	WRITE_PORT_UCHAR(MOUSE_DATA_PORT, status);
//
//	return S_OK;
//}

/*
 * Issues a reset command to the PS/2 mouse device
 */
static HRESULT __nxapi mouse_reset()
{
	uint8_t ps2_status;
	HRESULT hr;

	mouse_write(0xFF);
	ps2_status = mouse_read(&hr);

	if (FAILED(hr)) {
		return hr;
	}

	return ps2_status = MOUSE_SUCCESS ? S_OK : E_FAIL;
}

static HRESULT __nxapi mouse_init(K_DEVICE *self)
{
	if (__ctx != NULL) {
		/* Already one mouse is installed */
		return E_FAIL;
	}

	/* Allocate driver context */
	if (!(__ctx = kcalloc(sizeof(PS2MOUSE_DRV_CONTEXT)))) {
		return E_OUTOFMEM;
	}

	self->opaque = __ctx;

	/* Disable interrupts while configuring the PS/2 controller. */
	hal_cli();
	mouse_enable_ps2_port2(TRUE);

	HRESULT hr;

	/* Reset the mouse */
	hr = mouse_reset();
	if (FAILED(hr)) goto fail;

	/* Use default settings */
	mouse_write(0xF6);
	mouse_read(NULL);

	/* Enable mouse */
	mouse_write(0xF4);
	mouse_read(NULL);

	/* Install IRQ handler */
	register_isr_callback(irq_to_intid(MOUSE_IRQ_ID), mouse_isr, self->opaque);

	/* Reenable interrupts */
	hal_sti();

	spinlock_create(&__ctx->cb_lock);
	return S_OK;

fail:
	hal_sti();
	return E_FAIL;
}

static HRESULT __nxapi mouse_fini(K_DEVICE *self)
{
	hal_cli();

	/* Disable PS/2 port 2 and mouse */
	mouse_enable_ps2_port2(FALSE);
	hal_sti();

	/* Free resources */
	spinlock_destroy(&__ctx->cb_lock);
	kfree(self->opaque);
	__ctx = NULL;

	/* Detach IRQ handler */
	unregister_isr_callback(irq_to_intid(MOUSE_IRQ_ID));

	return S_OK;
}

static HRESULT mouse_register_cb(PS2MOUSE_DRV_CONTEXT *ctx, IOCTL_POINTING_REGISTER_STRUCT *arg)
{
	HRESULT		hr		= S_OK;
	uint32_t 	iflag 	= spinlock_acquire(&ctx->cb_lock);
	uint32_t	i;

	if (ctx->cb_count >= MOUSE_MAX_CB_COUNT) {
		hr = E_FAIL;
		goto finally;
	}

	for (i=0; i<ctx->cb_count; i++) {
		if (ctx->cb_list[i] == arg->cb && ctx->cb_user_list[i] == arg->user) {
			hr = E_INVALIDARG;
			goto finally;
		}
	}

	ctx->cb_list[ctx->cb_count] = arg->cb;
	ctx->cb_user_list[ctx->cb_count] = arg->user;
	ctx->cb_count++;

finally:
	spinlock_release(&ctx->cb_lock, iflag);
	return hr;
}

static HRESULT mouse_unregister_cb(PS2MOUSE_DRV_CONTEXT *ctx, IOCTL_POINTING_REGISTER_STRUCT *arg)
{
	HRESULT		hr		= S_OK;
	uint32_t 	iflag 	= spinlock_acquire(&ctx->cb_lock);
	int32_t		id		= -1;
	uint32_t	i;

	for (i=0; i<ctx->cb_count; i++) {
		if (ctx->cb_list[i] == arg->cb && ctx->cb_user_list[i] == arg->user) {
			id = (int32_t)i;
			break;
		}
	}

	if (id >= 0) {
		/* Found. Delete it */
		for (i=(uint32_t)id; i<ctx->cb_count-1; i++) {
			ctx->cb_list[i] = ctx->cb_list[i+1];
			ctx->cb_user_list[i] = ctx->cb_user_list[i+1];
		}

		ctx->cb_count--;
	} else {
		/* Not found */
		hr = E_NOTFOUND;
	}

	spinlock_release(&ctx->cb_lock, iflag);
	return hr;
}

/*
 * Parses mouse packet and tries to find differences with current state.
 * If such are found, it generates and emits mouse event(s).
 *
 * @return S_OK if event is generated, S_FALSE is no event is generated, error otherwise.
 */
static HRESULT mouse_parse_packet(PS2MOUSE_DRV_CONTEXT *ctx, uint8_t *packet, uint32_t size)
{
	HRESULT			hr;
	K_MOUSE_EVENT	e;
	int16_t			xm, ym;
	BOOL			new_states[MOUSE_MAX_BUTTONS];
	uint32_t		i;

	mouse_decode_packet(packet, size, &xm, &ym, new_states);

	/* Find the differences! */
	for (i=MOUSE_BUTTON_LEFT; i<=MOUSE_BUTTON_5; i++) {
		if (ctx->button_state[i] != new_states[i]) {
			/* State transition found */
			e.type 			= new_states[i] ? MOUSE_EVENT_BUTTON_DOWN : MOUSE_EVENT_BUTTON_UP;
			e.button.type	= i;

			hr = mouse_emit_event(ctx, &e);
			if (FAILED(hr)) return hr;
		}
	}

	/* Movement ? */
	if (xm != 0 || ym != 0) {
		e.type = MOUSE_EVENT_MOVEMENT;
		e.movement.x = xm;
		e.movement.y = ym;

		hr = mouse_emit_event(ctx, &e);
		if (FAILED(hr)) return hr;
	}

	return S_OK;
}

static HRESULT mouse_emit_event(PS2MOUSE_DRV_CONTEXT *ctx, K_MOUSE_EVENT *ev)
{
	uint32_t	iflag 	= spinlock_acquire(&ctx->cb_lock);
	uint32_t	i;

	for (i=0; i<ctx->cb_count; i++) {
		ctx->cb_list[i](*ev, ctx->cb_user_list[i]);
	}

	spinlock_release(&ctx->cb_lock, iflag);
	return S_OK;
}

static HRESULT mouse_ioctl(K_STREAM *s, uint32_t code, void *arg)
{
	PS2MOUSE_DRV_CONTEXT *ctx = GET_DRV_CTX(s);

	switch (code) {
		case IOCTL_DEVICE_OPEN:
		case IOCTL_DEVICE_CLOSE:
			return S_OK;

		case IOCTL_POINTING_REGISTER:
			return mouse_register_cb(ctx, arg);

		case IOCTL_POINTING_UNREGISTER:
			return mouse_unregister_cb(ctx, arg);
	}

	return E_INVALIDARG;
}

static K_DEVICE ps2m_device = {
		.default_url = "/dev/mouse",
		.type 		= DEVICE_TYPE_CHAR,
		.class		= DEVICE_CLASS_POINTING,
		.subclass 	= DEVICE_SUBCLASS_MOUSE,
		.read 		= NULL,
		.write 		= NULL,
		.ioctl 		= mouse_ioctl,
		.seek 		= NULL,
		.tell		= NULL,
		.initialize = mouse_init,
		.finalize 	= mouse_fini,
		.open 		= NULL,
		.close 		= NULL,
};

HRESULT __nxapi ps2mouse_install()
{
	return vfs_mount_device(&ps2m_device, ps2m_device.default_url);
}

HRESULT __nxapi ps2mouse_uninstall()
{
	return vfs_unmount_device(ps2m_device.default_url);
}
