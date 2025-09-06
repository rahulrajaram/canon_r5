// SPDX-License-Identifier: GPL-2.0
/*
 * Canon R5 Linux Driver Suite
 * ALSA audio capture driver implementation
 *
 * Copyright (C) 2025 Canon R5 Driver Project
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>
#include <linux/timer.h>
#include <linux/atomic.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/dma-mapping.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/control.h>
#include <sound/info.h>
#include <sound/initval.h>

#include "../../include/core/canon-r5.h"
#include "../../include/core/canon-r5-ptp.h"
#include "../../include/audio/canon-r5-audio.h"

/* Module information */
MODULE_AUTHOR("Canon R5 Driver Project");
MODULE_DESCRIPTION("Canon R5 ALSA Audio Capture Driver");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("1.0.0");

/* ALSA module parameters */
static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;
static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for Canon R5 audio driver.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for Canon R5 audio driver.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable Canon R5 audio driver.");

/* PCM hardware definition */
const struct snd_pcm_hardware canon_r5_audio_pcm_hardware = {
	.info = SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_BATCH |
		SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_BLOCK_TRANSFER,
	.formats = CANON_R5_AUDIO_FORMATS,
	.rates = CANON_R5_AUDIO_RATES,
	.rate_min = 48000,
	.rate_max = 96000,
	.channels_min = 1,
	.channels_max = CANON_R5_AUDIO_CHANNELS_MAX,
	.buffer_bytes_max = 64 * 1024,
	.period_bytes_min = 1024,
	.period_bytes_max = 16 * 1024,
	.periods_min = 2,
	.periods_max = 32,
};

/* Helper functions for validation and naming */
bool canon_r5_audio_format_valid(u32 format)
{
	return format == SNDRV_PCM_FORMAT_S16_LE ||
	       format == SNDRV_PCM_FORMAT_S24_3LE ||
	       format == SNDRV_PCM_FORMAT_S32_LE;
}

bool canon_r5_audio_rate_valid(u32 rate)
{
	return rate == 48000 || rate == 96000;
}

bool canon_r5_audio_channels_valid(u8 channels)
{
	return channels >= 1 && channels <= CANON_R5_AUDIO_CHANNELS_MAX;
}

const char *canon_r5_audio_input_name(enum canon_r5_audio_input input)
{
	static const char *names[] = {
		"Internal Microphone",
		"External Microphone",
		"Line Input"
	};
	
	if (input < CANON_R5_AUDIO_INPUT_COUNT)
		return names[input];
	return "Unknown";
}

const char *canon_r5_audio_mode_name(enum canon_r5_audio_mode mode)
{
	static const char *names[] = {
		"Manual",
		"Auto Level Control",
		"Wind Cut Filter"
	};
	
	if (mode < CANON_R5_AUDIO_MODE_COUNT)
		return names[mode];
	return "Unknown";
}

/* Settings validation */
int canon_r5_audio_validate_quality(const struct canon_r5_audio_quality *quality)
{
	if (!quality)
		return -EINVAL;
		
	if (!canon_r5_audio_rate_valid(quality->sample_rate))
		return -EINVAL;
		
	if (!canon_r5_audio_channels_valid(quality->channels))
		return -EINVAL;
		
	if (quality->bit_depth != 16 && quality->bit_depth != 24 && quality->bit_depth != 32)
		return -EINVAL;
		
	if (quality->input_source >= CANON_R5_AUDIO_INPUT_COUNT)
		return -EINVAL;
		
	if (quality->recording_mode >= CANON_R5_AUDIO_MODE_COUNT)
		return -EINVAL;
		
	if (quality->input_gain > 100 || quality->monitoring_level > 100)
		return -EINVAL;
		
	return 0;
}

/* Memory management */
void *canon_r5_audio_alloc_buffer(struct canon_r5_audio_device *audio, size_t size)
{
	struct canon_r5_audio *priv = container_of(audio, struct canon_r5_audio, device);
	unsigned long flags;
	void *buffer = NULL;
	int bit;
	
	spin_lock_irqsave(&priv->memory.lock, flags);
	
	bit = find_first_zero_bit(priv->memory.bitmap, priv->memory.buffer_size / PAGE_SIZE);
	if (bit < priv->memory.buffer_size / PAGE_SIZE) {
		set_bit(bit, priv->memory.bitmap);
		buffer = priv->memory.buffer_pool + (bit * PAGE_SIZE);
	}
	
	spin_unlock_irqrestore(&priv->memory.lock, flags);
	
	return buffer;
}

void canon_r5_audio_free_buffer(struct canon_r5_audio_device *audio, void *buffer)
{
	struct canon_r5_audio *priv = container_of(audio, struct canon_r5_audio, device);
	unsigned long flags;
	int bit;
	
	if (!buffer)
		return;
		
	spin_lock_irqsave(&priv->memory.lock, flags);
	
	bit = ((char *)buffer - (char *)priv->memory.buffer_pool) / PAGE_SIZE;
	if (bit >= 0 && bit < priv->memory.buffer_size / PAGE_SIZE)
		clear_bit(bit, priv->memory.bitmap);
		
	spin_unlock_irqrestore(&priv->memory.lock, flags);
}

/* PTP audio command stubs */
int canon_r5_ptp_audio_start_recording(struct canon_r5_device *dev)
{
	/* Stub implementation using existing PTP command framework */
	return canon_r5_ptp_command(dev, 0x9170, NULL, 0, NULL, 0, NULL);
}

int canon_r5_ptp_audio_stop_recording(struct canon_r5_device *dev)
{
	/* Stub implementation using existing PTP command framework */
	return canon_r5_ptp_command(dev, 0x9171, NULL, 0, NULL, 0, NULL);
}

int canon_r5_ptp_audio_set_input(struct canon_r5_device *dev, enum canon_r5_audio_input input)
{
	u32 params = (u32)input;
	return canon_r5_ptp_command(dev, 0x9172, &params, 1, NULL, 0, NULL);
}

int canon_r5_ptp_audio_set_gain(struct canon_r5_device *dev, u8 gain)
{
	u32 params = (u32)gain;
	return canon_r5_ptp_command(dev, 0x9173, &params, 1, NULL, 0, NULL);
}

int canon_r5_ptp_audio_get_levels(struct canon_r5_device *dev, u32 *left, u32 *right)
{
	u8 levels[8];
	u16 response_code = 0;
	int ret;
	
	ret = canon_r5_ptp_command(dev, 0x9174, NULL, 0, levels, sizeof(levels), &response_code);
	if (ret)
		return ret;
		
	if (response_code == 0x2001) { /* PTP_RC_OK */
		*left = le32_to_cpu(*(u32 *)&levels[0]);
		*right = le32_to_cpu(*(u32 *)&levels[4]);
	}
	
	return 0;
}

/* Work functions */
void canon_r5_audio_capture_work(struct work_struct *work)
{
	struct canon_r5_audio_pcm *pcm = container_of(work, struct canon_r5_audio_pcm, capture_work);
	struct canon_r5_audio_device *audio = pcm->audio;
	struct snd_pcm_runtime *runtime = pcm->substream->runtime;
	unsigned long flags;
	
	canon_r5_audio_dbg(audio, "Processing audio capture work");
	
	spin_lock_irqsave(&pcm->buffer_lock, flags);
	
	/* Simulate audio data capture by advancing buffer position */
	if (pcm->capture_active) {
		size_t period_bytes = frames_to_bytes(runtime, runtime->period_size);
		int pos = atomic_read(&pcm->buffer_pos);
		
		pos += period_bytes;
		if (pos >= frames_to_bytes(runtime, runtime->buffer_size))
			pos = 0;
			
		atomic_set(&pcm->buffer_pos, pos);
		audio->stats.frames_captured += runtime->period_size;
		audio->stats.total_bytes += period_bytes;
		audio->stats.last_capture = ktime_get();
	}
	
	spin_unlock_irqrestore(&pcm->buffer_lock, flags);
	
	if (pcm->capture_active)
		snd_pcm_period_elapsed(pcm->substream);
}

void canon_r5_audio_level_work(struct work_struct *work)
{
	struct canon_r5_audio_device *audio = container_of(work, struct canon_r5_audio_device, level_work);
	u32 left_level, right_level;
	int ret;
	
	ret = canon_r5_ptp_audio_get_levels(audio->canon_dev, &left_level, &right_level);
	if (!ret) {
		audio->stats.peak_level_left = left_level;
		audio->stats.peak_level_right = right_level;
	}
}

/* PCM operations */
static int canon_r5_audio_pcm_open(struct snd_pcm_substream *substream)
{
	struct canon_r5_audio_device *audio = snd_pcm_substream_chip(substream);
	struct canon_r5_audio_pcm *pcm = &audio->capture_pcm;
	struct snd_pcm_runtime *runtime = substream->runtime;
	int ret;
	
	canon_r5_audio_dbg(audio, "Opening PCM capture stream");
	
	mutex_lock(&audio->lock);
	
	pcm->substream = substream;
	runtime->hw = canon_r5_audio_pcm_hardware;
	
	/* Initialize buffer management */
	INIT_LIST_HEAD(&pcm->buffer_list);
	INIT_LIST_HEAD(&pcm->free_buffers);
	spin_lock_init(&pcm->buffer_lock);
	atomic_set(&pcm->buffer_pos, 0);
	
	INIT_WORK(&pcm->capture_work, canon_r5_audio_capture_work);
	
	ret = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0) {
		canon_r5_audio_err(audio, "Failed to set period constraint: %d", ret);
		goto error;
	}
	
	mutex_unlock(&audio->lock);
	return 0;
	
error:
	mutex_unlock(&audio->lock);
	return ret;
}

static int canon_r5_audio_pcm_close(struct snd_pcm_substream *substream)
{
	struct canon_r5_audio_device *audio = snd_pcm_substream_chip(substream);
	struct canon_r5_audio_pcm *pcm = &audio->capture_pcm;
	
	canon_r5_audio_dbg(audio, "Closing PCM capture stream");
	
	mutex_lock(&audio->lock);
	
	pcm->capture_active = false;
	cancel_work_sync(&pcm->capture_work);
	pcm->substream = NULL;
	
	mutex_unlock(&audio->lock);
	return 0;
}

static int canon_r5_audio_pcm_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params)
{
	struct canon_r5_audio_device *audio = snd_pcm_substream_chip(substream);
	struct canon_r5_audio_pcm *pcm = &audio->capture_pcm;
	size_t buffer_bytes = params_buffer_bytes(params);
	int ret;
	
	canon_r5_audio_dbg(audio, "Setting HW params: rate=%d, channels=%d, format=%d, buffer_bytes=%zu",
			   params_rate(params), params_channels(params),
			   params_format(params), buffer_bytes);
	
	mutex_lock(&audio->lock);
	
	/* Allocate DMA coherent buffer */
	pcm->dma_area = dma_alloc_coherent(audio->canon_dev->dev, buffer_bytes,
					   &pcm->dma_addr, GFP_KERNEL);
	if (!pcm->dma_area) {
		canon_r5_audio_err(audio, "Failed to allocate DMA buffer");
		ret = -ENOMEM;
		goto error;
	}
	
	pcm->dma_bytes = buffer_bytes;
	
	/* Set up runtime buffer */
	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);
	substream->dma_buffer.area = pcm->dma_area;
	substream->dma_buffer.addr = pcm->dma_addr;
	substream->dma_buffer.bytes = buffer_bytes;
	
	mutex_unlock(&audio->lock);
	return 0;
	
error:
	mutex_unlock(&audio->lock);
	return ret;
}

static int canon_r5_audio_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct canon_r5_audio_device *audio = snd_pcm_substream_chip(substream);
	struct canon_r5_audio_pcm *pcm = &audio->capture_pcm;
	
	canon_r5_audio_dbg(audio, "Freeing HW params");
	
	mutex_lock(&audio->lock);
	
	if (pcm->dma_area) {
		dma_free_coherent(audio->canon_dev->dev, pcm->dma_bytes,
				  pcm->dma_area, pcm->dma_addr);
		pcm->dma_area = NULL;
		pcm->dma_bytes = 0;
	}
	
	snd_pcm_set_runtime_buffer(substream, NULL);
	
	mutex_unlock(&audio->lock);
	return 0;
}

static int canon_r5_audio_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct canon_r5_audio_device *audio = snd_pcm_substream_chip(substream);
	struct canon_r5_audio_pcm *pcm = &audio->capture_pcm;
	
	canon_r5_audio_dbg(audio, "Preparing PCM capture");
	
	mutex_lock(&audio->lock);
	
	atomic_set(&pcm->buffer_pos, 0);
	memset(pcm->dma_area, 0, pcm->dma_bytes);
	
	mutex_unlock(&audio->lock);
	return 0;
}

static int canon_r5_audio_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct canon_r5_audio_device *audio = snd_pcm_substream_chip(substream);
	struct canon_r5_audio_pcm *pcm = &audio->capture_pcm;
	int ret = 0;
	
	canon_r5_audio_dbg(audio, "PCM trigger command: %d", cmd);
	
	mutex_lock(&audio->lock);
	
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		pcm->capture_active = true;
		ret = canon_r5_ptp_audio_start_recording(audio->canon_dev);
		if (!ret)
			queue_work(audio->audio_wq, &pcm->capture_work);
		break;
		
	case SNDRV_PCM_TRIGGER_STOP:
		pcm->capture_active = false;
		cancel_work_sync(&pcm->capture_work);
		ret = canon_r5_ptp_audio_stop_recording(audio->canon_dev);
		break;
		
	default:
		ret = -EINVAL;
		break;
	}
	
	mutex_unlock(&audio->lock);
	return ret;
}

static snd_pcm_uframes_t canon_r5_audio_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct canon_r5_audio_device *audio = snd_pcm_substream_chip(substream);
	struct canon_r5_audio_pcm *pcm = &audio->capture_pcm;
	struct snd_pcm_runtime *runtime = substream->runtime;
	int pos;
	
	pos = atomic_read(&pcm->buffer_pos);
	return bytes_to_frames(runtime, pos);
}

const struct snd_pcm_ops canon_r5_audio_pcm_ops = {
	.open = canon_r5_audio_pcm_open,
	.close = canon_r5_audio_pcm_close,
	.hw_params = canon_r5_audio_pcm_hw_params,
	.hw_free = canon_r5_audio_pcm_hw_free,
	.prepare = canon_r5_audio_pcm_prepare,
	.trigger = canon_r5_audio_pcm_trigger,
	.pointer = canon_r5_audio_pcm_pointer,
};

/* Control callbacks */
static int canon_r5_audio_input_gain_info(struct snd_kcontrol *kcontrol __attribute__((unused)),
					  struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 100;
	return 0;
}

static int canon_r5_audio_input_gain_get(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct canon_r5_audio_device *audio = snd_kcontrol_chip(kcontrol);
	
	mutex_lock(&audio->lock);
	ucontrol->value.integer.value[0] = audio->quality.input_gain;
	mutex_unlock(&audio->lock);
	
	return 0;
}

static int canon_r5_audio_input_gain_put(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct canon_r5_audio_device *audio = snd_kcontrol_chip(kcontrol);
	u8 gain = ucontrol->value.integer.value[0];
	int ret = 0;
	
	if (gain > 100)
		return -EINVAL;
		
	mutex_lock(&audio->lock);
	
	if (audio->quality.input_gain != gain) {
		audio->quality.input_gain = gain;
		ret = canon_r5_ptp_audio_set_gain(audio->canon_dev, gain);
		if (!ret)
			ret = 1; /* changed */
	}
	
	mutex_unlock(&audio->lock);
	return ret;
}

/* ALSA control creation */
int canon_r5_audio_create_controls(struct canon_r5_audio_device *audio)
{
	struct snd_kcontrol_new input_gain_control = {
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Capture Volume",
		.info = canon_r5_audio_input_gain_info,
		.get = canon_r5_audio_input_gain_get,
		.put = canon_r5_audio_input_gain_put,
	};
	int ret;
	
	audio->controls.input_gain = snd_ctl_new1(&input_gain_control, audio);
	if (!audio->controls.input_gain)
		return -ENOMEM;
		
	ret = snd_ctl_add(audio->card, audio->controls.input_gain);
	if (ret)
		return ret;
		
	return 0;
}

void canon_r5_audio_free_controls(struct canon_r5_audio_device *audio __attribute__((unused)))
{
	/* Controls are automatically freed when card is freed */
}

/* Proc interface */
static void canon_r5_audio_proc_read(struct snd_info_entry *entry,
				     struct snd_info_buffer *buffer)
{
	struct canon_r5_audio_device *audio = entry->private_data;
	struct canon_r5_audio_stats stats;
	
	canon_r5_audio_get_stats(audio, &stats);
	
	snd_iprintf(buffer, "Canon R5 Audio Driver Status\n");
	snd_iprintf(buffer, "============================\n\n");
	snd_iprintf(buffer, "Capture Statistics:\n");
	snd_iprintf(buffer, "  Frames captured: %llu\n", stats.frames_captured);
	snd_iprintf(buffer, "  Frames dropped: %llu\n", stats.frames_dropped);
	snd_iprintf(buffer, "  Total bytes: %llu\n", stats.total_bytes);
	snd_iprintf(buffer, "  Buffer overruns: %u\n", stats.buffer_overruns);
	snd_iprintf(buffer, "  Buffer underruns: %u\n", stats.buffer_underruns);
	snd_iprintf(buffer, "\nAudio Levels:\n");
	snd_iprintf(buffer, "  Peak level (L): %u\n", stats.peak_level_left);
	snd_iprintf(buffer, "  Peak level (R): %u\n", stats.peak_level_right);
	snd_iprintf(buffer, "\nCurrent Settings:\n");
	snd_iprintf(buffer, "  Sample rate: %u Hz\n", audio->quality.sample_rate);
	snd_iprintf(buffer, "  Channels: %u\n", audio->quality.channels);
	snd_iprintf(buffer, "  Bit depth: %u\n", audio->quality.bit_depth);
	snd_iprintf(buffer, "  Input source: %s\n", canon_r5_audio_input_name(audio->quality.input_source));
	snd_iprintf(buffer, "  Recording mode: %s\n", canon_r5_audio_mode_name(audio->quality.recording_mode));
}

int canon_r5_audio_create_proc(struct canon_r5_audio_device *audio)
{
	struct canon_r5_audio *priv = container_of(audio, struct canon_r5_audio, device);
	
	priv->proc_entry = snd_info_create_card_entry(audio->card, "canon_r5_audio", 
						      audio->card->proc_root);
	if (!priv->proc_entry)
		return -ENOMEM;
		
	snd_info_set_text_ops(priv->proc_entry, audio, canon_r5_audio_proc_read);
	
	return 0;
}

void canon_r5_audio_free_proc(struct canon_r5_audio_device *audio)
{
	struct canon_r5_audio *priv = container_of(audio, struct canon_r5_audio, device);
	
	if (priv->proc_entry)
		snd_info_free_entry(priv->proc_entry);
}

/* Public API functions */
int canon_r5_audio_set_quality(struct canon_r5_audio_device *audio,
			       const struct canon_r5_audio_quality *quality)
{
	int ret;
	
	if (!audio || !quality)
		return -EINVAL;
		
	ret = canon_r5_audio_validate_quality(quality);
	if (ret)
		return ret;
		
	mutex_lock(&audio->lock);
	audio->quality = *quality;
	mutex_unlock(&audio->lock);
	
	canon_r5_audio_info(audio, "Audio quality updated: %uHz, %uch, %ubit",
			    quality->sample_rate, quality->channels, quality->bit_depth);
	
	return 0;
}

int canon_r5_audio_get_quality(struct canon_r5_audio_device *audio,
			       struct canon_r5_audio_quality *quality)
{
	if (!audio || !quality)
		return -EINVAL;
		
	mutex_lock(&audio->lock);
	*quality = audio->quality;
	mutex_unlock(&audio->lock);
	
	return 0;
}

int canon_r5_audio_start_capture(struct canon_r5_audio_device *audio)
{
	int ret;
	
	if (!audio)
		return -EINVAL;
		
	mutex_lock(&audio->lock);
	
	if (audio->capture_enabled) {
		ret = -EBUSY;
		goto unlock;
	}
	
	ret = canon_r5_ptp_audio_start_recording(audio->canon_dev);
	if (!ret) {
		audio->capture_enabled = true;
		queue_work(audio->audio_wq, &audio->level_work);
	}
	
unlock:
	mutex_unlock(&audio->lock);
	return ret;
}

int canon_r5_audio_stop_capture(struct canon_r5_audio_device *audio)
{
	int ret;
	
	if (!audio)
		return -EINVAL;
		
	mutex_lock(&audio->lock);
	
	if (!audio->capture_enabled) {
		ret = 0;
		goto unlock;
	}
	
	ret = canon_r5_ptp_audio_stop_recording(audio->canon_dev);
	audio->capture_enabled = false;
	cancel_work_sync(&audio->level_work);
	
unlock:
	mutex_unlock(&audio->lock);
	return ret;
}

int canon_r5_audio_get_stats(struct canon_r5_audio_device *audio,
			    struct canon_r5_audio_stats *stats)
{
	if (!audio || !stats)
		return -EINVAL;
		
	mutex_lock(&audio->lock);
	*stats = audio->stats;
	mutex_unlock(&audio->lock);
	
	return 0;
}

void canon_r5_audio_reset_stats(struct canon_r5_audio_device *audio)
{
	if (!audio)
		return;
		
	mutex_lock(&audio->lock);
	memset(&audio->stats, 0, sizeof(audio->stats));
	mutex_unlock(&audio->lock);
}

/* Driver initialization */
int canon_r5_audio_init(struct canon_r5_device *dev)
{
	struct canon_r5_audio *priv;
	struct canon_r5_audio_device *audio;
	struct snd_card *card;
	struct snd_pcm *pcm;
	int ret;
	
	if (!dev) {
		pr_err("Canon R5 Audio: Invalid device\n");
		return -EINVAL;
	}
	
	/* Create ALSA card */
	ret = snd_card_new(dev->dev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1,
			   THIS_MODULE, sizeof(*priv), &card);
	if (ret) {
		dev_err(dev->dev, "Failed to create ALSA card: %d\n", ret);
		return ret;
	}
	
	priv = card->private_data;
	audio = &priv->device;
	audio->canon_dev = dev;
	audio->card = card;
	
	/* Initialize device state */
	mutex_init(&audio->lock);
	audio->initialized = false;
	audio->capture_enabled = false;
	
	/* Set default quality */
	audio->quality.sample_rate = 48000;
	audio->quality.channels = 2;
	audio->quality.bit_depth = 16;
	audio->quality.input_source = CANON_R5_AUDIO_INPUT_INTERNAL;
	audio->quality.recording_mode = CANON_R5_AUDIO_MODE_AUTO;
	audio->quality.input_gain = 50;
	audio->quality.monitoring_level = 50;
	audio->quality.limiter_enabled = true;
	audio->quality.low_cut_filter = false;
	
	/* Initialize capture PCM */
	audio->capture_pcm.audio = audio;
	INIT_WORK(&audio->level_work, canon_r5_audio_level_work);
	
	/* Initialize memory management */
	priv->memory.buffer_size = 256 * 1024; /* 256KB buffer pool */
	priv->memory.buffer_pool = vmalloc(priv->memory.buffer_size);
	if (!priv->memory.buffer_pool) {
		ret = -ENOMEM;
		goto error_card;
	}
	
	priv->memory.bitmap = bitmap_zalloc(priv->memory.buffer_size / PAGE_SIZE, GFP_KERNEL);
	if (!priv->memory.bitmap) {
		ret = -ENOMEM;
		goto error_buffer;
	}
	
	spin_lock_init(&priv->memory.lock);
	
	/* Create workqueue */
	audio->audio_wq = alloc_workqueue("canon_r5_audio", WQ_MEM_RECLAIM, 0);
	if (!audio->audio_wq) {
		ret = -ENOMEM;
		goto error_bitmap;
	}
	
	/* Create PCM device */
	ret = snd_pcm_new(card, "Canon R5 Audio", 0, 0, 1, &pcm);
	if (ret) {
		dev_err(dev->dev, "Failed to create PCM device: %d\n", ret);
		goto error_wq;
	}
	
	audio->pcm = pcm;
	pcm->private_data = audio;
	strcpy(pcm->name, "Canon R5 Audio Capture");
	
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &canon_r5_audio_pcm_ops);
	
	/* Create ALSA controls */
	ret = canon_r5_audio_create_controls(audio);
	if (ret) {
		dev_err(dev->dev, "Failed to create ALSA controls: %d\n", ret);
		goto error_wq;
	}
	
	/* Create proc interface */
	ret = canon_r5_audio_create_proc(audio);
	if (ret) {
		dev_warn(dev->dev, "Failed to create proc interface: %d\n", ret);
		/* Non-fatal error */
	}
	
	/* Set card info */
	strcpy(card->driver, "canon_r5_audio");
	strcpy(card->shortname, "Canon R5 Audio");
	snprintf(card->longname, sizeof(card->longname),
		 "Canon R5 Audio Capture on %s", dev_name(dev->dev));
	
	/* Register card */
	ret = snd_card_register(card);
	if (ret) {
		dev_err(dev->dev, "Failed to register ALSA card: %d\n", ret);
		goto error_proc;
	}
	
	/* Register with core driver */
	ret = canon_r5_register_audio_driver(dev, audio);
	if (ret) {
		dev_err(dev->dev, "Failed to register audio driver: %d\n", ret);
		goto error_unregister;
	}
	
	audio->initialized = true;
	dev_info(dev->dev, "Canon R5 audio driver initialized successfully\n");
	
	return 0;
	
error_unregister:
	snd_card_disconnect(card);
error_proc:
	canon_r5_audio_free_proc(audio);
	canon_r5_audio_free_controls(audio);
error_wq:
	destroy_workqueue(audio->audio_wq);
error_bitmap:
	bitmap_free(priv->memory.bitmap);
error_buffer:
	vfree(priv->memory.buffer_pool);
error_card:
	snd_card_free(card);
	return ret;
}

void canon_r5_audio_cleanup(struct canon_r5_device *dev)
{
	struct canon_r5_audio_device *audio;
	struct canon_r5_audio *priv;
	
	if (!dev)
		return;
		
	audio = canon_r5_get_audio_driver(dev);
	if (!audio)
		return;
		
	priv = container_of(audio, struct canon_r5_audio, device);
	
	dev_info(dev->dev, "Cleaning up Canon R5 audio driver\n");
	
	/* Stop capture if active */
	canon_r5_audio_stop_capture(audio);
	
	/* Cleanup resources */
	canon_r5_audio_free_proc(audio);
	canon_r5_audio_free_controls(audio);
	
	if (audio->audio_wq) {
		flush_workqueue(audio->audio_wq);
		destroy_workqueue(audio->audio_wq);
	}
	
	if (priv->memory.bitmap)
		bitmap_free(priv->memory.bitmap);
		
	if (priv->memory.buffer_pool)
		vfree(priv->memory.buffer_pool);
	
	snd_card_disconnect(audio->card);
	snd_card_free(audio->card);
	
	canon_r5_unregister_audio_driver(dev);
}

/* Module functions */
static int __init canon_r5_audio_module_init(void)
{
	pr_info("Canon R5 Audio Driver v%s loaded\n", "1.0.0");
	return 0;
}

static void __exit canon_r5_audio_module_exit(void)
{
	pr_info("Canon R5 Audio Driver unloaded\n");
}

module_init(canon_r5_audio_module_init);
module_exit(canon_r5_audio_module_exit);

/* Export symbols for use by core driver */
EXPORT_SYMBOL_GPL(canon_r5_audio_init);
EXPORT_SYMBOL_GPL(canon_r5_audio_cleanup);
EXPORT_SYMBOL_GPL(canon_r5_audio_set_quality);
EXPORT_SYMBOL_GPL(canon_r5_audio_get_quality);
EXPORT_SYMBOL_GPL(canon_r5_audio_start_capture);
EXPORT_SYMBOL_GPL(canon_r5_audio_stop_capture);
EXPORT_SYMBOL_GPL(canon_r5_audio_get_stats);
EXPORT_SYMBOL_GPL(canon_r5_audio_reset_stats);