// SPDX-License-Identifier: GPL-2.0
/* ALIENTEK ALPHA V2.4: SAI2 + I2C1 WM8960, Linux 7.2 ASoC. */
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

/* WM8960 SYSCLK selector; defined by sound/soc/codecs/wm8960.h. */
#define ALPHA_WM8960_SYSCLK_MCLK 2
#define ALPHA_MCLK 12288000

struct alpha_audio {
	struct snd_soc_card card;
	struct snd_soc_dai_link link;
	struct snd_soc_dai_link_component cpu, codec, platform;
};

static int alpha_startup(struct snd_pcm_substream *s)
{
	int ret;

	ret = snd_pcm_hw_constraint_single(s->runtime,
					  SNDRV_PCM_HW_PARAM_RATE, 48000);
	if (ret)
		return ret;
	ret = snd_pcm_hw_constraint_single(s->runtime,
					  SNDRV_PCM_HW_PARAM_CHANNELS, 2);
	if (ret)
		return ret;
	return snd_pcm_hw_constraint_mask64(s->runtime,
		SNDRV_PCM_HW_PARAM_FORMAT, SNDRV_PCM_FMTBIT_S16_LE);
}

static int alpha_link_init(struct snd_soc_pcm_runtime *rtd)
{
	int ret;

	/* SAI supplies MCLK1; WM8960 supplies BCLK and the shared TX LRCLK. */
	ret = snd_soc_dai_set_sysclk(snd_soc_rtd_to_cpu(rtd, 0), 1,
				     ALPHA_MCLK, SND_SOC_CLOCK_OUT);
	if (ret)
		return ret;
	return snd_soc_dai_set_sysclk(snd_soc_rtd_to_codec(rtd, 0),
		ALPHA_WM8960_SYSCLK_MCLK, ALPHA_MCLK, SND_SOC_CLOCK_IN);
}

static const struct snd_soc_ops alpha_ops = {
	.startup = alpha_startup,
};

static const struct snd_soc_dapm_widget alpha_widgets[] = {
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_SPK("Speaker", NULL),
	SND_SOC_DAPM_MIC("Onboard Mic", NULL),
	SND_SOC_DAPM_LINE("Line In", NULL),
};

static const struct snd_kcontrol_new alpha_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone"),
	SOC_DAPM_PIN_SWITCH("Speaker"),
	SOC_DAPM_PIN_SWITCH("Onboard Mic"),
	SOC_DAPM_PIN_SWITCH("Line In"),
};

static void alpha_put_node(void *node)
{
	of_node_put(node);
}

static int alpha_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct alpha_audio *a;
	int ret;

	a = devm_kzalloc(dev, sizeof(*a), GFP_KERNEL);
	if (!a)
		return -ENOMEM;
	a->cpu.of_node = of_parse_phandle(dev->of_node, "audio-cpu", 0);
	if (!a->cpu.of_node)
		return dev_err_probe(dev, -EINVAL, "missing audio-cpu\n");
	ret = devm_add_action_or_reset(dev, alpha_put_node, a->cpu.of_node);
	if (ret)
		return ret;
	a->codec.of_node = of_parse_phandle(dev->of_node, "audio-codec", 0);
	if (!a->codec.of_node)
		return dev_err_probe(dev, -EINVAL, "missing audio-codec\n");
	ret = devm_add_action_or_reset(dev, alpha_put_node, a->codec.of_node);
	if (ret)
		return ret;
	a->codec.dai_name = "wm8960-hifi";
	a->platform.of_node = a->cpu.of_node;
	a->link.name = "WM8960";
	a->link.stream_name = "WM8960 PCM";
	a->link.cpus = &a->cpu;
	a->link.num_cpus = 1;
	a->link.codecs = &a->codec;
	a->link.num_codecs = 1;
	a->link.platforms = &a->platform;
	a->link.num_platforms = 1;
	a->link.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			  SND_SOC_DAIFMT_CBP_CFP;
	a->link.init = alpha_link_init;
	a->link.ops = &alpha_ops;
	a->link.symmetric_rate = 1;
	a->link.symmetric_channels = 1;
	a->link.symmetric_sample_bits = 1;
	a->card.dev = dev;
	a->card.owner = THIS_MODULE;
	a->card.dai_link = &a->link;
	a->card.num_links = 1;
	a->card.dapm_widgets = alpha_widgets;
	a->card.num_dapm_widgets = ARRAY_SIZE(alpha_widgets);
	a->card.controls = alpha_controls;
	a->card.num_controls = ARRAY_SIZE(alpha_controls);
	a->card.fully_routed = true;
	ret = snd_soc_of_parse_card_name(&a->card, "model");
	if (ret)
		return ret;
	ret = snd_soc_of_parse_audio_routing(&a->card, "audio-routing");
	if (ret)
		return ret;
	return devm_snd_soc_register_card(dev, &a->card);
}

static const struct of_device_id alpha_of_match[] = {
	{ .compatible = "alientek,alpha-wm8960" },
	{ }
};
MODULE_DEVICE_TABLE(of, alpha_of_match);

static struct platform_driver alpha_driver = {
	.probe = alpha_probe,
	.driver = {
		.name = "alpha-audio",
		.of_match_table = alpha_of_match,
	},
};
module_platform_driver(alpha_driver);
MODULE_DESCRIPTION("ALIENTEK ALPHA WM8960 ASoC machine driver");
MODULE_AUTHOR("ALPHA driver project");
MODULE_LICENSE("GPL");
