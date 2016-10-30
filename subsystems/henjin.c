/*
 * henjin.c
 *
 *  Created on: 7.10.2016 ã.
 *      Author: Anton Angelov
 */
#include <scheduler.h>
#include <string.h>
#include <mm.h>
#include <kstdio.h>
#include "henjin.h"
#include "henjin_desktop.h"
#include "henjin_demos.h"

HJ_SERVER_CONTEXT __hj_server = { 0 };

/*
 * Prototypes
 */
static HRESULT henjin_set_resolution(uint32_t width, uint32_t height, NXGI_FORMAT format);
static HRESULT henjin_process_mouse_events(HJ_SERVER_CONTEXT *hj);
/*
 * Implementation
 */
HRESULT henjin_set_resolution(uint32_t width, uint32_t height, NXGI_FORMAT format)
{
	HJ_SERVER_CONTEXT	*server = &__hj_server;
	HJ_MESSAGE			msg;
	HRESULT				hr;
	K_EVENT				*event = kmalloc(sizeof(K_EVENT));

	if (!event) {
		return E_OUTOFMEM;
	}

	/* Create event */
	event_create(event, EVENT_FLAG_AUTORESET);

	/* Send DISABLE_DRAWING message. This message is intended only for the desktop.
	 * Other controls should not receive it.
	 */
	memset(&msg, 0, sizeof(msg));
	msg.type = HJ_MESSAGE_DISABLE_DRAWING;
	msg.lock.event = event;

	hr = hj_queue_message(HJ_CAST(server->desktop, HJ_CLASS_CONTROL, HJ_CONTROL), &msg);
	if (FAILED(hr)) goto finally;

	/* Wait until desktop get's disabled */
	hr = event_waitfor(event, 5000);
	if (FAILED(hr)) goto finally;

	/* Reset the NXGI library */
	nxgi_fini();

	/* Initialize NXGI and set new video mode */
	hr = nxgi_init(width, height, format);
	if (FAILED(hr)) goto finally;

	/* Send ENABLE_DRAWING message. We don't need to wait this time.
	 */
	memset(&msg, 0, sizeof(msg));
	msg.type = HJ_MESSAGE_ENABLE_DRAWING;
	//msg.lock.event = event;

	hr = hj_queue_message(HJ_CAST(server->desktop, HJ_CLASS_CONTROL, HJ_CONTROL), &msg);
	if (FAILED(hr)) goto finally;

	event_reset(event);

	/* Notify the desktop that resolution has changes.
	 */
	memset(&msg, 0, sizeof(msg));
	msg.type = HJ_MESSAGE_RESIZE;
	msg.resize.x = width;
	msg.resize.y = height;

	hr = hj_queue_message(HJ_CAST(server->desktop, HJ_CLASS_CONTROL, HJ_CONTROL), &msg);
	if (FAILED(hr)) goto finally;

	/* Request full desktop repaint
	 */
	memset(&msg, 0, sizeof(msg));
	msg.type = HJ_MESSAGE_REPAINT;
	msg.repaint.rect = RECT(0, 0, width, height);

	hr = hj_queue_message(HJ_CAST(server->desktop, HJ_CLASS_CONTROL, HJ_CONTROL), &msg);
	if (FAILED(hr)) goto finally;

	server->screen_rect = RECT(0, 0, width, height);
	server->mouse_pos = nxgig_clip_point(server->mouse_pos, server->screen_rect);

finally:
	event_destroy(event);
	kfree(event);

	return hr;
}

static HRESULT henjin_mouse_driver_callback(K_MOUSE_EVENT ev, void *user)
{
	/* Write raw events to queue which will be later transformed
	 * to HJ_MESSAGEs.
	 */
	RING_BUFFER *rb = user;

	rb_write(rb, &ev, sizeof(ev));
	return S_OK;
}

static HRESULT henjin_process_mouse_events(HJ_SERVER_CONTEXT *hj)
{
	HJ_CONTROL		*desktop = HJ_CAST(hj->desktop, HJ_CLASS_CONTROL, HJ_CONTROL);
	K_MOUSE_EVENT 	ev;
	HJ_MESSAGE		msg;
	HRESULT 		hr;

	while (rb_get_read_size(hj->mouse_driver_events) >= sizeof(K_MOUSE_EVENT)) {
		hr = rb_read(hj->mouse_driver_events, &ev, sizeof(ev));
		if (FAILED(hr)) return hr;

		/* Convert mouse driver event to Henjin message */
		switch (ev.type) {
			case MOUSE_EVENT_MOVEMENT:
				hj->mouse_pos.x += ev.movement.x;
				hj->mouse_pos.y -= ev.movement.y; //Invert Y
				hj->mouse_pos = nxgig_clip_point(hj->mouse_pos, hj->screen_rect);

				memset(&msg, 0, sizeof(msg));
				msg.type = HJ_MESSAGE_MOUSE_MOVED;
				msg.mouse.x = hj->mouse_pos.x;
				msg.mouse.y = hj->mouse_pos.y;

				hj_queue_message(desktop, &msg);

				break;

			case MOUSE_EVENT_BUTTON_UP:
				memset(&msg, 0, sizeof(msg));
				msg.type = HJ_MESSAGE_MOUSE_UP;
				msg.mouse.x = hj->mouse_pos.x;
				msg.mouse.y = hj->mouse_pos.y;
				msg.mouse.button = ev.button.type;

				hj_queue_message(desktop, &msg);
				break;

			case MOUSE_EVENT_BUTTON_DOWN:
				memset(&msg, 0, sizeof(msg));
				msg.type = HJ_MESSAGE_MOUSE_DOWN;
				msg.mouse.x = hj->mouse_pos.x;
				msg.mouse.y = hj->mouse_pos.y;

				hj_queue_message(desktop, &msg);
				break;

			case MOUSE_EVENT_SCROLL:
				break;

			default:
				break;
		}
	}

	return S_OK;
}

HRESULT henjin_start_server()
{
	HJ_SERVER_CONTEXT 	*server = &__hj_server;
	HRESULT				hr;

	/* Create desktop instance */
	server->desktop = hj_create_desktop();

	/* Create desktop thread */
	hr = sched_create_thread(NULL, hj_desktop_thread_proc, NULL);
	if (FAILED(hr)) {
		henjin_shutdown_server();
		return hr;
	}

	k_printf("Setting resolution...\n");
	/* Set resolution */
	hr = henjin_set_resolution(640, 480, NXGI_FORMAT_BGRA32);
//	hr = henjin_set_resolution(800, 600, NXGI_FORMAT_BGRA32);
	if (FAILED(hr)) goto fail;

	/* Add demo windows */
	hr = demo1_init();
	if (FAILED(hr)) goto fail;

	/* Allocate resources */
	server->mouse_driver_events = create_ring_buffer(128 * sizeof(K_MOUSE_EVENT), RING_BUFFER_LOCK_SPINLOCK);
	server->running = TRUE;

	/* Subscribe for mouse data */
	hr = k_fopen("/dev/mouse", FILE_OPEN_READ, &server->mouse_drv);
	if (SUCCEEDED(hr)) {
		IOCTL_POINTING_REGISTER_STRUCT params;

		params.user = server->mouse_driver_events;
		params.cb = henjin_mouse_driver_callback;

		hr = k_ioctl(server->mouse_drv, IOCTL_POINTING_REGISTER, &params);
		if (FAILED(hr)) goto fail;
	}

	/* Enter server loop */
	while (TRUE) {
		/* Process mouse driver events */
		hr = henjin_process_mouse_events(server);
		if (FAILED(hr)) goto fail;

		sched_yield();
	}

fail:
	k_printf("WCS SERVER FAILED!");
	hj_destroy_desktop((HJ_DESKTOP**)&__hj_server.desktop);
	return E_FAIL;
}

HRESULT henjin_shutdown_server()
{
	HJ_SERVER_CONTEXT 	*server = &__hj_server;

	/* Close mouse driver */
	if (server->mouse_drv != NULL) {
		IOCTL_POINTING_REGISTER_STRUCT params;

		params.user = server->mouse_driver_events;
		params.cb = henjin_mouse_driver_callback;

		k_ioctl(server->mouse_drv, IOCTL_POINTING_UNREGISTER, &params);
		k_fclose(&server->mouse_drv);
	}

	//TODO
	return E_NOTIMPL;
}

HRESULT henjin_register_window(VOID *wnd, HJ_POINT position)
{
	HRESULT hr;
	HJ_CONTROL *desktop = HJ_CAST(__hj_server.desktop, HJ_CLASS_CONTROL, HJ_CONTROL);

	hr = hj_add_child(desktop, HJ_CAST(wnd, HJ_CLASS_CONTROL, HJ_CONTROL));
	if (FAILED(hr)) return hr;

	hr = hj_control_set_child_decoration(desktop, hj_get_children_count(desktop)-1, HJ_DECORATION_NORMAL);
	if (FAILED(hr)) return hr;

	hr = hj_control_set_child_position(desktop, hj_get_children_count(desktop)-1, position);
	if (FAILED(hr)) return hr;

	return S_OK;
}

HRESULT henjin_unregister_window(VOID *wnd)
{
	HRESULT hr;

	//TODO:
	return E_NOTIMPL;

//	hr = hj_remove_child(HJ_CAST(__hj_server.desktop, HJ_CLASS_CONTROL, HJ_CONTROL), HJ_CAST(wnd, HJ_CLASS_CONTROL, HJ_CONTROL));
//	if (FAILED(hr)) return hr;

	return S_OK;
}
