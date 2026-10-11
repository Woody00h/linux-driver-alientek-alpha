// SPDX-License-Identifier: GPL-2.0
/* ALIENTEK ALPHA GPIO1_IO18: interrupt driven, active-low key. */
#include <linux/gpio/consumer.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

#define DEBOUNCE_MS 20

struct alpha_key {
	struct gpio_desc *gpio;
	struct input_dev *input;
	struct delayed_work work;
	spinlock_t lock;
	ktime_t deadline;
	int irq;
	int state;
};

/* Read the physical pin without requiring debugfs or GPIO re-export. */
static ssize_t raw_level_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct alpha_key *key = dev_get_drvdata(dev);
	int level = gpiod_get_raw_value(key->gpio);

	if (level < 0)
		return level;
	return sysfs_emit(buf, "%d\n", level);
}
static DEVICE_ATTR_RO(raw_level);

static struct attribute *alpha_key_attrs[] = {
	&dev_attr_raw_level.attr,
	NULL,
};
ATTRIBUTE_GROUPS(alpha_key);

static void alpha_key_work(struct work_struct *work)
{
	struct alpha_key *key = container_of(to_delayed_work(work),
					   struct alpha_key, work);
	unsigned long flags;
	int state;

	spin_lock_irqsave(&key->lock, flags);
	/* Check real elapsed time too: this kernel may use CONFIG_HZ=100. */
	if (ktime_before(ktime_get(), key->deadline)) {
		mod_delayed_work(system_percpu_wq, &key->work, 1);
		goto out;
	}
	/* On-chip i.MX GPIO is non-sleeping; IRQ and sampling are serialized. */
	state = gpiod_get_value(key->gpio);
	if (state >= 0 && state != key->state) {
		key->state = state;
		input_report_key(key->input, KEY_ENTER, state);
		input_sync(key->input);
	}
out:
	spin_unlock_irqrestore(&key->lock, flags);
}

static irqreturn_t alpha_key_irq(int irq, void *data)
{
	struct alpha_key *key = data;
	unsigned long flags;

	spin_lock_irqsave(&key->lock, flags);
	key->deadline = ktime_add_ms(ktime_get(), DEBOUNCE_MS);
	mod_delayed_work(system_percpu_wq, &key->work,
			 msecs_to_jiffies(DEBOUNCE_MS));
	spin_unlock_irqrestore(&key->lock, flags);
	return IRQ_HANDLED;
}

static int alpha_key_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct alpha_key *key;
	int ret;

	key = devm_kzalloc(dev, sizeof(*key), GFP_KERNEL);
	if (!key)
		return -ENOMEM;
	key->gpio = devm_gpiod_get(dev, "key", GPIOD_IN);
	if (IS_ERR(key->gpio))
		return dev_err_probe(dev, PTR_ERR(key->gpio), "get key GPIO\n");
	if (gpiod_cansleep(key->gpio))
		return dev_err_probe(dev, -EINVAL, "requires non-sleeping GPIO\n");
	key->irq = gpiod_to_irq(key->gpio);
	if (key->irq < 0)
		return dev_err_probe(dev, key->irq, "get key IRQ\n");
	key->input = devm_input_allocate_device(dev);
	if (!key->input)
		return -ENOMEM;
	key->input->name = "alpha-key";
	key->input->phys = "alpha-key/input0";
	key->input->id.bustype = BUS_HOST;
	input_set_capability(key->input, EV_KEY, KEY_ENTER);
	ret = input_register_device(key->input);
	if (ret)
		return dev_err_probe(dev, ret, "register input device\n");

	spin_lock_init(&key->lock);
	INIT_DELAYED_WORK(&key->work, alpha_key_work);
	platform_set_drvdata(pdev, key);
	/* Use explicit IRQ ownership so remove can stop IRQs before work. */
	ret = request_irq(key->irq, alpha_key_irq,
			  IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			  dev_name(dev), key);
	if (ret)
		return dev_err_probe(dev, ret, "request key IRQ\n");
	/* Also sample a key held during module loading, after debounce. */
	alpha_key_irq(key->irq, key);
	dev_info(dev, "KEY_ENTER, both-edge IRQ, debounce %d ms\n", DEBOUNCE_MS);
	return 0;
}

static void alpha_key_remove(struct platform_device *pdev)
{
	struct alpha_key *key = platform_get_drvdata(pdev);

	free_irq(key->irq, key);
	cancel_delayed_work_sync(&key->work);
}

static const struct of_device_id alpha_key_match[] = {
	{ .compatible = "alientek,alpha-key" },
	{ }
};
MODULE_DEVICE_TABLE(of, alpha_key_match);

static struct platform_driver alpha_key_driver = {
	.probe = alpha_key_probe,
	.remove = alpha_key_remove,
	.driver = {
		.name = "alpha-key",
		.of_match_table = alpha_key_match,
		.dev_groups = alpha_key_groups,
	},
};
module_platform_driver(alpha_key_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ALIENTEK ALPHA interrupt key with 20ms debounce");
