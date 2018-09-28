/*
 * Junction temperature thermal driver for MAX77714.
 *
 * Copyright (C) 2018 Maxim Integrated. All rights reserved.
 *
 * Author:
 *	Daniel Jeong <daniel.jeong@maximintegrated.com>
 *	Maxim LDD <opensource@maximintegrated.com>
 *
 * based on MAX77620 Driver
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/mfd/max77714.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/thermal.h>

#define MAX77714_NORMAL_OPERATING_TEMP	100000
#define MAX77714_TJALARM1_TEMP		120000
#define MAX77714_TJALARM2_TEMP		140000

struct max77714_therm_info {
	struct device			*dev;
	struct regmap			*rmap;
	struct thermal_zone_device	*tz_device;
	int				irq_tjalarm1;
	int				irq_tjalarm2;
};

/**
 * max77714_thermal_read_temp: Read PMIC die temperatue.
 * @data:	Device specific data.
 * temp:	Temperature in millidegrees Celsius
 *
 * The actual temperature of PMIC die is not available from PMIC.
 * PMIC only tells the status if it has crossed or not the threshold level
 * of 120degC or 140degC.
 * If threshold has not been crossed then assume die temperature as 100degC
 * else 120degC or 140deG based on the PMIC die temp threshold status.
 *
 * Return 0 on success otherwise error number to show reason of failure.
 */

static int max77714_thermal_read_temp(void *data, int *temp)
{
	struct max77714_therm_info *mtherm = data;
	unsigned int val;
	int ret;

	ret = regmap_read(mtherm->rmap, MAX77714_REG_STAT_MBATTRST_TEMP, &val);
	if (ret < 0) {
		dev_err(mtherm->dev, "Failed to read STATLBT: %d\n", ret);
		return ret;
	}

	if (val & MAX77714_MASK_TJALRM2)
		*temp = MAX77714_TJALARM2_TEMP;
	else if (val & MAX77714_MASK_TJALRM1)
		*temp = MAX77714_TJALARM1_TEMP;
	else
		*temp = MAX77714_NORMAL_OPERATING_TEMP;

	return 0;
}

static const struct thermal_zone_of_device_ops max77714_thermal_ops = {
	.get_temp = max77714_thermal_read_temp,
};

static irqreturn_t max77714_thermal_irq(int irq, void *data)
{
	struct max77714_therm_info *mtherm = data;

	if (irq == mtherm->irq_tjalarm1)
		dev_warn(mtherm->dev, "Junction Temp Alarm1(120C) occurred\n");
	else if (irq == mtherm->irq_tjalarm2)
		dev_crit(mtherm->dev, "Junction Temp Alarm2(140C) occurred\n");

	thermal_zone_device_update(mtherm->tz_device,
				   THERMAL_EVENT_UNSPECIFIED);

	return IRQ_HANDLED;
}

static int max77714_thermal_probe(struct platform_device *pdev)
{
	struct max77714_therm_info *mtherm;
	int ret;

	mtherm = devm_kzalloc(&pdev->dev, sizeof(*mtherm), GFP_KERNEL);
	if (!mtherm)
		return -ENOMEM;

	mtherm->irq_tjalarm1 = platform_get_irq(pdev, 0);
	mtherm->irq_tjalarm2 = platform_get_irq(pdev, 1);
	if ((mtherm->irq_tjalarm1 < 0) || (mtherm->irq_tjalarm2 < 0)) {
		dev_err(&pdev->dev, "Alarm irq number not available\n");
		return -EINVAL;
	}

	pdev->dev.of_node = pdev->dev.parent->of_node;

	mtherm->dev = &pdev->dev;
	mtherm->rmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!mtherm->rmap) {
		dev_err(&pdev->dev, "Failed to get parent regmap\n");
		return -ENODEV;
	}

	mtherm->tz_device = devm_thermal_zone_of_sensor_register(&pdev->dev, 0,
				mtherm, &max77714_thermal_ops);
	if (IS_ERR(mtherm->tz_device)) {
		ret = PTR_ERR(mtherm->tz_device);
		dev_err(&pdev->dev, "Failed to register thermal zone: %d\n",
			ret);
		return ret;
	}

	ret = devm_request_threaded_irq(&pdev->dev, mtherm->irq_tjalarm1, NULL,
					max77714_thermal_irq,
					IRQF_ONESHOT | IRQF_SHARED,
					dev_name(&pdev->dev), mtherm);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to request irq1: %d\n", ret);
		return ret;
	}

	ret = devm_request_threaded_irq(&pdev->dev, mtherm->irq_tjalarm2, NULL,
					max77714_thermal_irq,
					IRQF_ONESHOT | IRQF_SHARED,
					dev_name(&pdev->dev), mtherm);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to request irq2: %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, mtherm);

	return 0;
}

static struct platform_device_id max77714_thermal_devtype[] = {
	{ .name = "max77714-thermal", },
	{},
};

static struct platform_driver max77714_thermal_driver = {
	.driver = {
		.name = "max77714-thermal",
	},
	.probe = max77714_thermal_probe,
	.id_table = max77714_thermal_devtype,
};

module_platform_driver(max77714_thermal_driver);

MODULE_DESCRIPTION("Max77714 Junction temperature Thermal driver");
MODULE_AUTHOR("Laxman Dewangan <ldewangan@nvidia.com>");
MODULE_AUTHOR("Mallikarjun Kasoju <mkasoju@nvidia.com>");
MODULE_LICENSE("GPL v2");