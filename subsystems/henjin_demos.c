/*
 * henjin_demos.c
 *
 *	Demo applications
 *
 *  Created on: 9.10.2016 ã.
 *      Author: Anton Angelov
 */
#include <scheduler.h>
#include <hal.h>
#include "henjin_demos.h"
#include "henjin_window.h"
#include "henjin_button.h"

/*
 * Prototypes
 */
static HRESULT demo1_message_handler(HJ_OBJECT *this, HJ_MESSAGE *msg);
static HRESULT demo1_quit(HJ_WINDOW *wnd);
static VOID demo1_thread_proc();
static VOID demo2_thread_proc();

/*
 * Global variables
 */
static HJ_WINDOW *wnd_demo1, *wnd_demo2;

/*
 * Implementation
 */
static HRESULT demo1_message_handler(HJ_OBJECT *this, HJ_MESSAGE *msg)
{
	HJ_WINDOW 	*wnd = HJ_CAST(this, HJ_CLASS_WINDOW, HJ_WINDOW);
	HRESULT		hr;

	switch (msg->type) {
		case HJ_MESSAGE_REPAINT:
			return S_OK;

		case HJ_MESSAGE_RESIZE:
			/* Issue a full redraw */
			hr = hj_control_invalidate_rect(&wnd->control, RECT(0, 0, msg->resize.x, msg->resize.y));
			if (FAILED(hr)) return hr;

			return S_OK;

		case HJ_MESSAGE_QUIT:
			return demo1_quit(wnd);

		default:
			return S_FALSE;
	}
}

HRESULT demo1_init()
{
	HJ_WINDOW	*wnd;
	HJ_WINDOW	*wnd2;
	HJ_BUTTON	*btn1;
	HJ_BUTTON	*btn2;
	HRESULT		hr;

	/*
	 * Create new window
	 */
	wnd = hj_create_window(demo1_message_handler);
	if (!wnd) {
		return E_FAIL;
	}

	hr = hj_control_resize(&wnd->control, SIZE(300, 200));
	if (FAILED(hr)) return hr;

	wnd_demo1 = wnd;

	/*
	 * Register to server
	 */
	hr = henjin_register_window(wnd, POINT(100, 100));
	if (FAILED(hr)) return hr;

	/*
	 * Add button
	 */
	if (!(btn1 = hj_create_button())) {
		return E_FAIL;
	}

	hr = hj_add_child(&wnd->control, &btn1->control);
	if (FAILED(hr)) return hr;

	hr = hj_btn_set_caption(btn1, "Okay");
	if (FAILED(hr)) return hr;

	hj_control_set_child_position(&wnd->control, hj_get_children_count(&wnd->control)-1, POINT(220, 160));
	hj_control_resize(&btn1->control, SIZE(70, 30));

	/*
	 * Add button
	 */
	if (!(btn2 = hj_create_button())) {
		return E_FAIL;
	}

	hr = hj_add_child(&wnd->control, &btn2->control);
	if (FAILED(hr)) return hr;

	hr = hj_btn_set_caption(btn2, "Cancel");
	if (FAILED(hr)) return hr;

	hj_control_set_child_position(&wnd->control, hj_get_children_count(&wnd->control)-1, POINT(210-120, 160));
	hj_control_resize(&btn2->control, SIZE(120, 30));

//
//	/*
//	 * Create new window
//	 */
//	wnd2 = hj_create_window(demo1_message_handler);
//	if (!wnd2) {
//		return E_FAIL;
//	}
//
//	hr = hj_control_resize(&wnd2->control, SIZE(200, 150));
//	if (FAILED(hr)) return hr;
//
//	wnd_demo2 = wnd2;
//
//	/*
//	 * Register to server
//	 */
//	hr = henjin_register_window(wnd2, POINT(300, 100));
//	if (FAILED(hr)) return hr;

	/*
	 * Start wnd thread
	 */
	hr = sched_create_thread(NULL, demo1_thread_proc, NULL);
	if (FAILED(hr)) return hr;
//
//	hr = sched_create_thread(NULL, demo2_thread_proc, NULL);
//	if (FAILED(hr)) return hr;

	return S_OK;
}

static HRESULT demo1_quit(HJ_WINDOW *wnd)
{
	HRESULT hr;

	hr = henjin_unregister_window(wnd);
	if (FAILED(hr)) return hr;

	hj_destroy_window(wnd);

	return S_OK;
}

static VOID demo1_thread_proc()
{
	HJ_MESSAGE	msg;
	HRESULT 	hr;

	/* Loops and processes incoming messages */
	while (TRUE) {
		hr = hj_get_message(&wnd_demo1->control, &msg);

		if (hr == E_BUFFERUNDERFLOW) {
			/* Queue is empty */
			sched_yield();
			continue;
		}else if (FAILED(hr)) {
			HalKernelPanic("demo1_thread_proc(): failed to retrieve message.");
		}

		hr = hj_process_message(&wnd_demo1->control, &msg);
		if (FAILED(hr)) {
			HalKernelPanic("demo1_thread_proc(): failed to process message.");
		}
	}
}

static VOID demo2_thread_proc()
{
	HJ_MESSAGE	msg;
	HRESULT 	hr;

	/* Loops and processes incoming messages */
	while (TRUE) {
		hr = hj_get_message(&wnd_demo2->control, &msg);

		if (hr == E_BUFFERUNDERFLOW) {
			/* Queue is empty */
			sched_yield();
			continue;
		}else if (FAILED(hr)) {
			HalKernelPanic("demo1_thread_proc(): failed to retrieve message.");
		}

		hr = hj_process_message(&wnd_demo2->control, &msg);
		if (FAILED(hr)) {
			HalKernelPanic("demo1_thread_proc(): failed to process message.");
		}
	}

}
