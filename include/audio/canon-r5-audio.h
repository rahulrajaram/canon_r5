/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Canon R5 Linux Driver Suite
 * ALSA audio capture driver definitions
 *
 * Copyright (C) 2025 Canon R5 Driver Project
 */

#ifndef __CANON_R5_AUDIO_H__
#define __CANON_R5_AUDIO_H__

#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/control.h>
#include <sound/info.h>

/* Forward declarations */
struct canon_r5_device;
struct canon_r5_audio_device;

/* Audio formats supported */
#define CANON_R5_AUDIO_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | \
				SNDRV_PCM_FMTBIT_S24_3LE | \
				SNDRV_PCM_FMTBIT_S32_LE)

/* Supported sample rates */
#define CANON_R5_AUDIO_RATES (SNDRV_PCM_RATE_48000 | \
			      SNDRV_PCM_RATE_96000)

/* Audio channel configurations */
enum canon_r5_audio_channels {
	CANON_R5_AUDIO_MONO = 1,
	CANON_R5_AUDIO_STEREO = 2,
	CANON_R5_AUDIO_CHANNELS_MAX = 2
};

/* Audio input sources */
enum canon_r5_audio_input {
	CANON_R5_AUDIO_INPUT_INTERNAL = 0,	/* Built-in microphone */
	CANON_R5_AUDIO_INPUT_EXTERNAL,		/* External microphone */
	CANON_R5_AUDIO_INPUT_LINE,		/* Line input */
	CANON_R5_AUDIO_INPUT_COUNT
};

/* Audio recording modes */
enum canon_r5_audio_mode {
	CANON_R5_AUDIO_MODE_MANUAL = 0,		/* Manual levels */
	CANON_R5_AUDIO_MODE_AUTO,		/* Auto level control */
	CANON_R5_AUDIO_MODE_WIND_CUT,		/* Wind noise reduction */
	CANON_R5_AUDIO_MODE_COUNT
};

/* Audio quality settings */
struct canon_r5_audio_quality {
	u32 sample_rate;		/* Sample rate in Hz */
	u8 channels;			/* Number of channels */
	u8 bit_depth;			/* Bit depth (16, 24, 32) */
	enum canon_r5_audio_input input_source;
	enum canon_r5_audio_mode recording_mode;
	
	/* Level controls */
	u8 input_gain;			/* Input gain 0-100 */
	u8 monitoring_level;		/* Headphone monitoring 0-100 */
	bool limiter_enabled;		/* Audio limiter */
	bool low_cut_filter;		/* High-pass filter */
};

/* Audio buffer management */
struct canon_r5_audio_buffer {
	void *data;
	size_t size;
	size_t pos;
	dma_addr_t dma_addr;
	struct list_head list;
};

/* Audio capture statistics */
struct canon_r5_audio_stats {
	u64 frames_captured;
	u64 frames_dropped;
	u64 total_bytes;
	u32 buffer_overruns;
	u32 buffer_underruns;
	ktime_t last_capture;
	u32 peak_level_left;
	u32 peak_level_right;
};

/* ALSA PCM runtime data */
struct canon_r5_audio_pcm {
	struct snd_pcm_substream *substream;
	struct canon_r5_audio_device *audio;
	
	/* Buffer management */
	struct list_head buffer_list;
	struct list_head free_buffers;
	spinlock_t buffer_lock;
	
	/* Capture state */
	bool capture_active;
	atomic_t buffer_pos;
	struct work_struct capture_work;
	
	/* DMA coherent buffer */
	void *dma_area;
	dma_addr_t dma_addr;
	size_t dma_bytes;
};

/* Audio device structure */
struct canon_r5_audio_device {
	struct canon_r5_device *canon_dev;
	struct snd_card *card;
	struct snd_pcm *pcm;
	
	/* Device state */
	struct mutex lock;
	bool initialized;
	bool capture_enabled;
	
	/* Audio quality settings */
	struct canon_r5_audio_quality quality;
	
	/* PCM substream data */
	struct canon_r5_audio_pcm capture_pcm;
	
	/* Audio processing */
	struct workqueue_struct *audio_wq;
	struct work_struct level_work;
	
	/* Statistics */
	struct canon_r5_audio_stats stats;
	
	/* ALSA controls */
	struct {
		struct snd_kcontrol *input_gain;
		struct snd_kcontrol *monitoring_level;
		struct snd_kcontrol *input_source;
		struct snd_kcontrol *recording_mode;
		struct snd_kcontrol *limiter;
		struct snd_kcontrol *low_cut;
	} controls;
};

/* Audio driver private data */
struct canon_r5_audio {
	struct canon_r5_audio_device device;
	
	/* Memory management */
	struct {
		void *buffer_pool;
		size_t buffer_size;
		unsigned long *bitmap;
		spinlock_t lock;
	} memory;
	
	/* Proc interface */
	struct snd_info_entry *proc_entry;
};

/* API functions */
int canon_r5_audio_init(struct canon_r5_device *dev);
void canon_r5_audio_cleanup(struct canon_r5_device *dev);

/* Audio quality and settings */
int canon_r5_audio_set_quality(struct canon_r5_audio_device *audio,
			       const struct canon_r5_audio_quality *quality);
int canon_r5_audio_get_quality(struct canon_r5_audio_device *audio,
			       struct canon_r5_audio_quality *quality);

/* Capture control */
int canon_r5_audio_start_capture(struct canon_r5_audio_device *audio);
int canon_r5_audio_stop_capture(struct canon_r5_audio_device *audio);
int canon_r5_audio_pause_capture(struct canon_r5_audio_device *audio, bool pause);

/* Level monitoring */
int canon_r5_audio_get_levels(struct canon_r5_audio_device *audio,
			     u32 *left_level, u32 *right_level);
int canon_r5_audio_set_input_gain(struct canon_r5_audio_device *audio, u8 gain);
int canon_r5_audio_set_monitoring_level(struct canon_r5_audio_device *audio, u8 level);

/* Statistics */
int canon_r5_audio_get_stats(struct canon_r5_audio_device *audio,
			    struct canon_r5_audio_stats *stats);
void canon_r5_audio_reset_stats(struct canon_r5_audio_device *audio);

/* ALSA callbacks */
extern const struct snd_pcm_ops canon_r5_audio_pcm_ops;
extern const struct snd_pcm_hardware canon_r5_audio_pcm_hardware;

/* Internal functions */
void canon_r5_audio_capture_work(struct work_struct *work);
void canon_r5_audio_level_work(struct work_struct *work);

/* Memory management */
void *canon_r5_audio_alloc_buffer(struct canon_r5_audio_device *audio, size_t size);
void canon_r5_audio_free_buffer(struct canon_r5_audio_device *audio, void *buffer);

/* ALSA control callbacks */
int canon_r5_audio_create_controls(struct canon_r5_audio_device *audio);
void canon_r5_audio_free_controls(struct canon_r5_audio_device *audio);

/* Proc interface */
int canon_r5_audio_create_proc(struct canon_r5_audio_device *audio);
void canon_r5_audio_free_proc(struct canon_r5_audio_device *audio);

/* Debugging */
#define canon_r5_audio_dbg(audio, fmt, ...) \
	dev_dbg((audio)->canon_dev->dev, "[AUDIO] " fmt, ##__VA_ARGS__)

#define canon_r5_audio_info(audio, fmt, ...) \
	dev_info((audio)->canon_dev->dev, "[AUDIO] " fmt, ##__VA_ARGS__)

#define canon_r5_audio_warn(audio, fmt, ...) \
	dev_warn((audio)->canon_dev->dev, "[AUDIO] " fmt, ##__VA_ARGS__)

#define canon_r5_audio_err(audio, fmt, ...) \
	dev_err((audio)->canon_dev->dev, "[AUDIO] " fmt, ##__VA_ARGS__)

/* Helper functions */
const char *canon_r5_audio_input_name(enum canon_r5_audio_input input);
const char *canon_r5_audio_mode_name(enum canon_r5_audio_mode mode);

/* Format validation */
bool canon_r5_audio_format_valid(u32 format);
bool canon_r5_audio_rate_valid(u32 rate);
bool canon_r5_audio_channels_valid(u8 channels);

/* Settings validation */
int canon_r5_audio_validate_quality(const struct canon_r5_audio_quality *quality);

/* PTP audio commands */
int canon_r5_ptp_audio_start_recording(struct canon_r5_device *dev);
int canon_r5_ptp_audio_stop_recording(struct canon_r5_device *dev);
int canon_r5_ptp_audio_set_input(struct canon_r5_device *dev, enum canon_r5_audio_input input);
int canon_r5_ptp_audio_set_gain(struct canon_r5_device *dev, u8 gain);
int canon_r5_ptp_audio_get_levels(struct canon_r5_device *dev, u32 *left, u32 *right);

#endif /* __CANON_R5_AUDIO_H__ */