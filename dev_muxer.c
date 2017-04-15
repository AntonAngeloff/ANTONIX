/*
 * dev_muxer.c
 *
 *  Created on: 13.04.2017 ã.
 *      Author: Anton Angelov
 */
#include <dev_muxer.h>
#include <syncobjs.h>
#include <timer.h>
#include <hal.h>
#include <mm.h>

typedef enum {
	VSTREAM_CALL_READ,
	VSTREAM_CALL_WRITE,
	VSTREAM_CALL_CLOSE,
	VSTREAM_CALL_TELL,
	VSTREAM_CALL_SEEK,
	VSTREAM_CALL_IOCTL,
} VSTREAM_CALL_TYPE;

typedef union {
	VSTREAM_CALL_TYPE type;

	struct {
		size_t	block_size;
		void	*out_buf;
		size_t	*bytes_read;
	} read;

	struct {
		int32_t	block_size;
		void	*in_buf;
		size_t	*bytes_written;
	} write;

	struct {
		int64_t pos;
		int8_t	origin;
	} seek;

	struct {
		uint32_t code;
		void	*arg;
	} ioctl;
} VSTREAM_CALL_PARAMS;

/*
 * Private function prototypes
 */
static K_DEV_STREAM __nxapi *vstream_create(K_DEV_MUXER *dm, uint32_t slot);
static VOID			__nxapi	vstream_destroy(K_DEV_STREAM *s);
static HRESULT		__nxapi vstream_forward_call(K_DEV_STREAM *str, VSTREAM_CALL_PARAMS *params, uint32_t *result);
static HRESULT		__nxapi vstream_deserialize_call(K_STREAM *target, VSTREAM_CALL_PARAMS *p, uint32_t *result);
static HRESULT 		__nxapi vstream_read(K_STREAM *str, const size_t block_size, void *out_buf, size_t *bytes_read);
static HRESULT 		__nxapi vstream_write(K_STREAM *str, const int32_t block_size, void *in_buf, size_t *bytes_written);
static HRESULT 		__nxapi vstream_close(K_STREAM **str);
static uint32_t 	__nxapi vstream_tell(K_STREAM *str);
static uint32_t 	__nxapi vstream_seek(K_STREAM *str, int64_t pos, int8_t origin);
static HRESULT 		__nxapi vstream_ioctl(K_STREAM *s, uint32_t code, void *arg);

static ULONG		__nxapi device_addref(K_DEV_DESC *dd);
static ULONG		__nxapi device_release(K_DEV_DESC *dd);

static K_DEV_MUXER	__nxapi *devmux_create_intrnl(K_DEVMUX_DIRECTION dir, K_STREAM *stream, BOOL autoclose);

/*
 * Implementation
 */

static K_DEV_MUXER __nxapi
*devmux_create_intrnl(K_DEVMUX_DIRECTION dir, K_STREAM *stream, BOOL autoclose)
{
	K_DEV_MUXER	*dm;
	uint32_t	i;

	if (stream == NULL || (dir != DEVMUX_DIRECTION_INPUT && dir != DEVMUX_DIRECTION_OUTPUT)) {
		return NULL;
	}

	if ((dm = kcalloc(sizeof(K_DEV_MUXER))) == NULL) {
		return NULL;
	}

	dm->direction = dir;
	mutex_create(&dm->sec_devices_lock);

	dm->prim_auto_close				= autoclose;
	dm->prim_device.initialized 	= TRUE;
	dm->prim_device.stream			= stream;

	for (i=0; i<DEV_MUXER_MAX_DEVICES; i++) {
		mutex_create(&dm->sec_devices[i].lock);
		dm->sec_devices[i].muxer = dm;
	}

	return dm;
}

K_DEV_MUXER	__nxapi
*devmux_create(K_DEVMUX_DIRECTION dir, char	*primary_device)
{
	K_DEV_MUXER	*dm;
	uint32_t	s_flags;
	K_STREAM	*s;

	if (primary_device == NULL) {
		return NULL;
	}

	/* Decide file open flags according to device multiplexer direction
	 */
	switch (dir) {
	case DEVMUX_DIRECTION_INPUT:
		s_flags = FILE_OPEN_WRITE;
		break;

	case DEVMUX_DIRECTION_OUTPUT:
		s_flags = FILE_OPEN_READ;
		break;

	case DEVMUX_DIRECTION_BIDIRECTION:
		s_flags = FILE_OPEN_READWRITE;
		break;

	default:
		return NULL;
	}

	if (FAILED(k_fopen(primary_device, s_flags, &s))) {
		return NULL;
	}

	/* Invoke internal constructor */
	dm = devmux_create_intrnl(dir, s, TRUE);

	if (dm == NULL) {
		k_fclose(&s);
	}

	return dm;
}

K_DEV_MUXER	__nxapi
*devmux_create2(K_DEVMUX_DIRECTION dir, K_STREAM *stream)
{
	return devmux_create_intrnl(dir, stream, FALSE);
}

VOID __nxapi
devmux_destroy(K_DEV_MUXER *dm)
{
	uint32_t 	i;

	/* Lock list */
	mutex_lock(&dm->sec_devices_lock);

	/* Remove all devices */
	for (i=0; i<DEV_MUXER_MAX_DEVICES; i++) {
		devmux_remove_device(dm, i);
		mutex_destroy(&dm->sec_devices[i].lock);
	}

	/* Wait for all devices slots to be released. A semaphore
	 * will be more appropriate approach than sleep. */
	while (dm->sec_device_count != 0) {
		timer_sleep(10);
	}

	/* Unlock list */
	mutex_unlock(&dm->sec_devices_lock);
	mutex_destroy(&dm->sec_devices_lock);

	/* Free device multiplexer structure */
	kfree(dm);
}

HRESULT	__nxapi
devmux_add_device(K_DEV_MUXER *dm, int32_t slot_id, uint32_t *slot_id_out, K_STREAM **stream_out)
{
	HRESULT 	hr = S_OK;
	uint32_t	i;
	K_DEV_DESC	*dd;

	if (slot_id < -1 || slot_id >= DEV_MUXER_MAX_DEVICES) {
		return E_INVALIDARG;
	}

	/* Lock secondary device list */
	mutex_lock(&dm->sec_devices_lock);

	/* If no slot is specified, find free slot */
	if (slot_id == -1) {
		for (i=0; i<DEV_MUXER_MAX_DEVICES; i++) {
			if (!dm->sec_devices[i].initialized) {
				slot_id = i;
				break;
			}
		}

		if (slot_id == -1) {
			/* Failed to find free slot */
			hr = E_FAIL;
			goto finally;
		}
	}

	/* Lock device slot */
	dd = &dm->sec_devices[slot_id];
	mutex_lock(&dd->lock);

	/* Make sure slot is free */
	if (dd->initialized) {
		/* Invalid slot */
		hr = E_INVALIDARG;
		mutex_unlock(&dd->lock);
		goto finally;
	}

	if (dd->ref_count != 0) {
		HalKernelPanic("Unexpected error.");
	}

	dd->initialized = TRUE;
	dd->stream		= (K_STREAM*)vstream_create(dm, slot_id);

	if (dd->stream == NULL) {
		/* Failed to create virtual stream */
		dd->initialized	= FALSE;
		hr = E_FAIL;
		mutex_unlock(&dd->lock);
		goto finally;
	}

	/* Create activation and destruction event */
	event_create(&dd->activation_ev, EVENT_FLAG_AUTORESET);
	event_create(&dd->destruction_ev, EVENT_FLAG_AUTORESET);

	/* Add reference to device slot to keep it alive */
	device_addref(dd);

	/* Unlock device slot */
	mutex_unlock(&dd->lock);
finally:
	/* Assign output parameters on success */
	if (SUCCEEDED(hr)) {
		if (slot_id_out) *slot_id_out = slot_id;
		if (stream_out)	 *stream_out = dd->stream;
	}

	/* Unlock device list */
	mutex_unlock(&dm->sec_devices_lock);

	return hr;
}

HRESULT	__nxapi
devmux_remove_device(K_DEV_MUXER *dm, uint32_t slot_id)
{
	HRESULT		hr = S_OK;
	K_DEV_DESC	*dd;

	if (slot_id >= DEV_MUXER_MAX_DEVICES) {
		return E_INVALIDARG;
	}

	/* Lock device list */
	mutex_lock(&dm->sec_devices_lock);

	/* Lock device slot */
	dd = &dm->sec_devices[slot_id];
	mutex_lock(&dd->lock);

	/* If slot is free, we can't remove it's device */
	if (!dd->initialized) {
		hr = E_INVALIDARG;
		goto finally;
	}

	/* Signal the destruction event */
	event_signal(&dd->destruction_ev);

	/* Release reference */
	device_release(dd);

finally:
	mutex_unlock(&dd->lock);
	mutex_unlock(&dm->sec_devices_lock);

	return hr;
}

HRESULT	__nxapi
devmux_get_device_stream(K_DEV_MUXER *dm, uint32_t slot_id, K_STREAM **stream_out)
{
	HRESULT	hr;

	if (slot_id >= DEV_MUXER_MAX_DEVICES) {
		return E_INVALIDARG;
	}

	/* Lock device list */
	mutex_lock(&dm->sec_devices_lock);

	/* Lock device */
	mutex_lock(&dm->sec_devices[slot_id].lock);

	if (!dm->sec_devices[slot_id].initialized) {
		hr = E_INVALIDARG;
		goto finally;
	}

	*stream_out = (K_STREAM*)dm->sec_devices[slot_id].stream;

finally:
	/* Unlock device */
	mutex_unlock(&dm->sec_devices[slot_id].lock);

	/* Unlock device list */
	mutex_unlock(&dm->sec_devices_lock);
	return hr;
}

HRESULT	__nxapi
devmux_switch_to(K_DEV_MUXER *dm, uint32_t slot)
{
	HRESULT		hr;
	K_DEV_DESC	*dd;

	if (slot >= DEV_MUXER_MAX_DEVICES) {
		return E_INVALIDARG;
	}

	/* Lock multiplexer */
	mutex_lock(&dm->sec_devices_lock);

	/* Lock device */
	dd = &dm->sec_devices[slot];
	mutex_lock(&dd->lock);

	/* Make sure new device slot is valid */
	if (!dm->sec_devices[slot].initialized) {
		hr = E_INVALIDARG;
		goto finally;
	}

	/* Switch active device slot */
	dm->active_slot = slot;
	event_signal(&dm->sec_devices[slot].activation_ev);

finally:
	mutex_unlock(&dd->lock);
	mutex_unlock(&dm->sec_devices_lock);
	return hr;
}

static K_DEV_STREAM __nxapi
*vstream_create(K_DEV_MUXER *dm, uint32_t slot)
{
	K_DEV_STREAM *s = kcalloc(sizeof(K_DEV_STREAM));

	if (s == NULL) {
		return NULL;
	}

	s->dm = dm;
	s->slot = slot;

	/* Assign methods */
	s->stream.read	= vstream_read;
	s->stream.write = vstream_write;
	s->stream.close	= vstream_close;
	s->stream.seek	= vstream_seek;
	s->stream.tell	= vstream_tell;
	s->stream.ioctl	= vstream_ioctl;

	return s;
}

static VOID	__nxapi
vstream_destroy(K_DEV_STREAM *s)
{
	kfree(s);
}

static HRESULT __nxapi
vstream_read(K_STREAM *str, const size_t block_size, void *out_buf, size_t *bytes_read)
{
	VSTREAM_CALL_PARAMS params;

	params.type = VSTREAM_CALL_READ;
	params.read.block_size	= block_size;
	params.read.out_buf		= out_buf;
	params.read.bytes_read	= bytes_read;

	return vstream_forward_call((K_DEV_STREAM*)str, &params, NULL);
}

static HRESULT __nxapi
vstream_write(K_STREAM *str, const int32_t block_size, void *in_buf, size_t *bytes_written)
{
	VSTREAM_CALL_PARAMS params;

	params.type = VSTREAM_CALL_WRITE;
	params.write.block_size		= block_size;
	params.write.in_buf			= in_buf;
	params.write.bytes_written 	= bytes_written;

	return vstream_forward_call((K_DEV_STREAM*)str, &params, NULL);
}

static HRESULT __nxapi
vstream_close(K_STREAM **str)
{
	VSTREAM_CALL_PARAMS params;

	params.type = VSTREAM_CALL_CLOSE;
	return vstream_forward_call((K_DEV_STREAM*)str, &params, NULL);
}

static uint32_t __nxapi
vstream_tell(K_STREAM *str)
{
	VSTREAM_CALL_PARAMS params;
	uint32_t			result = 0;

	params.type = VSTREAM_CALL_TELL;
	vstream_forward_call((K_DEV_STREAM*)str, &params, &result);

	return result;
}

static uint32_t __nxapi
vstream_seek(K_STREAM *str, int64_t pos, int8_t origin)
{
	VSTREAM_CALL_PARAMS params;
	uint32_t			result = 0;

	params.type = VSTREAM_CALL_SEEK;
	params.seek.pos 	= pos;
	params.seek.origin	= origin;

	vstream_forward_call((K_DEV_STREAM*)str, &params, &result);

	return result;
}

static HRESULT __nxapi
vstream_ioctl(K_STREAM *s, uint32_t code, void *arg)
{
	VSTREAM_CALL_PARAMS params;

	params.type = VSTREAM_CALL_IOCTL;
	params.ioctl.code	= code;
	params.ioctl.arg	= arg;

	return vstream_forward_call((K_DEV_STREAM*)s, &params, NULL);
}

static HRESULT __nxapi
vstream_forward_call(K_DEV_STREAM *str, VSTREAM_CALL_PARAMS *params, uint32_t *result)
{
	K_DEV_MUXER		*dm = str->dm;
	K_DEV_DESC		*dd;
	K_EVENT			elist[2];
	uint32_t		waitres;
	HRESULT			hr;
	BOOL			is_active_slot;

	mutex_lock(&dm->sec_devices_lock);

	is_active_slot	= str->dm->active_slot == str->slot;
	dd 				= &dm->sec_devices[str->slot];

	/* Add reference to the device, which ensures it will not be
	 * removed until we release it later. This is similar to locking.
	 */
	device_addref(dd);

	/* Unlock device list, so other devices can be added/removed while we wait */
	mutex_unlock(&dm->sec_devices_lock);

	/* If we are not on an active slot, block until we are activated or terminated */
	if (!is_active_slot) {
		elist[0] = dd->destruction_ev;
		elist[1] = dd->activation_ev;

		/* Wait for device to either get activated or terminated */
		hr = event_waitfor_multiple(elist, 2, TIMEOUT_INFINITE, &waitres);
		if (FAILED(hr)) goto finally;

		if (waitres == 0) {
			/* Destruction event signaled */
			return E_INTERRUPTED;
		} else if (waitres == 1) {
			/* Activation event signaled - forward the call to primary device */
		} else {
			/* Unexpected */
			HalKernelPanic("Unexpected case.");
		}
	}

	/* Forward the call */
	hr = vstream_deserialize_call(dm->prim_device.stream, params, result);

finally:
	device_release(dd);
	return hr;
}

static HRESULT __nxapi
vstream_deserialize_call(K_STREAM *target, VSTREAM_CALL_PARAMS *p, uint32_t *result)
{
	switch (p->type) {
		case VSTREAM_CALL_READ:
			return k_fread(target, p->read.block_size, p->read.out_buf, p->read.bytes_read);

		case VSTREAM_CALL_WRITE:
			return k_fwrite(target, p->write.block_size, p->write.in_buf, p->write.bytes_written);

		case VSTREAM_CALL_CLOSE:
			return E_UNEXPECTED;

		case VSTREAM_CALL_TELL:
			*result = k_ftell(target);
			return S_OK;

		case VSTREAM_CALL_SEEK:
			*result = k_fseek(target, p->seek.pos, p->seek.origin);
			return S_OK;

		case VSTREAM_CALL_IOCTL:
			return k_ioctl(target, p->ioctl.code, p->ioctl.arg);

		default:
			return E_INVALIDARG;
	}
}

static ULONG __nxapi
device_addref(K_DEV_DESC *dd)
{
	uint32_t	cnt;
	K_DEV_MUXER	*m;

	mutex_lock(&dd->lock);
	cnt = dd->ref_count++;

	m = dd->muxer;
	atomic_increment(&m->sec_device_count);

	mutex_unlock(&dd->lock);

	return cnt;
}

static ULONG __nxapi
device_release(K_DEV_DESC *dd)
{
	uint32_t	cnt;
	K_DEV_MUXER	*m;

	mutex_lock(&dd->lock);

	cnt = dd->ref_count--;
	if (cnt == 0) {
		/* Destroy */
		vstream_destroy((K_DEV_STREAM*)dd->stream);
		dd->initialized = FALSE;
	}

	m = dd->muxer;
	atomic_decrement(&m->sec_device_count);

	mutex_unlock(&dd->lock);
	return cnt;
}

HRESULT	__nxapi
devmux_test(char *target_device)
{
	K_DEV_MUXER *dm;
	HRESULT		hr;
	uint32_t	i, j, bytes;
	uint8_t		buff[128];

#define TEST_STREAM_CNT	10
	K_STREAM	*streams[TEST_STREAM_CNT], *s;
	uint32_t	slots[TEST_STREAM_CNT];

	/* Create new muxer */
	dm = devmux_create(DEVMUX_DIRECTION_BIDIRECTION, target_device);
	if (dm == NULL) return E_FAIL;

	/* Create secondary device streams */
	for (i=0; i<TEST_STREAM_CNT; i++) {
		hr = devmux_add_device(dm, -1, &slots[i], &streams[i]);
		if (FAILED(hr)) {
			k_printf("devmux_add_device() failed, hr=%x\n", hr);
			goto finally;
		}
	}

	/* Switch to each stream and try to access any other */
	for (i=0; i<TEST_STREAM_CNT; i++) {
		hr = devmux_switch_to(dm, i);
		if (FAILED(hr)) {
			k_printf("devmux_switch_to() failed, hr=%x\n", hr);
			goto finally;
		}

		for (j=0; j<TEST_STREAM_CNT; j++) {
			/* Test devmux_get_device_stream() */
			hr = devmux_get_device_stream(dm, j, &s);
			if (FAILED(hr) || s != streams[j]) {
				k_printf("devmux_get_device_stream() failed, hr=%x\n", hr);
				goto finally;
			}

			/* Skip active stream */
			if (i==j) {
				continue;
			}

			/* Try each operation */
			hr = k_fread(s, 128, buff, &bytes);
			///....
		}

		/* Test active stream */

	}

finally:
	devmux_destroy(dm);
	return hr;
}
