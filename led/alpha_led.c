// SPDX-License-Identifier: GPL-2.0
/* ALIENTEK ALPHA GPIO LED, using logical GPIO values (1 = on). */
#include <linux/gpio/consumer.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

struct alpha_led {
	struct gpio_desc *gpio;
	struct led_classdev cdev;
};

static int alpha_led_set(struct led_classdev *cdev,
			 enum led_brightness brightness)
{
	struct alpha_led *led = container_of(cdev, struct alpha_led, cdev);

	/* GPIO_ACTIVE_LOW in DT turns logical 1 into physical low. */
	gpiod_set_value_cansleep(led->gpio, brightness != LED_OFF);
	return 0;
}

static void alpha_led_off(void *data)
{
	struct alpha_led *led = data;

	gpiod_set_value_cansleep(led->gpio, 0);
}

static int alpha_led_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct alpha_led *led;
	int ret;

	led = devm_kzalloc(dev, sizeof(*led), GFP_KERNEL);
	if (!led)
		return -ENOMEM;

	/* Start off; active-low GPIO is driven high. */
	led->gpio = devm_gpiod_get(dev, "led", GPIOD_OUT_LOW);
	if (IS_ERR(led->gpio))
		return dev_err_probe(dev, PTR_ERR(led->gpio),
				     "cannot acquire LED GPIO\n");

	/* Registered before the classdev: turn off after classdev teardown. */
	ret = devm_add_action_or_reset(dev, alpha_led_off, led);
	if (ret)
		return ret;

	led->cdev.name = "alpha:green:status";
	led->cdev.max_brightness = 1;
	led->cdev.brightness = LED_OFF;
	led->cdev.brightness_set_blocking = alpha_led_set;
	led->cdev.flags = LED_CORE_SUSPENDRESUME;
	platform_set_drvdata(pdev, led);

	return devm_led_classdev_register(dev, &led->cdev);
}

static void alpha_led_shutdown(struct platform_device *pdev)
{
	struct alpha_led *led = platform_get_drvdata(pdev);

	/* Unregister stops triggers, timers and pending brightness work. */
	devm_led_classdev_unregister(&pdev->dev, &led->cdev);
	alpha_led_off(led);
}

static const struct of_device_id alpha_led_match[] = {
	{ .compatible = "alientek,alpha-led" },
	{ }
};
MODULE_DEVICE_TABLE(of, alpha_led_match);

static struct platform_driver alpha_led_driver = {
	.probe = alpha_led_probe,
	.shutdown = alpha_led_shutdown,
	.driver = {
		.name = "alpha-led",
		.of_match_table = alpha_led_match,
	},
};
module_platform_driver(alpha_led_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Woody");
MODULE_DESCRIPTION("ALIENTEK ALPHA GPIO1_IO03 active-low LED");
