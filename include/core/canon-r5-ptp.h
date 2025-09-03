/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Canon R5 Linux Driver Suite
 * PTP protocol definitions and Canon extensions
 *
 * Copyright (C) 2025 Canon R5 Driver Project
 */

#ifndef __CANON_R5_PTP_H__
#define __CANON_R5_PTP_H__

#include <linux/types.h>

/* PTP container types */
#define PTP_CONTAINER_COMMAND		0x0001
#define PTP_CONTAINER_DATA		0x0002
#define PTP_CONTAINER_RESPONSE		0x0003
#define PTP_CONTAINER_EVENT		0x0004

/* Standard PTP operation codes */
#define PTP_OP_GET_DEVICE_INFO		0x1001
#define PTP_OP_OPEN_SESSION		0x1002
#define PTP_OP_CLOSE_SESSION		0x1003
#define PTP_OP_GET_STORAGE_IDS		0x1004
#define PTP_OP_GET_STORAGE_INFO		0x1005
#define PTP_OP_GET_NUM_OBJECTS		0x1006
#define PTP_OP_GET_OBJECT_HANDLES	0x1007
#define PTP_OP_GET_OBJECT_INFO		0x1008
#define PTP_OP_GET_OBJECT		0x1009
#define PTP_OP_DELETE_OBJECT		0x100A
#define PTP_OP_INITIATE_CAPTURE		0x100E
#define PTP_OP_GET_DEVICE_PROP_DESC	0x1014
#define PTP_OP_GET_DEVICE_PROP_VALUE	0x1015
#define PTP_OP_SET_DEVICE_PROP_VALUE	0x1016

/* Canon PTP extensions */
#define CANON_PTP_OP_GET_CHANGES	0x9101
#define CANON_PTP_OP_GET_FOLDER_INFO	0x9102
#define CANON_PTP_OP_CREATE_FOLDER	0x9103
#define CANON_PTP_OP_GET_PARTIAL_OBJECT	0x9107
#define CANON_PTP_OP_SET_OBJECT_TIME	0x9108
#define CANON_PTP_OP_GET_DEVICE_INFO_EX	0x9109
#define CANON_PTP_OP_SET_PROPERTY	0x9110
#define CANON_PTP_OP_CAPTURE		0x9116
#define CANON_PTP_OP_GET_PROPERTY	0x9127
#define CANON_PTP_OP_INITIATE_RELEASE_CONTROL	0x9128
#define CANON_PTP_OP_TERMINATE_RELEASE_CONTROL	0x9129
#define CANON_PTP_OP_REMOTE_RELEASE_ON	0x9130
#define CANON_PTP_OP_REMOTE_RELEASE_OFF	0x9131

/* Canon Live View operations */
#define CANON_PTP_OP_LIVEVIEW_START	0x9153
#define CANON_PTP_OP_LIVEVIEW_STOP	0x9154
#define CANON_PTP_OP_GET_LIVEVIEW	0x9155

/* Canon Still Image Capture operations */
#define CANON_PTP_OP_SET_IMAGE_QUALITY	0x9158
#define CANON_PTP_OP_GET_IMAGE_QUALITY	0x9159
#define CANON_PTP_OP_BULB_START		0x915A
#define CANON_PTP_OP_BULB_END		0x915B
#define CANON_PTP_OP_AUTOFOCUS		0x915C
#define CANON_PTP_OP_MANUAL_FOCUS	0x915D
#define CANON_PTP_OP_SET_BRACKETING	0x915E
#define CANON_PTP_OP_GET_FOCUS_INFO	0x915F
#define CANON_PTP_OP_CAPTURE_BURST	0x9160
#define CANON_PTP_OP_SET_WB		0x9161
#define CANON_PTP_OP_GET_BATTERY	0x9162
#define CANON_PTP_OP_LIVEVIEW_LOCK	0x9156
#define CANON_PTP_OP_LIVEVIEW_UNLOCK	0x9157

/* Canon focus operations */
#define CANON_PTP_OP_DRIVE_LENS		0x9158
#define CANON_PTP_OP_SET_AF_POINT	0x9159
#define CANON_PTP_OP_GET_AF_INFO	0x915A

/* Canon movie operations */
#define CANON_PTP_OP_MOVIE_START	0x915E
#define CANON_PTP_OP_MOVIE_STOP		0x915F

/* PTP response codes */
#define PTP_RC_OK			0x2001
#define PTP_RC_GENERAL_ERROR		0x2002
#define PTP_RC_SESSION_NOT_OPEN		0x2003
#define PTP_RC_INVALID_TRANSACTION_ID	0x2004
#define PTP_RC_OPERATION_NOT_SUPPORTED	0x2005
#define PTP_RC_PARAMETER_NOT_SUPPORTED	0x2006
#define PTP_RC_INCOMPLETE_TRANSFER	0x2007
#define PTP_RC_INVALID_STORAGE_ID	0x2008
#define PTP_RC_INVALID_OBJECT_HANDLE	0x2009
#define PTP_RC_DEVICE_PROP_NOT_SUPPORTED 0x200A
#define PTP_RC_INVALID_OBJECT_FORMAT_CODE 0x200B
#define PTP_RC_STORAGE_FULL		0x200C
#define PTP_RC_OBJECT_WRITE_PROTECTED	0x200D
#define PTP_RC_STORE_READ_ONLY		0x200E
#define PTP_RC_ACCESS_DENIED		0x200F
#define PTP_RC_NO_THUMBNAIL_PRESENT	0x2010
#define PTP_RC_SELF_TEST_FAILED		0x2011
#define PTP_RC_PARTIAL_DELETION		0x2012
#define PTP_RC_STORE_NOT_AVAILABLE	0x2013
#define PTP_RC_SPECIFICATION_BY_FORMAT_UNSUPPORTED 0x2014
#define PTP_RC_NO_VALID_OBJECT_INFO	0x2015
#define PTP_RC_INVALID_CODE_FORMAT	0x2016
#define PTP_RC_UNKNOWN_VENDOR_CODE	0x2017
#define PTP_RC_CAPTURE_ALREADY_ACTIVE	0x2018
#define PTP_RC_DEVICE_BUSY		0x2019
#define PTP_RC_INVALID_PARENT_OBJECT	0x201A
#define PTP_RC_INVALID_DEVICE_PROP_FORMAT 0x201B
#define PTP_RC_INVALID_DEVICE_PROP_VALUE 0x201C
#define PTP_RC_INVALID_PARAMETER	0x201D
#define PTP_RC_SESSION_ALREADY_OPEN	0x201E
#define PTP_RC_TRANSACTION_CANCELLED	0x201F
#define PTP_RC_SPECIFICATION_OF_DESTINATION_UNSUPPORTED 0x2020

/* Canon specific response codes */
#define CANON_PTP_RC_UNKNOWN_COMMAND	0xA001
#define CANON_PTP_RC_OPERATION_REFUSED	0xA005
#define CANON_PTP_RC_LENS_COVER_CLOSE	0xA006
#define CANON_PTP_RC_LOW_BATTERY	0xA101
#define CANON_PTP_RC_OBJECT_NOTREADY	0xA102
#define CANON_PTP_RC_CANNOT_MAKE_OBJECT	0xA104
#define CANON_PTP_RC_MEMORY_STATUS_NOTREADY 0xA105
#define CANON_PTP_RC_DIRECTORY_CREATION_FAILED 0xA106
#define CANON_PTP_RC_CANCEL_ALL_TRANSFERS 0xA107
#define CANON_PTP_RC_DEVICE_BUSY	0xA108

/* PTP event codes */
#define PTP_EC_CANCEL_TRANSACTION	0x4001
#define PTP_EC_OBJECT_ADDED		0x4002
#define PTP_EC_OBJECT_REMOVED		0x4003
#define PTP_EC_STORE_ADDED		0x4004
#define PTP_EC_STORE_REMOVED		0x4005
#define PTP_EC_DEVICE_PROP_CHANGED	0x4006
#define PTP_EC_OBJECT_INFO_CHANGED	0x4007
#define PTP_EC_DEVICE_INFO_CHANGED	0x4008
#define PTP_EC_REQUEST_OBJECT_TRANSFER	0x4009
#define PTP_EC_STORE_FULL		0x400A
#define PTP_EC_DEVICE_RESET		0x400B
#define PTP_EC_STORAGE_INFO_CHANGED	0x400C
#define PTP_EC_CAPTURE_COMPLETE		0x400D
#define PTP_EC_UNREPORTED_STATUS	0x400E

/* Canon event codes */
#define CANON_PTP_EC_OBJECT_CREATED	0xC181
#define CANON_PTP_EC_OBJECT_REMOVED	0xC182
#define CANON_PTP_EC_REQUEST_OBJECT_TRANSFER 0xC183
#define CANON_PTP_EC_SHUTDOWN		0xC184
#define CANON_PTP_EC_DEVICE_INFO_CHANGED 0xC185
#define CANON_PTP_EC_CAPTURE_COMPLETE_IMMEDIATELY 0xC186
#define CANON_PTP_EC_CAMERA_STATUS_CHANGED 0xC187
#define CANON_PTP_EC_WILLSHUTDOWN	0xC188
#define CANON_PTP_EC_SHUTTER_BUTTON_DOWN 0xC189
#define CANON_PTP_EC_SHUTTER_BUTTON_UP	0xC18A
#define CANON_PTP_EC_BULB_EXPOSURE_TIME	0xC18B

/* Device property codes */
#define PTP_DPC_BATTERY_LEVEL		0x5001
#define PTP_DPC_FUNCTIONAL_MODE		0x5002
#define PTP_DPC_IMAGE_SIZE		0x5003
#define PTP_DPC_COMPRESSION_SETTING	0x5004
#define PTP_DPC_WHITE_BALANCE		0x5005
#define PTP_DPC_RGB_GAIN		0x5006
#define PTP_DPC_F_NUMBER		0x5007
#define PTP_DPC_FOCAL_LENGTH		0x5008
#define PTP_DPC_FOCUS_DISTANCE		0x5009
#define PTP_DPC_FOCUS_MODE		0x500A
#define PTP_DPC_EXPOSURE_METERING_MODE	0x500B
#define PTP_DPC_FLASH_MODE		0x500C
#define PTP_DPC_EXPOSURE_TIME		0x500D
#define PTP_DPC_EXPOSURE_PROGRAM_MODE	0x500E
#define PTP_DPC_EXPOSURE_INDEX		0x500F
#define PTP_DPC_EXPOSURE_BIAS_COMPENSATION 0x5010
#define PTP_DPC_DATE_TIME		0x5011
#define PTP_DPC_CAPTURE_DELAY		0x5012
#define PTP_DPC_STILL_CAPTURE_MODE	0x5013
#define PTP_DPC_CONTRAST		0x5014
#define PTP_DPC_SHARPNESS		0x5015
#define PTP_DPC_DIGITAL_ZOOM		0x5016

/* Canon device property codes */
#define CANON_PTP_DPC_BEEP		0xD001
#define CANON_PTP_DPC_BATTERY		0xD002
#define CANON_PTP_DPC_BATTERY_KIND	0xD003
#define CANON_PTP_DPC_BATTERY_STATUS	0xD004
#define CANON_PTP_DPC_UI_LOCK		0xD005
#define CANON_PTP_DPC_CAMERA_MODE	0xD006
#define CANON_PTP_DPC_IMAGE_QUALITY	0xD007
#define CANON_PTP_DPC_FULL_VIEW_FILE_FORMAT 0xD008
#define CANON_PTP_DPC_IMAGE_SIZE	0xD009
#define CANON_PTP_DPC_SELF_TIME		0xD00A
#define CANON_PTP_DPC_FLASH_MODE	0xD00B
#define CANON_PTP_DPC_BEEP_MODE		0xD00C
#define CANON_PTP_DPC_SHOOT_MODE	0xD00D
#define CANON_PTP_DPC_IMAGE_MODE	0xD00E
#define CANON_PTP_DPC_DRIVE_MODE	0xD00F
#define CANON_PTP_DPC_EZ_ZOOM		0xD010
#define CANON_PTP_DPC_ML_SPOT_POS	0xD011
#define CANON_PTP_DPC_DISP_AV		0xD012
#define CANON_PTP_DPC_AV_OPEN_APEX	0xD013
#define CANON_PTP_DPC_DZ_MAG		0xD014
#define CANON_PTP_DPC_ML_SPOT_POS_X	0xD015
#define CANON_PTP_DPC_ML_SPOT_POS_Y	0xD016
#define CANON_PTP_DPC_DISP_AV_MAX	0xD017
#define CANON_PTP_DPC_AV_MAX_APEX	0xD018
#define CANON_PTP_DPC_EZ_ZOOM_POS	0xD019
#define CANON_PTP_DPC_FOCAL_LENGTH	0xD01A
#define CANON_PTP_DPC_FOCAL_LENGTH_TELE	0xD01B
#define CANON_PTP_DPC_FOCAL_LENGTH_WIDE	0xD01C
#define CANON_PTP_DPC_FOCAL_LENGTH_DENOMINATOR 0xD01D
#define CANON_PTP_DPC_CAPTURE_TRANSFER_MODE 0xD01E

/* PTP container structure */
struct ptp_container {
	u32	length;
	u16	type;
	u16	code;
	u32	trans_id;
	u32	params[5];
} __packed;

/* PTP device info structure */
struct ptp_device_info {
	u16	standard_version;
	u32	vendor_extension_id;
	u16	vendor_extension_version;
	char	*vendor_extension_desc;
	u16	functional_mode;
	u32	*operations_supported;
	u32	*events_supported;
	u32	*device_properties_supported;
	u32	*capture_formats;
	u32	*image_formats;
	char	*manufacturer;
	char	*model;
	char	*device_version;
	char	*serial_number;
} __packed;

/* Live view frame header */
struct canon_liveview_header {
	u32	length;
	u32	frame_type;
	u32	width;
	u32	height;
	u32	data_offset;
	u32	timestamp;
	u8	reserved[8];
} __packed;

/* Function prototypes */
struct canon_r5_device;

/* PTP core functions */
int canon_r5_ptp_init(struct canon_r5_device *dev);
void canon_r5_ptp_cleanup(struct canon_r5_device *dev);
int canon_r5_ptp_open_session(struct canon_r5_device *dev);
int canon_r5_ptp_close_session(struct canon_r5_device *dev);

/* PTP command functions */
int canon_r5_ptp_command(struct canon_r5_device *dev, u16 code, 
			 u32 *params, int param_count,
			 void *data, int data_len,
			 u16 *response_code);

int canon_r5_ptp_get_device_info(struct canon_r5_device *dev,
				struct ptp_device_info *info);

/* Canon specific PTP functions */
int canon_r5_ptp_initiate_release_control(struct canon_r5_device *dev);
int canon_r5_ptp_terminate_release_control(struct canon_r5_device *dev);

/* Live view functions */
int canon_r5_ptp_liveview_start(struct canon_r5_device *dev);
int canon_r5_ptp_liveview_stop(struct canon_r5_device *dev);
int canon_r5_ptp_get_liveview_frame(struct canon_r5_device *dev,
				   void **frame_data, size_t *frame_size);

/* Capture functions */
int canon_r5_ptp_capture_image(struct canon_r5_device *dev);
int canon_r5_ptp_start_movie(struct canon_r5_device *dev);
int canon_r5_ptp_stop_movie(struct canon_r5_device *dev);

/* Property functions */
int canon_r5_ptp_get_property(struct canon_r5_device *dev, u16 property,
			     void *value, size_t value_size);
int canon_r5_ptp_set_property(struct canon_r5_device *dev, u16 property,
			     void *value, size_t value_size);

/* Event handling */
int canon_r5_ptp_check_event(struct canon_r5_device *dev);
void canon_r5_ptp_event_handler(struct work_struct *work);

/* Still image capture PTP functions */
int canon_r5_ptp_capture_single(struct canon_r5_device *dev);
int canon_r5_ptp_capture_burst(struct canon_r5_device *dev, u16 count);
int canon_r5_ptp_autofocus(struct canon_r5_device *dev);
int canon_r5_ptp_manual_focus(struct canon_r5_device *dev, u32 position);
int canon_r5_ptp_get_focus_info(struct canon_r5_device *dev, u32 *position, bool *achieved);
int canon_r5_ptp_set_image_quality(struct canon_r5_device *dev, u32 format, u32 size, u32 quality);
int canon_r5_ptp_get_image_quality(struct canon_r5_device *dev, u32 *format, u32 *size, u32 *quality);
int canon_r5_ptp_bulb_start(struct canon_r5_device *dev);
int canon_r5_ptp_bulb_end(struct canon_r5_device *dev);
int canon_r5_ptp_set_bracketing(struct canon_r5_device *dev, u8 shots, s8 step);
int canon_r5_ptp_get_battery_info(struct canon_r5_device *dev, u32 *level, u32 *status);
int canon_r5_ptp_get_captured_image(struct canon_r5_device *dev, u32 object_id, void **data, size_t *size);

#endif /* __CANON_R5_PTP_H__ */