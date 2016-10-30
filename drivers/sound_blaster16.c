/*
 * sound_blaster16.c
 *
*	Sound Blaster 16 driver for ANOTNIX.
 *
 *	We will use the common technique of auto-initialized DMA transfer
 *	with double buffering.
 *
 *	@References:
 *		- https://pdos.csail.mit.edu/6.828/2007/readings/hardware/SoundBlaster.pdf
 *		- http://qzx.com/pc-gpe/
 *
  *  Created on: 4.09.2016 ã.
 *      Author: Anton Angelov
 */
#include <stddef.h>
#include <scheduler.h>
#include <mm_virt.h>
#include <mm.h>
#include <isa_dma.h>
#include <desctables.h>
#include <vfs.h>
#include <hal.h>
#include <vga.h>
#include <string.h>
#include "sound_blaster16.h"

/* Sound Blaster 16  ports */
#define SB16_PORT_MIXER			0x04
#define SB16_PORT_MIXER_DATA	0x05
#define	SB16_PORT_DSP_READ		0x0A
#define SB16_PORT_DSP_WRITE		0x0C
#define SB16_PORT_DSP_READ_STAT	0x0C
#define SB16_PORT_INT_ACKNWG	0x0F

/* Commands */
#define SB16_CMD_SET_RATE		0x41
#define SB16_CMD_PLAY_PCM		0xA6
#define SB16_CMD_STOP_PCM		0xD9
#define SB16_CMD_GET_VERSION	0xE1

/* Channel modes */
#define SB16_CHMODE_MONO		0x00
#define SB16_CHMODE_STEREO		0x00

#define SB16_DMA_MODE_NONE		0x00
#define SB16_DMA_MODE_8BIT		0x01
#define SB16_DMA_MODE_16BIT		0x02

/* Player states */
#define PLAYER_STATE_NOT_READY	0x00
#define PLAYER_STATE_STOPPED	0x01
#define PLAYER_STATE_PLAYING	0x02
#define PLAYER_STATE_PAUSED		0x03

/* Base registers */
#define BASE0   0x200
#define BASE1   0x220
#define BASE2   0x240
#define BASE3   0x260
#define BASE4   0x280
#define BASE5   0x210

/* DSP commands */
#define CMD_GET_VERSION			0xE1
#define CMD_SET_SAMPLE_RATE		0x41
#define CMD_GET_SAMPLE_RATE		0x42
#define CMD_SPEAKER_ON			0xD1
#define CMD_SPEAKER_OFF			0xD3
#define CMD_START_8BIT_IN		0xCE
#define CMD_START_8BIT_OUT		0xC6
#define CMD_START_16BIT_IN		0xBE
#define CMD_START_16BIT_OUT		0xB6
#define CMD_STOP_8BIT			0xDA
#define CMD_STOP_16BIT			0xD9

/* Size of DMA memory buffer */
#define DMA_BUFFER_SIZE			(unsigned)0x10000

typedef struct SB16_DRV_CONTEXT SB16_DRV_CONTEXT;
struct SB16_DRV_CONTEXT {
	/**
	 * We use this buffer (which is mapped to physical memory
	 * below 16 mb) to write audio samples directly to audio
	 * card. It's size is 64kb.
	 */
	uint8_t *dma_memory;
	uintptr_t dma_memory_phys;
	uint32_t dma_mode;

	/**
	 * If set to 0 it means that the first half of the DMA memory
	 * has to be filled next. If set to 1 - the second half.
	 */
	uint32_t dma_current_half;

	/**
	 * We use this buffer to queue audio samples.
	 * When sufficient space is freed in `dma_memoery`
	 * samples are transfered to there.
	 */
	uint8_t *audio_buffer;
	uint32_t audio_buffer_capacity;

	/**
	 * Read pointer for the ring buffer (`audio_buffer`)
	 */
	uint32_t audio_rp;

	/**
	 * Write pointer for ring buffer.
	 */
	uint32_t audio_wp;

	K_SPINLOCK audio_buffer_lock;

	/**
	 * Major and minor version of device, retrieved by GetVersion
	 * command
	 */
	uint32_t version_major;
	uint32_t version_minor;

	/**
	 * ID of IRQ line used for feedback.
	 */
	uint32_t irq_id;

	/**
	 * DMA channel used for transfer to/from the SB16 device.
	 */
	uint32_t dma_channel;

	/**
	 * Player state - initializing, playing, stopped.
	 * Pause is not implemented.
	 */
	uint32_t player_state;

	/**
	 * This flag is lifted when a stop request is made. When the ISR
	 * routine detects it, it will issue command to exit DMA transfer mode.
	 */
	uint32_t stop_pending_flag;
	uint32_t skip_cntr;

	SB16_AUDIO_FORMAT fmt;
};

static struct {
	/** Used to reset the DSP to its default state. */
	uint32_t dsp_reset;

	/** Used to access in-bound DSP data. */
	uint32_t dsp_read;

	/**
	 * Used to send commands or data to the DSP. When reading
	 * from this register, it indicates weather DSP is ready to
	 * accept command or data.
	 */
	uint32_t dsp_write;

	/**
	 * Indicates whether there is any in-bound data
	 * available for reading.
	 */
	uint32_t dsp_read_status;

	/** Read this port to acknowledge 16-bit interrupt */
	uint32_t int_acknwg16;

	/** Mixer read port (read/write) */
	uint32_t mixer_data;

	/** Mixer address port (write only) */
	uint32_t mixer_addr;
} sb16_ports;

/**
 * We need a pointer to the driver context here, so we
 * can access it from ISR.
 */
static SB16_DRV_CONTEXT *drv_ctx;


/* Prototypes */
static uint8_t sb_read_dsp(HRESULT *hr);
static HRESULT sb_write_dsp(uint8_t data);
static void sb_enable_dac_speaker(uint8_t enable);

static HRESULT sb_get_version(uint32_t *pmaj, uint32_t *pmin);
static HRESULT sb_set_volume(uint8_t master);
static uint8_t sb_read_mixer(uint8_t reg);
static HRESULT sb_write_mixer(uint8_t reg, uint8_t data);
static HRESULT sb_get_mixer_irq_dma(uint32_t *irq, uint32_t *dma_channel);
static HRESULT sb_set_mixer_irq_dma(uint32_t irq, uint32_t dma_channel);

static HRESULT rb_write(SB16_DRV_CONTEXT *ctx, uint8_t *src, size_t len);
static HRESULT rb_read(SB16_DRV_CONTEXT *ctx, uint8_t *dst, size_t len);
static HRESULT rb_read_upto(SB16_DRV_CONTEXT *ctx, uint8_t *dst, size_t max, size_t *actual_bytes);
static uint32_t rb_get_read_size(SB16_DRV_CONTEXT *ctx);
static uint32_t rb_get_write_size(SB16_DRV_CONTEXT *ctx);

static HRESULT sb16_device_init(K_DEVICE *self);
static HRESULT sb16_device_fini(K_DEVICE *self);
static HRESULT sb16_reset_dsp(uint32_t base);
static HRESULT sb16_begin_playback(K_STREAM *s);
static HRESULT sb16_stop_playback(K_STREAM *s);
static HRESULT sb16_write(K_STREAM *str, const int32_t block_size, void *in_buf, size_t *bytes_written);
static HRESULT sb16_ioctl(K_STREAM *s, uint32_t code, void *arg);
static HRESULT sb16_get_format(K_STREAM *s, SB16_AUDIO_FORMAT *fmt);
static HRESULT sb16_set_format(K_STREAM *s, uint32_t channel_cnt, uint32_t sample_size_bits, uint32_t sample_rate);
static HRESULT sb16_get_bufferred_size(K_STREAM *s, uint32_t *size);
static HRESULT sb16_get_buffer_free_size(K_STREAM *s, uint32_t *size);
static void sb16_update_player_state(SB16_DRV_CONTEXT *ctx, uint32_t new_state);

static VOID __cdecl sb16_isr(K_REGISTERS regs);

/**
 * Helper function for retrieving driver context from stream object
 */
static inline SB16_DRV_CONTEXT *get_drv_ctx(K_STREAM *s)
{
	K_VFS_NODE *n = s->priv_data;
	return ((K_DEVICE*)n->content)->opaque;
}

/**
 * Writes a byte to the DSP port
 */
static HRESULT sb_write_dsp(uint8_t data)
{
	uint32_t timeout = 50000;

	/* Wait until Write Status bit 7 is cleared */
	while ((READ_PORT_UCHAR(sb16_ports.dsp_write) & 0x80) != 0) {
		//sched_update_sw();
		if (--timeout == 0) {
			HalKernelPanic("sb_write_dsp(): timed out.");
			return E_TIMEDOUT;
		}
	}

	WRITE_PORT_UCHAR(sb16_ports.dsp_write, data);
	return S_OK;
}

/**
 * Reads from DSP port
 */
static uint8_t sb_read_dsp(HRESULT *hr)
{
	uint32_t timeout = 50000;

	/* Wait until Read Status bit 7 is cleared */
	while ((READ_PORT_UCHAR(sb16_ports.dsp_read_status) & 0x80) == 0) {
		if (--timeout == 0) {
			HalKernelPanic("sb_read_dsp(): timed out.");

			if (hr) *hr = E_TIMEDOUT;
			return 0;
		}
	}

	if (hr) *hr = S_OK;
	return READ_PORT_UCHAR(sb16_ports.dsp_read);
}

/**
 * Writes a byte to an arbitrary register from the mixer chip's
 * register map.
 */
static HRESULT sb_write_mixer(uint8_t reg, uint8_t data)
{
	WRITE_PORT_UCHAR(sb16_ports.mixer_addr, reg);
	WRITE_PORT_UCHAR(sb16_ports.mixer_data, data);

	return S_OK;
}

/**
 * Sets volume on the mixer chip and disables MIDI, MIC and Line.
 */
static HRESULT sb_set_volume(uint8_t master)
{
	uint8_t v;
	v = master << 3;

	/* Voice volume */
	sb_write_mixer(0x32, v);
	sb_write_mixer(0x33, v);

	/* MIDI volume */
	sb_write_mixer(0x34, 0);
	sb_write_mixer(0x35, 0);

	/* CD volume */
	sb_write_mixer(0x36, 0);
	sb_write_mixer(0x37, 0);

	/* Line volume */
	sb_write_mixer(0x38, 0);
	sb_write_mixer(0x39, 0);

	/* Mic */
	sb_write_mixer(0x3A, 0);

	/* Master volume */
	sb_write_mixer(0x30, v);
	sb_write_mixer(0x31, v);

	return S_OK;
}

/**
 * Reads a byte from mixer mapped register.
 */
static uint8_t sb_read_mixer(uint8_t reg)
{
	WRITE_PORT_UCHAR(sb16_ports.mixer_addr, reg);
	return READ_PORT_UCHAR(sb16_ports.mixer_data);
}

/**
 * Selects IRQ id and DMA channel by writing to the mixer chip.
 */
static HRESULT sb_set_mixer_irq_dma(uint32_t irq, uint32_t dma_channel)
{
	uint32_t int_status;

	/* Select IRQ line */
	switch(irq) {
		case 2:	int_status = 1;	break;
		case 5:	int_status = 2;	break;
		case 7:	int_status = 4;	break;
		case 10:int_status = 8;	break;
		default:
			/* Invalid IRQ */
			return E_FAIL;
	}

	/* Select IRQ id and DMA channel id */
	sb_write_mixer(0x80, int_status);
	sb_write_mixer(0x81, 1 << dma_channel);

	return S_OK;
}

/**
 * Generic ring buffer write routine. Writes `len` bytes from `src`
 * to the ring buffer. If RB can not accommodate it, the routine returns
 * E_BUFFEROVERFLOW error code.
 */
static HRESULT rb_write(SB16_DRV_CONTEXT *ctx, uint8_t *src, size_t len)
{
	/* Do we have enough space? Find free segment (section) size. */
	uint32_t free_seg_size = ctx->audio_wp >= ctx->audio_rp ?
					ctx->audio_buffer_capacity - (ctx->audio_wp - ctx->audio_rp) :
					ctx->audio_rp - ctx->audio_wp;

	/* We should always keep 1 byte difference between two
	 * positions.
	 */
	free_seg_size--;

	/* Make sure we have enough space to accommodate source buffer */
	if (free_seg_size < len) {
		return E_BUFFEROVERFLOW;
	}

	/* Decide if we have to wrap the ring */
	uint8_t wrap = len > (ctx->audio_buffer_capacity - ctx->audio_wp) ? TRUE : FALSE;

	if (wrap) {
		/* Copy in two iterations */
		uint32_t 	s1 = ctx->audio_buffer_capacity - ctx->audio_wp;
		uint32_t 	s2 = len - s1;
		uint8_t		*in = src;

		memcpy(ctx->audio_buffer + ctx->audio_wp, in, s1);
		memcpy(ctx->audio_buffer, in + s1, s2);

		/* Update write index */
		ctx->audio_wp = s2;
	} else {
		/* Copy at once */
		uint32_t 	s1 = len;

		memcpy(ctx->audio_buffer + ctx->audio_wp, src, s1);

		/* Update write index */
		ctx->audio_wp += s1;
	}

	return S_OK;
}

/**
 * Generic ring buffer read routine.
 */
static HRESULT rb_read(SB16_DRV_CONTEXT *ctx, uint8_t *dst, size_t len)
{
	/* Do we have enough data to read? */
	uint32_t avail_bytes = ctx->audio_rp > ctx->audio_wp ?
					ctx->audio_buffer_capacity - (ctx->audio_rp - ctx->audio_wp) :
					ctx->audio_wp - ctx->audio_rp;

	if (avail_bytes < len) {
		/* Not enough bytes available. */
		return E_BUFFERUNDERFLOW;
	}

	uint8_t wrap = ctx->audio_rp + len > ctx->audio_buffer_capacity ? TRUE : FALSE;

	if (wrap) {
		uint32_t	s1 = ctx->audio_buffer_capacity - ctx->audio_rp;
		uint32_t	s2 = len - s1;
		uint8_t		*out = dst;

		memcpy(out, ctx->audio_buffer + ctx->audio_rp, s1);
		memcpy(out + s1, ctx->audio_buffer, s2);

		/* Update read index */
		ctx->audio_rp = s2;
	} else {
		memcpy(dst, ctx->audio_buffer + ctx->audio_rp, len);
		ctx->audio_rp += len;
	}

	return S_OK;
}

/**
 * Returns number of available bytes for reading.
 */
static uint32_t rb_get_read_size(SB16_DRV_CONTEXT *ctx)
{
	uint32_t avail_bytes = ctx->audio_rp > ctx->audio_wp ?
					ctx->audio_buffer_capacity - (ctx->audio_rp - ctx->audio_wp) :
					ctx->audio_wp - ctx->audio_rp;

	return avail_bytes;
}

static uint32_t rb_get_write_size(SB16_DRV_CONTEXT *ctx)
{
	/* Do we have enough space? Find free segment (section) size. */
	uint32_t free_seg_size = ctx->audio_wp >= ctx->audio_rp ?
					ctx->audio_buffer_capacity - (ctx->audio_wp - ctx->audio_rp) :
					ctx->audio_rp - ctx->audio_wp;

	/* We should always keep 1 byte difference between two
	 * positions.
	 */
	free_seg_size--;

	return free_seg_size;
}

/*
 * Reads up to `max` bytes from the ring buffer. Opposed to `rb_read()` this
 * routine returns E_BUFFERUNDERFLOW only when there are no bytes to read.
 */
static HRESULT rb_read_upto(SB16_DRV_CONTEXT *ctx, uint8_t *dst, size_t max, size_t *actual_bytes)
{
	uint32_t size = rb_get_read_size(ctx);

	size = size > max ? max : size;
	if (actual_bytes) *actual_bytes = size;

	if (size == 0) {
		return E_BUFFERUNDERFLOW;
	}

	return rb_read(ctx, dst, size);
}

/**
 * Retrieves the current IRQ and DMA channel from mixer
 */
static HRESULT sb_get_mixer_irq_dma(uint32_t *irq, uint32_t *dma_channel)
{
	uint32_t irq_status;
	uint32_t dma_status;

	irq_status = sb_read_mixer(0x80);
	dma_status = sb_read_mixer(0x81);

	switch (irq_status) {
		case 1: *irq = 2; break;
		case 2: *irq = 5; break;
		case 4: *irq = 7; break;
		case 8: *irq = 10; break;
		default:
			return E_FAIL;
	}

	switch (dma_status) {
		case 1: *dma_channel = 0; break;
		case 2: *dma_channel = 1; break;
		case 8: *dma_channel = 3; break;
		case 32: *dma_channel = 5; break;
		case 64: *dma_channel = 6; break;
		case 128: *dma_channel = 7; break;
		default:
			return E_FAIL;
	}

	return S_OK;
}

/**
 * Reset the mixer chip by writing any value to register
 * address 0x00
 */
static void sb_reset_mixer()
{
	sb_write_mixer(0x00, 0);
}

/**
 * Atomically updates the player state of a driver
 */
static void sb16_update_player_state(SB16_DRV_CONTEXT *ctx, uint32_t new_state)
{
	atomic_update_int(&ctx->player_state, new_state);
}

/**
 * Reset DSP. For non-enlightened technical observers like me, we must
 * state that some legacy ISA devices are equipped with hardware jumpers on them
 * which switch the IO ports that the card/device use.
 *
 * Probably Sound Blaster 16's floating IO ports are decided by the jumper state
 * and it has to be either hardcoded in the driver, or described in some kind of
 * driver configuration file.
 */
static HRESULT sb16_reset_dsp(uint32_t base) {
	volatile uint32_t timeout;

	/* Setup port addresses */
	sb16_ports.dsp_reset = base + 0x06;
	sb16_ports.dsp_read = base + 0x0A;
	sb16_ports.dsp_write = base + 0x0C;
	sb16_ports.dsp_read_status = base + 0x0E;
	sb16_ports.int_acknwg16 = base + 0x0F;
	sb16_ports.mixer_addr = base + 0x04;
	sb16_ports.mixer_data = base + 0x05;

	/* Write 1 to reset port */
	WRITE_PORT_UCHAR(sb16_ports.dsp_reset, 1);

	/* Wait 3 microseconds (with sched_update_sw() we have no idea
	 * how much usec we wait).
	 */
	//sched_update_sw();
	timeout = 255;
	while (--timeout) {};

	/* Write 0 to reset port */
	WRITE_PORT_UCHAR(sb16_ports.dsp_reset, 0);

	/* Wait 100 microseconds (implementing usleep() would help) */
	sched_update_sw();
	sched_update_sw();
	sched_update_sw();

	timeout = 65535;

	/* Poll read status register, until bit 7 is set */
	while ((READ_PORT_UCHAR(sb16_ports.dsp_read_status) & 0x80) == 0) {
		if (--timeout == 0) {
			goto fail;
		}
	}

	if (READ_PORT_UCHAR(sb16_ports.dsp_read) != 0xAA) {
		goto fail;
	}

	/* Success, we found it */
	return S_OK;

fail:
	memset(&sb16_ports, 0, sizeof(sb16_ports));
	return E_FAIL;
}

/**
 * Enables/disables the Digital-to-Analog Converter speaker.
 */
static void sb_enable_dac_speaker(uint8_t enable)
{
	if (enable) {
		sb_write_dsp(CMD_SPEAKER_ON);
	}else {
		sb_write_dsp(CMD_SPEAKER_OFF);
	}
}

/**
 * Retrieves sound blaster version and writes it to driver context.
 */
static HRESULT sb_get_version(uint32_t *pmaj, uint32_t *pmin)
{
	HRESULT hr;

	/* Write command */
	hr = sb_write_dsp(CMD_GET_VERSION);
	if (FAILED(hr)) return hr;

	/* Read back major and minor version */
	*pmaj = sb_read_dsp(&hr);
	if (FAILED(hr)) return hr;

	*pmin = sb_read_dsp(&hr);
	if (FAILED(hr)) return hr;

	/* Success */
	return S_OK;
}

static VOID __cdecl sb16_isr(K_REGISTERS regs)
{
	UNUSED_ARG(regs);

	/* Read interrupt status from mixer register 0x82 */
	SB16_DRV_CONTEXT 	*ctx = drv_ctx; //todo: receive context from ISR data pointer
	uint32_t 			intr_status = sb_read_mixer(0x82);

	/* Switch to current DMA buffer half */
	ctx->dma_current_half ^= 1;

	/* Handle stop request */
	if (ctx->stop_pending_flag) {
		switch (intr_status & 0x07) {
			case 1:
				sb_write_dsp(CMD_STOP_8BIT);
				break;

			case 2:
				sb_write_dsp(CMD_STOP_16BIT);
				break;

			default:
				HalKernelPanic("sb16_isr(): invalid interrupt status (read from reg 0x82).");
		}

		ctx->stop_pending_flag = FALSE;
		goto finally;
	}

	/* Lock audio buffer spinlock */
	uint32_t intf = spinlock_acquire(&ctx->audio_buffer_lock);

	if (ctx->skip_cntr) {
		ctx->skip_cntr--;
		goto unlock;
	}

	uint32_t 	size = DMA_BUFFER_SIZE / 2;
	uint32_t	actual_size;
	uint8_t 	*dma_dst = ctx->dma_current_half == 0 ? ctx->dma_memory : ctx->dma_memory + size;;

	/*
	 * Copy from ring buffer to DMA memory
	 */
	rb_read_upto(ctx, dma_dst, size, &actual_size);
	if (actual_size < size) {
		/* Fill remaining with silence */
		memset(dma_dst + actual_size, 0, size-actual_size);
		goto unlock;
	}

unlock:
	spinlock_release(&ctx->audio_buffer_lock, intf);

finally:
	/* Acknowledge interrupt */
	switch (intr_status & 0x07) {
		case 1:
			READ_PORT_UCHAR(sb16_ports.dsp_read_status);
			break;

		case 2:
			READ_PORT_UCHAR(sb16_ports.int_acknwg16);
			break;

		default:
			HalKernelPanic("sb16_isr(): invalid interrupt status (read from reg 0x82).");
	}
}

static HRESULT sb16_get_bufferred_size(K_STREAM *s, uint32_t *size)
{
	SB16_DRV_CONTEXT *ctx = get_drv_ctx(s);

	uint32_t intf = spinlock_acquire(&ctx->audio_buffer_lock);
	*size = rb_get_read_size(ctx);
	spinlock_release(&ctx->audio_buffer_lock, intf);

	return S_OK;
}

static HRESULT sb16_get_buffer_free_size(K_STREAM *s, uint32_t *size)
{
	SB16_DRV_CONTEXT *ctx = get_drv_ctx(s);

	uint32_t intf = spinlock_acquire(&ctx->audio_buffer_lock);
	*size = rb_get_write_size(ctx);

	spinlock_release(&ctx->audio_buffer_lock, intf);

	return S_OK;
}

/**
 * Sets driver internal audio format. It must be called while the driver is not
 * in playing/paused state, also not while initializing.
 */
static HRESULT sb16_set_format(K_STREAM *s, uint32_t channel_cnt, uint32_t sample_size_bits, uint32_t sample_rate)
{
	SB16_DRV_CONTEXT *ctx = get_drv_ctx(s);

	/* We cannot change format while playing */
	if (ctx->player_state != PLAYER_STATE_STOPPED) {
		return E_INVALIDSTATE;
	}

	/* We support sample rate in range [5000hz .. 44100hz] */
	if (sample_rate < 5000 || sample_rate > 44100) {
		return E_NOTSUPPORTED;
	}

	/* Sample size of 8 and 16 bit is only supported */
	if (sample_size_bits != 8 && sample_size_bits != 16) {
		return E_NOTSUPPORTED;
	}

	/* Channel count 1 to 2 is only supported */
	if (channel_cnt < 1 || channel_cnt > 2) {
		return E_NOTSUPPORTED;
	}

	/* Clear buffered samples */
	ctx->audio_rp = 0;
	ctx->audio_wp = 0;

	/* Set new format */
	ctx->fmt.channels = channel_cnt;
	ctx->fmt.sample_rate = sample_rate;
	ctx->fmt.bit_depth = sample_size_bits;
	ctx->fmt.block_size = channel_cnt * sample_size_bits / 8;

	return S_OK;
}

/**
 * Retrieves internal audio format
 */
static HRESULT sb16_get_format(K_STREAM *s, SB16_AUDIO_FORMAT *fmt)
{
	SB16_DRV_CONTEXT *ctx = get_drv_ctx(s);

	if (ctx->player_state == PLAYER_STATE_NOT_READY) {
		return E_INVALIDSTATE;
	}

	*fmt = ctx->fmt;
	return S_OK;
}

/**
 * Sends command to begin playback. If there are any audio samples
 * buffered in `audio_buffer`, commit them to DMA memory.
 */
static HRESULT sb16_begin_playback(K_STREAM *s)
{
	SB16_DRV_CONTEXT *ctx = get_drv_ctx(s);

	/* Assert current player state allows this operation */
	if (ctx->player_state == PLAYER_STATE_PLAYING || ctx->player_state == PLAYER_STATE_NOT_READY) {
		return E_INVALIDSTATE;
	}

	/* Clear DMA memory. If there is no buffered data, the DMA will
	 * stay filled with "silence".
	 */
	memset(ctx->dma_memory, 0, DMA_BUFFER_SIZE);

	/* Retrieve default IRQ id and DMA channel
	 */
	sb_get_mixer_irq_dma(&ctx->irq_id, &ctx->dma_channel);
	//vga_printf("SB16: IRQ: %d; DMA ch: %d\n", ctx->irq_id, ctx->dma_channel);

	/* Install ISR */
	register_isr_callback(irq_to_intid(ctx->irq_id), sb16_isr, ctx);

	/* Configure DMA channel (only if we are in stopped state, not paused). We can possibly
	 * iterate channels 5 to 7 and use whichever is available.
	 */
	if (ctx->player_state == PLAYER_STATE_STOPPED) {
		HRESULT hr;

		/* For 8bit samples use channel [1,3] and for 16bit samples use [5..7].
		 * Channel 2 is reserved for floppy.
		 */
		ctx->dma_mode = ctx->fmt.bit_depth == 8 ? SB16_DMA_MODE_8BIT : SB16_DMA_MODE_16BIT;
		ctx->dma_channel = ctx->dma_mode == SB16_DMA_MODE_8BIT ? 1 : 5; //hardcode this for now
		ctx->dma_current_half = 0;
		ctx->skip_cntr = 1; //Set this, because we initially fill both halves of buffer

		/* Set DMA channel back to mixer controller */
		hr = sb_set_mixer_irq_dma(ctx->irq_id, ctx->dma_channel);
		vga_printf("dma_channel=%d\n", ctx->dma_channel);
		if (FAILED(hr)) return hr;

		/* Open DMA channel. Always use single-cycle with autoinit.
		 */
		hr = isadma_open_channel(
				ctx->dma_channel,
				(void*)ctx->dma_memory_phys,
				DMA_BUFFER_SIZE,
				DMA_TRANSFER_READ,
				DMA_MODE_SINGLE,
				TRUE
		);
		//k_printf("isadma_open_channel(): hr=%x\n", hr);
		if (FAILED(hr))	return hr;
	}

	/* Commit buffered audio samples to DMA memory (if anything is buffered at all).
	 * This is known as pre-rolling.
	 */
	uint32_t	actual;
	rb_read_upto(ctx, ctx->dma_memory, DMA_BUFFER_SIZE, &actual);

	if (actual < DMA_BUFFER_SIZE) {
		ctx->skip_cntr = 0;
		ctx->dma_current_half = 1;
	}

	/* Turn DAC speakers on */
	sb_enable_dac_speaker(TRUE);

	/* Setup playback options on the DSP controller starting with
	 * sample rate
	 */
	sb_write_dsp(CMD_SET_SAMPLE_RATE);
	sb_write_dsp((ctx->fmt.sample_rate >> 8) & 0xFF);
	sb_write_dsp(ctx->fmt.sample_rate & 0xFF);

	/* Used in switch statement */
	uint8_t cmd_channel;
	uint16_t cmd_blk_size;

	/* Start output.
	 */
	switch (ctx->dma_mode) {
		case SB16_DMA_MODE_8BIT:
			cmd_channel = ctx->fmt.channels==1 ? 0x00 : 0x20;
			cmd_blk_size = (DMA_BUFFER_SIZE / 2)-1; //half size of the buffer in samples (bytes)

			sb_write_dsp(CMD_START_8BIT_OUT);
			sb_write_dsp(cmd_channel);
			sb_write_dsp(cmd_blk_size & 0xFF);
			sb_write_dsp((cmd_blk_size >> 8) & 0xFF);

			break;

		case SB16_DMA_MODE_16BIT:
			cmd_channel = ctx->fmt.channels==1 ? 0x10 : 0x30;
			cmd_blk_size = (DMA_BUFFER_SIZE / 4)-1; //half size of the buffer in samples (words)

			sb_write_dsp(CMD_START_16BIT_OUT);
			sb_write_dsp(cmd_channel);
			sb_write_dsp(cmd_blk_size & 0xFF);
			sb_write_dsp((cmd_blk_size >> 8) & 0xFF);

			break;

		default:
			/* Invalid mode */
			HalKernelPanic("sb16_begin_playback(): Unexpected DMA mode.");
	}

	/* Enter playing state */
	sb16_update_player_state(ctx, PLAYER_STATE_PLAYING);

	return S_OK;
}

/**
 * Stops the playback
 */
static HRESULT sb16_stop_playback(K_STREAM *s)
{
	SB16_DRV_CONTEXT *ctx = get_drv_ctx(s);
	uint32_t timeout;

	vga_printf("stopping.\n");

	if (ctx->player_state != PLAYER_STATE_PLAYING && ctx->player_state != PLAYER_STATE_PAUSED) {
		/* The command cannot be performed in the current state */
		return E_INVALIDSTATE;
	}

	/* Lift stop flag to notify the ISR to exit DMA transfer mode */
	atomic_update_int(&ctx->stop_pending_flag, TRUE);

	/* Wait until ISR handle the stop request. When it handle it
	 * the routine will drop back the flag. */
	timeout = 10000;
	while (ctx->stop_pending_flag) {
		sched_update_sw();
		if (--timeout == 0) return E_TIMEDOUT;
	}

	/* Turn off speaker */
	sb_enable_dac_speaker(FALSE);

	/* Uninstall IRQ handler */
	unregister_isr_callback(irq_to_intid(ctx->irq_id));

	/* Close DMA channel */
	isadma_close_channel(ctx->dma_channel);

	/* Clear remaining buffer */
	ctx->audio_rp = 0;
	ctx->audio_wp = 0;

	return S_OK;
}

/**
 * Releases driver's resources allocated by sb16_device_init()
 */
static HRESULT sb16_device_fini(K_DEVICE *self)
{
	SB16_DRV_CONTEXT *c = self->opaque;

	if(c == NULL) {
		HalKernelPanic("Finalizing SB16 driver while pDevice->opaque == NULL.");
	}

	/* Unmap DMA buffer */
	vmm_unmap_region(NULL, (uintptr_t)c->dma_memory, FALSE);

	if (c->audio_buffer != NULL) {
		kfree(c->audio_buffer);
	}
	kfree(c);

	return S_OK;
}

/**
 * Initializes the driver (called when mounting the driver to VFS)
 */
static HRESULT sb16_device_init(K_DEVICE *self)
{
	if (self->opaque) {
			return E_FAIL;
	}

	/* Initialize sound blaster. Manually probe for SB16 presence by
	 * attempting to initialize it with different base addresses.
	 */
	uint32_t base_ports[] = { BASE0, BASE1, BASE2, BASE3, BASE4, BASE5 };
	uint32_t base_port_cnt = 6;
	uint32_t i, b, vmaj, vmin = 0;
	SB16_DRV_CONTEXT *c;
	HRESULT hr = S_OK;

	for (i=0; i<base_port_cnt; i++) {
		if (SUCCEEDED(sb16_reset_dsp(base_ports[i]))) {
			b = base_ports[i];
			break;
		}
	}

	/* If not found, then no SB card is present */
	if (b == 0) {
		return E_FAIL;
	}

	/*
	 * Get device version
	 */
	hr = sb_get_version(&vmaj, &vmin);
	if (FAILED(hr)) goto fail;

//	vga_printf("SoundBlater 16 card found with base port %x; version:%d.%d\n", base_ports[i], vmaj, vmin);
	if (vmaj < 4) {
		vga_printf("Version below 4.0 are not supported by driver.");
		return E_FAIL;
	}

	/* Reset mixer */
	sb_reset_mixer();
	sb_set_volume(22);

	/* Allocate driver context */
	c = kcalloc(sizeof(SB16_DRV_CONTEXT));
	self->opaque = c;
	drv_ctx 	 = c;

	/* Initialize context */
	c->player_state	= PLAYER_STATE_NOT_READY;
	c->dma_memory 	= (uint8_t*)(KERNEL_TEMP_START - 0x10000); //Place DMA buffer right before beginning of TEMP region
	c->version_major= vmaj;
	c->version_minor= vmin;
	c->audio_buffer_capacity = 128 * 1024 * 3;
	c->audio_buffer	= kmalloc(c->audio_buffer_capacity); //additional 128 kb buffer

	/*
	 * Map dma_memory to random physical memory location below 16mb mark
	 */
	hr = vmm_alloc_and_map_limited(NULL, (uintptr_t)c->dma_memory, 0xFFFFFF, DMA_BUFFER_SIZE, USAGE_DATA, ACCESS_READWRITE, 1);
	if (FAILED(hr)) goto fail;

	/*
	 * Get physical address
	 */
	hr = vmm_get_region_phys_addr(NULL, (uintptr_t)c->dma_memory, &c->dma_memory_phys);
	if (FAILED(hr)) goto fail;

	/* Make sure physical memory is aligned at 64kb boundary.
	 * This is not guaranteed by vmm_alloc_and_map_limited() so a feature
	 * has to be implemented, which allows a flag to be set to require
	 * 64kb alignment of physical memory.
	*/
	if ((c->dma_memory_phys & 0xFFFF) != 0) {
		HalKernelPanic("SB16: DMA buffer is not on 64k physical boundary.");
	}

	/* Success */
	goto finally;

fail:
	sb16_device_fini(self);
	return E_FAIL;

finally:
	sb16_update_player_state(c, PLAYER_STATE_STOPPED);
	return hr;
}

/**
 * ioctl handler.
 */
static HRESULT sb16_ioctl(K_STREAM *s, uint32_t code, void *arg)
{
	switch (code) {
		case IOCTL_AUDIO_BEGIN_PLAYBACK:
			return sb16_begin_playback(s);

		case IOCTL_AUDIO_STOP_PLAYBACK:
			return sb16_stop_playback(s);

		case IOCTL_AUDIO_SET_FORMAT: {
			SB16_AUDIO_FORMAT *af = arg;
			return sb16_set_format(s, af->channels, af->bit_depth, af->sample_rate);
		}

		case IOCTL_AUDIO_GET_FORMAT:
			return sb16_get_format(s, arg);

		case IOCTL_AUDIO_GET_PLAYER_STATE:
			*(uint32_t*)arg = get_drv_ctx(s)->player_state;
			return S_OK;


		case IOCTL_AUDIO_BUFFER_FREE_SIZE:
			return sb16_get_buffer_free_size(s, arg);

		case IOCTL_AUDIO_GET_BUFFERED_SIZE:
			return sb16_get_bufferred_size(s, arg);

		/*
		 * Handle open and close signals.
		 */
		case DEVIO_OPEN:
		case DEVIO_CLOSE:
			return S_OK;

		default:
			return E_NOTSUPPORTED;
	}

	return S_OK;
}

/**
 * If in playing/paused state:
 * 	- Writes to audio buffer. If not enough space, returns error.
 *
 * If in any other state - fails.
 */
static HRESULT sb16_write(K_STREAM *str, const int32_t block_size, void *in_buf, size_t *bytes_written)
{
	SB16_DRV_CONTEXT *ctx = get_drv_ctx(str);
	uint32_t	intf;
	HRESULT		hr = S_OK;

	intf = spinlock_acquire(&ctx->audio_buffer_lock);

	/* Write to ring buffer */
	hr = rb_write(ctx, in_buf, block_size);
	if (FAILED(hr)) goto unlock;

//	k_printf("write successful! block_size=%x rp=%x wp=%x cap=%x\n",
//				block_size, ctx->audio_rp, ctx->audio_wp, ctx->audio_buffer_capacity);

	if (bytes_written) *bytes_written = block_size;

unlock:
	spinlock_release(&ctx->audio_buffer_lock, intf);
	return hr;
}

/*
 * Describe the driver in K_DEVICE structure
 */
static K_DEVICE sb16_device = {
		.default_url = "/dev/sb16",
		.opaque = NULL,
		.type 	= DEVICE_TYPE_CHAR,
		.read 	= NULL, //recording not implemented
		.write 	= sb16_write,
		.ioctl 	= sb16_ioctl,
		.seek 	= NULL,
		.tell 	= NULL,
		.open 	= NULL,
		.close 	= NULL,
		.initialize = sb16_device_init,
		.finalize = sb16_device_fini,
};

HRESULT __nxapi sb16_install()
{
	return vfs_mount_device(&sb16_device, sb16_device.default_url);
}

HRESULT __nxapi sb16_uninstall()
{
	return vfs_unmount_device(sb16_device.default_url);
}
