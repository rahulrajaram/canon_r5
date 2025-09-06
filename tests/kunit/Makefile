# SPDX-License-Identifier: GPL-2.0

# Canon R5 KUnit Tests
obj-$(CONFIG_CANON_R5_CORE_KUNIT_TEST) += canon-r5-core-test.o
obj-$(CONFIG_CANON_R5_PTP_KUNIT_TEST) += canon-r5-ptp-test.o
obj-$(CONFIG_CANON_R5_STORAGE_KUNIT_TEST) += canon-r5-storage-test.o
obj-$(CONFIG_CANON_R5_AUDIO_KUNIT_TEST) += canon-r5-audio-test.o
obj-$(CONFIG_CANON_R5_VIDEO_KUNIT_TEST) += canon-r5-video-test.o

# Include paths for test files
ccflags-y += -I$(srctree)/$(src)/../../include