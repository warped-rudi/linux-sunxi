/*
 * Copyright (C) 2007-2012 Allwinner Technology Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <sound/pcm.h>
#include <linux/fb.h>
#include <linux/crc32.h>
#include "hdmi_core.h"
#include "../disp/dev_disp.h"
#include "../disp/disp_hdmi.h"
#include "../disp/sunxi_disp_regs.h"
#include "hdmi_cec.h"

#define EDID_BLOCK_LENGTH	0x80
#define EDID_MAX_BLOCKS		2

#define DDC_ADDR 		0x50
#define DDC_RETRIES 		3

/*
 * ParseEDID()
 * Check EDID check sum and EDID 1.3 extended segment.
 */
static __s32
EDID_CheckSum(__u8 block, __u8 *buf)
{
	__s32 i = 0, CheckSum = 0;
	__u8 *pbuf = buf + 128 * block;

	for (i = 0, CheckSum = 0; i < 128; i++) {
		CheckSum += pbuf[i];
		CheckSum &= 0xFF;
	}

	return CheckSum;
}

static __s32
EDID_Header_Check(__u8 *pbuf)
{
	if (pbuf[0] != 0x00 || pbuf[1] != 0xFF || pbuf[2] != 0xFF ||
	    pbuf[3] != 0xFF || pbuf[4] != 0xFF || pbuf[5] != 0xFF ||
	    pbuf[6] != 0xFF || pbuf[7] != 0x00) {
		pr_err("EDID block0 header error\n");
		return -1;
	}
	return 0;
}

static __s32
EDID_Version_Check(__u8 *pbuf)
{
	pr_info("EDID version: %d.%d\n", pbuf[0x12], pbuf[0x13]);
	if (pbuf[0x12] != 0x01) {
		pr_err("Unsupport EDID format,EDID parsing exit\n");
		return -1;
	}
	if (pbuf[0x13] < 3 && !(pbuf[0x18] & 0x02)) {
		pr_err("EDID revision < 3 and preferred timing feature bit "
			"not set, ignoring EDID info\n");
		return -1;
	}
	return 0;
}

static __s32
Parse_AudioData_Block(__u8 *pbuf, __u8 size)
{
	unsigned long rates = 0;

	while (size >= 3) {
		if ((pbuf[0] & 0xf8) == 0x08) {
			int c = (pbuf[0] & 0x7) + 1;
			pr_info("Parse_AudioData_Block: max channel=%d\n", c);
			pr_info("Parse_AudioData_Block: SampleRate code=%x\n",
			      pbuf[1]);
			pr_info("Parse_AudioData_Block: WordLen code=%x\n",
			      pbuf[2]);
			/*
			 * If >= 2 channels and 16 bit is supported, then
			 * add the supported rates to our bitmap.
			 */
			if ((c >= 2) && (pbuf[2] & 0x01)) {
				if (pbuf[1] & 0x01)
					rates |= SNDRV_PCM_RATE_32000;
				if (pbuf[1] & 0x02)
					rates |= SNDRV_PCM_RATE_44100;
				if (pbuf[1] & 0x04)
					rates |= SNDRV_PCM_RATE_48000;
				if (pbuf[1] & 0x08)
					rates |= SNDRV_PCM_RATE_88200;
				if (pbuf[1] & 0x10)
					rates |= SNDRV_PCM_RATE_96000;
				if (pbuf[1] & 0x20)
					rates |= SNDRV_PCM_RATE_176400;
				if (pbuf[1] & 0x40)
					rates |= SNDRV_PCM_RATE_192000;
			}
		}
		pbuf += 3;
		size -= 3;
	}
	audio_info.supported_rates |= rates;
	return 0;
}

static __s32
Parse_HDMI_VSDB(__u8 *pbuf, __u8 size)
{
	__u8 index = 8;

	/* check if it's HDMI VSDB */
	if ((pbuf[0] == 0x03) && (pbuf[1] == 0x0c) && (pbuf[2] == 0x00))
		pr_info("Found HDMI Vendor Specific DataBlock\n");
	else
		return 0;

	cec_phy_addr = (((__u32)pbuf[3]) << 8) | pbuf[4];
	__inf("my phy addr is %x\n", cec_phy_addr);
	if (size <= 8)
		return 0;

	if ((pbuf[7] & 0x20) == 0)
		return 0;
	if ((pbuf[7] & 0x40) == 1)
		index = index + 2;
	if ((pbuf[7] & 0x80) == 1)
		index = index + 2;

	/* mandatary format support */
	if (pbuf[index] & 0x80)	{
		Device_Support_VIC[HDMI1080P_24_3D_FP] = 1;
		Device_Support_VIC[HDMI720P_50_3D_FP] = 1;
		Device_Support_VIC[HDMI720P_60_3D_FP] = 1;
		pr_info("  3D_present\n");
	} else {
		return 0;
	}

	if (((pbuf[index] & 0x60) == 1) || ((pbuf[index] & 0x60) == 2))
		pr_info("  3D_multi_present\n");

	index += (pbuf[index + 1] & 0xe0) + 2;
	if (index > (size + 1))
		return 0;

	pr_info("  3D_multi_present byte(%2.2x,%2.2x)\n", pbuf[index],
	      pbuf[index + 1]);

	return 0;
}

static __s32 ParseEDID_CEA861_extension_block(__u8 *EDID_Block)
{
	__u32 offset;

	if (EDID_Block[3] & 0x40) {
		audio_info.supported_rates |=
			SNDRV_PCM_RATE_32000 |
			SNDRV_PCM_RATE_44100 |
			SNDRV_PCM_RATE_48000;
	}

	offset = EDID_Block[2];
	/* deal with reserved data block */
	if (offset > 4)	{
		__u8 bsum = 4;
		while (bsum < offset) {
			__u8 tag = EDID_Block[bsum] >> 5;
			__u8 len = EDID_Block[bsum] & 0x1f;

			if ((len > 0) && ((bsum + len + 1) > offset)) {
				pr_err("%s: len or bsum size error\n", __func__);
				return 0;
			} 

			if (tag == 1) { /* ADB */
				Parse_AudioData_Block(EDID_Block + bsum + 1, len);
			} else if (tag == 3) { /* vendor specific */
				Parse_HDMI_VSDB(EDID_Block + bsum + 1, len);
			}

			bsum += (len + 1);
		}
	}

	return 1;
}

static int probe_ddc_edid(struct i2c_adapter *adapter,
		int block, unsigned char *buf)
{
	unsigned char start = block * EDID_BLOCK_LENGTH;
	struct i2c_msg msgs[] = {
		{
			.addr	= DDC_ADDR,
			.flags	= 0,
			.len	= 1,
			.buf	= &start,
		}, {
			.addr	= DDC_ADDR,
			.flags	= I2C_M_RD,
			.len	= EDID_BLOCK_LENGTH,
			.buf	= buf + start,
		}
	};

	/* !!! only 1 byte I2C address length used */
	if (block >= 2)
		return -EIO;

	if (i2c_transfer(adapter, msgs, 2) == 2)
		return 0;

	return -EIO;
}

static int get_edid_block(int block, unsigned char *buf)
{
	int i;

	for (i = 1; i <= DDC_RETRIES; i++) {
		if (probe_ddc_edid(&sunxi_hdmi_i2c_adapter, block, buf)) {
			dev_warn(&sunxi_hdmi_i2c_adapter.dev,
				 "unable to read EDID block %d, try %d/%d\n",
				 block, i, DDC_RETRIES);
			continue;
		}
		if (EDID_CheckSum(block, buf) != 0) {
			dev_warn(&sunxi_hdmi_i2c_adapter.dev,
				 "EDID block %d checksum error, try %d/%d\n",
				 block, i, DDC_RETRIES);
			continue;
		}
		break;
	}
	return (i <= DDC_RETRIES) ? 0 : -EIO;
}

/*
 * collect the EDID ucdata of segment 0
 */
__s32 ParseEDID(void)
{
	__u8 BlockCount;
	__u32 i, crc_val;
	static __u32 EDID_crc = 0;
	const struct fb_videomode *dfltMode;
	unsigned char *EDID_Buf = kmalloc(EDID_BLOCK_LENGTH * EDID_MAX_BLOCKS, GFP_KERNEL);

	if (!EDID_Buf)
		return -ENOMEM;

	__inf("ParseEDID\n");

	if (get_edid_block(0, EDID_Buf) != 0)
		goto ret;

	if (EDID_Header_Check(EDID_Buf) != 0)
		goto ret;

	if (EDID_Version_Check(EDID_Buf) != 0)
		goto ret;

	BlockCount = EDID_Buf[0x7E] + 1;
	if (BlockCount > EDID_MAX_BLOCKS)
		BlockCount = EDID_MAX_BLOCKS;

	for (i = 1; i < BlockCount; i++) {
		if (get_edid_block(i, EDID_Buf) != 0) {
			BlockCount = i;
			break;
		}
	}

	crc_val = crc32(-1U, EDID_Buf, EDID_BLOCK_LENGTH * BlockCount);

	if (crc_val != EDID_crc || !Device_Support_VIC[HDMI_EDID]) {
		EDID_crc = crc_val;

		pr_info("ParseEDID: New EDID received.\n");

		if (video_mode == HDMI_EDID) {
			/* HDMI_DEVICE_SUPPORT_VIC_SIZE - 1 so as to not overwrite
			  the currently in use timings with a new preferred mode! */
			memset(Device_Support_VIC, 0, HDMI_DEVICE_SUPPORT_VIC_SIZE - 1);
		} else {
			memset(Device_Support_VIC, 0, HDMI_DEVICE_SUPPORT_VIC_SIZE);
		}

		for (i = 1; i < BlockCount; i++) {
			if (EDID_Buf[EDID_BLOCK_LENGTH * i + 0] == 2)
				ParseEDID_CEA861_extension_block(EDID_Buf + EDID_BLOCK_LENGTH * i);
		}

		dfltMode = hdmi_edid_received(EDID_Buf, BlockCount, Device_Support_VIC);

		if (dfltMode) {
			videomode_to_video_timing(&video_timing[video_timing_edid], dfltMode);
			Device_Support_VIC[HDMI_EDID] = 1;
		}
	} else {
		pr_info("ParseEDID: EDID already known.\n");
	}

ret:
	kfree(EDID_Buf);
	return 0;
}
