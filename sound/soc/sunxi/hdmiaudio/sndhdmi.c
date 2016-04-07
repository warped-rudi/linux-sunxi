/*
 * sound\soc\sunxi\hdmiaudio\sndhdmi.c
 * (C) Copyright 2007-2011
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * chenpailin <chenpailin@allwinnertech.com>
 *
 * some simple description for this code
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/asoundef.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <plat/sys_config.h>
#include <linux/io.h>
#include <linux/drv_hdmi.h>

static __audio_hdmi_func	g_hdmi_func;
static hdmi_audio_t		hdmi_para;

/* The struct is just for registering the hdmiaudio codec node */
struct sndhdmi_priv {
	int sysclk;
	int dai_fmt;

	struct snd_pcm_substream *master_substream;
	struct snd_pcm_substream *slave_substream;
};

void audio_set_hdmi_func(__audio_hdmi_func *hdmi_func)
{
	g_hdmi_func.hdmi_audio_enable = hdmi_func->hdmi_audio_enable;
	g_hdmi_func.hdmi_set_audio_para = hdmi_func->hdmi_set_audio_para;
}
EXPORT_SYMBOL(audio_set_hdmi_func);

#define SNDHDMI_RATES		(\
	SNDRV_PCM_RATE_32000	|\
	SNDRV_PCM_RATE_44100	|\
	SNDRV_PCM_RATE_48000	|\
	SNDRV_PCM_RATE_96000	|\
	SNDRV_PCM_RATE_88200	|\
	SNDRV_PCM_RATE_96000	|\
	SNDRV_PCM_RATE_176400	|\
	SNDRV_PCM_RATE_192000	)

#define SNDHDMI_FORMATS		(\
	SNDRV_PCM_FMTBIT_S16_LE	|\
	SNDRV_PCM_FMTBIT_S32_LE	)

static int sndhdmi_mute(struct snd_soc_dai *dai, int mute)
{
	return 0;
}

static int sndhdmi_startup(struct snd_pcm_substream *substream,
						struct snd_soc_dai *dai)
{
	return 0;
}

static void sndhdmi_shutdown(struct snd_pcm_substream *substream,
						struct snd_soc_dai *dai)
{

}

static int sndhdmi_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	if ((!substream) || (!params)) {
		printk("error:%s,line:%d\n", __func__, __LINE__);
		return -EAGAIN;
	}

	hdmi_para.ch_stat[0] = IEC958_AES0_CON_NOT_COPYRIGHT;
	hdmi_para.ch_stat[1] = IEC958_AES1_CON_PCM_CODER | IEC958_AES1_CON_ORIGINAL;
	hdmi_para.ch_stat[2] = IEC958_AES2_CON_SOURCE_UNSPEC | IEC958_AES2_CON_CHANNEL_UNSPEC;
	hdmi_para.ch_stat[3] = IEC958_AES3_CON_CLOCK_1000PPM;
	hdmi_para.ch_stat[4] = IEC958_AES4_CON_MAX_WORDLEN_24 | IEC958_AES4_CON_WORDLEN_24_20;
	hdmi_para.ch_stat[5] = IEC958_AES5_CON_CGMSA_COPYFREELY;

	hdmi_para.sample_rate = params_rate(params);
	switch(hdmi_para.sample_rate) {
	case  32000:  hdmi_para.ch_stat[3] |= 0x03;  hdmi_para.ch_stat[4] |= 0xc0;  break;
	case  44100:  hdmi_para.ch_stat[3] |= 0x00;  hdmi_para.ch_stat[4] |= 0xf0;  break;
	case  48000:  hdmi_para.ch_stat[3] |= 0x02;  hdmi_para.ch_stat[4] |= 0xd0;  break;
	case  88200:  hdmi_para.ch_stat[3] |= 0x08;  hdmi_para.ch_stat[4] |= 0x70;  break;
	case  96000:  hdmi_para.ch_stat[3] |= 0x0a;  hdmi_para.ch_stat[4] |= 0x50;  break;
	case 176400:  hdmi_para.ch_stat[3] |= 0x0c;  hdmi_para.ch_stat[4] |= 0x30;  break;
	case 192000:  hdmi_para.ch_stat[3] |= 0x0e;  hdmi_para.ch_stat[4] |= 0x10;  break;
	    default:  return -EINVAL;
	}

	hdmi_para.channel_num = params_channels(params);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		hdmi_para.sample_bit = 16;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		hdmi_para.sample_bit = 32;
		break;
	default:
		return -EINVAL;
	}

	if (4 == hdmi_para.channel_num)
		hdmi_para.channel_num = 2;

	g_hdmi_func.hdmi_set_audio_para(&hdmi_para);
	g_hdmi_func.hdmi_audio_enable(1, 1);

	return 0;
}

static int sndhdmi_set_dai_sysclk(struct snd_soc_dai *codec_dai, int clk_id,
						unsigned int freq, int dir)
{
	return 0;
}

static int sndhdmi_set_dai_clkdiv(struct snd_soc_dai *codec_dai, int div_id,
									int div)
{

	hdmi_para.fs_between = div;

	return 0;
}


static int sndhdmi_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	return 0;
}


/* IEC60958 status functions */
static int sndhdmi_iec_info(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;

	return 0;
}

static int sndhdmi_iec_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *uvalue)
{
	memcpy(uvalue->value.iec958.status,
	       hdmi_para.ch_stat, sizeof(hdmi_para.ch_stat));

	return 0;
}

static int sndhdmi_iec_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *uvalue)
{
	/* Do not allow professional mode */
	if (uvalue->value.iec958.status[0] & IEC958_AES0_PROFESSIONAL)
		return -EPERM;

	memcpy(hdmi_para.ch_stat,
	       uvalue->value.iec958.status, sizeof(hdmi_para.ch_stat));

	if (g_hdmi_func.hdmi_set_audio_para)
		g_hdmi_func.hdmi_set_audio_para(&hdmi_para);

	return 0;
}

static int sndhdmi_probe(struct snd_soc_dai *dai)
{
	int ret;

	static struct snd_kcontrol_new sndhdmi_ctrls[] = {
		/* Status channel control */
		{
			.iface = SNDRV_CTL_ELEM_IFACE_PCM,
			.name = SNDRV_CTL_NAME_IEC958("", PLAYBACK, DEFAULT),
			.access = SNDRV_CTL_ELEM_ACCESS_READ |
				SNDRV_CTL_ELEM_ACCESS_WRITE |
				SNDRV_CTL_ELEM_ACCESS_VOLATILE,
			.info = sndhdmi_iec_info,
			.get = sndhdmi_iec_get,
			.put = sndhdmi_iec_put,
		},
	};

	ret = snd_soc_add_dai_controls(dai, sndhdmi_ctrls,
			ARRAY_SIZE(sndhdmi_ctrls));
	if (ret)
		dev_warn(dai->dev, "failed to add dai controls\n");

	return 0;
}

/* codec dai operation */
struct snd_soc_dai_ops sndhdmi_dai_ops = {
	.startup = sndhdmi_startup,
	.shutdown = sndhdmi_shutdown,
	.hw_params = sndhdmi_hw_params,
	.digital_mute = sndhdmi_mute,
	.set_sysclk = sndhdmi_set_dai_sysclk,
	.set_clkdiv = sndhdmi_set_dai_clkdiv,
	.set_fmt = sndhdmi_set_dai_fmt,
};

/* codec dai */
struct snd_soc_dai_driver sndhdmi_dai = {
	.name = "sndhdmi",
	/* playback capabilities */
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDHDMI_RATES,
		.formats = SNDHDMI_FORMATS,
	},
	/* pcm operations */
	.ops = &sndhdmi_dai_ops,
	.symmetric_rates = 1,
	.probe = &sndhdmi_probe,

};
EXPORT_SYMBOL(sndhdmi_dai);

static int sndhdmi_soc_probe(struct snd_soc_codec *codec)
{
	struct sndhdmi_priv *sndhdmi;

	if (!codec) {
		printk("error:%s,line:%d\n", __func__, __LINE__);
		return -EAGAIN;
	}

	sndhdmi = kzalloc(sizeof(struct sndhdmi_priv), GFP_KERNEL);
	if (sndhdmi == NULL) {
		printk("error at:%s,%d\n", __func__, __LINE__);
		return -ENOMEM;
	}

	snd_soc_codec_set_drvdata(codec, sndhdmi);

	return 0;
}

static int sndhdmi_soc_remove(struct snd_soc_codec *codec)
{
	struct sndhdmi_priv *sndhdmi;

	if (!codec) {
		printk("error:%s,line:%d\n", __func__, __LINE__);
		return -EAGAIN;
	}
	sndhdmi = snd_soc_codec_get_drvdata(codec);

	kfree(sndhdmi);

	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_sndhdmi = {
	.probe		= sndhdmi_soc_probe,
	.remove		= sndhdmi_soc_remove,
};

static int __devinit sndhdmi_codec_probe(struct platform_device *pdev)
{
	if (!pdev) {
		printk("error:%s,line:%d\n", __func__, __LINE__);
		return -EAGAIN;
	}
	return snd_soc_register_codec(&pdev->dev, &soc_codec_dev_sndhdmi,
							&sndhdmi_dai, 1);
}

static int __exit sndhdmi_codec_remove(struct platform_device *pdev)
{
	if (!pdev) {
		printk("error:%s,line:%d\n", __func__, __LINE__);
		return -EAGAIN;
	}
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

static struct platform_driver sndhdmi_codec_driver = {
	.driver = {
		.name = "sunxi-hdmiaudio-codec",
		.owner = THIS_MODULE,
	},
	.probe = sndhdmi_codec_probe,
	.remove = __exit_p(sndhdmi_codec_remove),
};

static int __init sndhdmi_codec_init(void)
{
	int err = 0;

	err = platform_driver_register(&sndhdmi_codec_driver);
	if (err < 0)
		return err;

	return 0;
}
module_init(sndhdmi_codec_init);

static void __exit sndhdmi_codec_exit(void)
{
	platform_driver_unregister(&sndhdmi_codec_driver);
}
module_exit(sndhdmi_codec_exit);

MODULE_DESCRIPTION("SNDHDMI ALSA soc codec driver");
MODULE_AUTHOR("Zoltan Devai, Christian Pellegrin <chripell@evolware.org>");
MODULE_LICENSE("GPL");
