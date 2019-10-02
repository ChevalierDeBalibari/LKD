/*
 * Copyright (c) 2019 Maxim Integrated Products, Inc.
 * Author: Maxim Integrated <opensource@maximintegrated.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/version.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regmap.h>
#include <linux/mfd/max77818.h>
#include <linux/regulator/max77818-regulator.h>
#include <linux/module.h>
#include <linux/regulator/of_regulator.h>
#include <linux/of.h>

#define M2SH	__CONST_FFS

/* Register */
/* Safeout */
#define REG_SAFEOUTCTRL		0xC6
#define BIT_SAFEOUT1		BITS(1, 0)
#define BIT_SAFEOUT2		BITS(3, 2)
#define BIT_ACTDISSAFEO1	BIT(4)
#define BIT_ACTDISSAFEO2	BIT(5)
#define BIT_ENSAFEOUT1		BIT(6)
#define BIT_ENSAFEOUT2		BIT(7)

struct max77818_data {
	struct device *dev;
	struct max77818_dev *iodev;
	int num_regulators;
	struct regulator_dev **rdev;
};

static unsigned int max77818_safeout_volt_table[] = {
	4850000, 4900000, 4950000, 3300000,
};

static struct regulator_ops max77818_safeout_ops = {
	.list_voltage		= regulator_list_voltage_table,
	.map_voltage		= regulator_map_voltage_ascend,
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
};

#define REGULATOR_DESC_SFO(num, vsel_m, enable_m) {		\
	.name		= "SAFEOUT"#num,	\
	.id			= MAX77818_SAFEOUT##num,\
	.ops		= &max77818_safeout_ops,\
	.type		= REGULATOR_VOLTAGE,	\
	.owner		= THIS_MODULE,			\
	.n_voltages	= ARRAY_SIZE(max77818_safeout_volt_table),\
	.volt_table	= max77818_safeout_volt_table,\
	.vsel_reg	= REG_SAFEOUTCTRL,	\
	.vsel_mask	= (vsel_m),				\
	.enable_reg	= REG_SAFEOUTCTRL,	\
	.enable_mask	= (enable_m),		\
}

static struct regulator_desc max77818_safeout_desc[] = {
	REGULATOR_DESC_SFO(1, BIT_SAFEOUT1, BIT_ENSAFEOUT1),
	REGULATOR_DESC_SFO(2, BIT_SAFEOUT2, BIT_ENSAFEOUT2),
};

#ifdef CONFIG_OF
	static struct max77818_regulator_platform_data
*max77818_regulator_parse_dt(struct device *dev)
{
	struct device_node *np = of_find_node_by_name(NULL, "regulator");
	struct device_node *reg_np;
	struct max77818_regulator_platform_data *pdata;
	struct max77818_regulator_data *rdata;
	int i;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (unlikely(!pdata)) {
		pdata = ERR_PTR(-ENOMEM);
		goto out;
	}

	if (np == NULL) {
		pr_err("%s np NULL\n", __func__);
		pdata = ERR_PTR(-EINVAL);
		goto out;
	} else {
		pdata->num_regulators = of_get_child_count(np);
	}

	rdata = devm_kzalloc(dev, sizeof(*rdata) *
			pdata->num_regulators, GFP_KERNEL);

	if (!rdata) {
		of_node_put(np);
		dev_err(dev, "could not allocate memory\n");
		dev_err(dev, "for regulator data\n");
		pdata = ERR_PTR(-ENOMEM);
		goto out;
	}

	pdata->regulators = rdata;
	for_each_child_of_node(np, reg_np) {
		for (i = 0; i < ARRAY_SIZE(max77818_safeout_desc); i++)
			if (!of_node_cmp(reg_np->name,
				max77818_safeout_desc[i].name))
				break;

		if (i == ARRAY_SIZE(max77818_safeout_desc)) {
			dev_warn(dev, "don't know how to configure regulator %s\n",
					reg_np->name);
			continue;
		}

		rdata->id = i;
		rdata->initdata = of_get_regulator_init_data(dev, reg_np,
			max77818_safeout_desc);
		rdata->of_node = reg_np;
		rdata++;
	}
	of_node_put(np);

out:
	return pdata;
}
#endif

static int max77818_regulator_probe(struct platform_device *pdev)
{
	struct max77818_dev *iodev = dev_get_drvdata(pdev->dev.parent);
	struct max77818_regulator_platform_data *pdata =
		dev_get_platdata(&pdev->dev);
	struct regulator_dev **rdev;
	struct max77818_data *max77818;
	struct regmap *regmap;
	struct regulator_config config;
	int i, ret, size;

	pr_info("%s: Max77818 Regulator Driver Loading\n", __func__);
	dev_info(&pdev->dev, "%s\n", __func__);

#ifdef CONFIG_OF
	pdata = max77818_regulator_parse_dt(&pdev->dev);
#endif
	if (unlikely(IS_ERR(pdata))) {
		pr_info("[%s:%d] !pdata\n", __FILE__, __LINE__);
		dev_err(pdev->dev.parent, "No platform init data supplied.\n");
		return PTR_ERR(pdata);
	}

	max77818 = kzalloc(sizeof(struct max77818_data), GFP_KERNEL);
	if (!max77818)
		return -ENOMEM;

	size = sizeof(struct regulator_dev *) * pdata->num_regulators;
	max77818->rdev = kzalloc(size, GFP_KERNEL);
	if (!max77818->rdev) {
		pr_info("[%s:%d] if (!max77818->rdev)\n", __FILE__, __LINE__);
		kfree(max77818);
		return -ENOMEM;
	}

	rdev = max77818->rdev;
	max77818->dev = &pdev->dev;
	max77818->iodev = iodev;
	max77818->num_regulators = pdata->num_regulators;
	platform_set_drvdata(pdev, max77818);
	regmap = max77818->iodev->regmap_pmic;
	pr_info("[%s:%d] pdata->num_regulators:%d\n", __FILE__, __LINE__,
			pdata->num_regulators);
	for (i = 0; i < pdata->num_regulators; i++) {
		int id = pdata->regulators[i].id;

		pr_info("[%s:%d] for in pdata->regulator[%d].id :%d\n",
			__FILE__,
			__LINE__, i, id);
		pr_info("[%s:%d] for in pdata->num_regulators:%d\n",
			__FILE__,
			__LINE__, pdata->num_regulators);

		config.dev = &pdev->dev;
		config.driver_data = max77818;
		config.init_data = pdata->regulators[i].initdata;
		config.of_node = pdata->regulators[i].of_node;
		config.regmap = regmap;
		rdev[i] = regulator_register(&max77818_safeout_desc[id],
			&config);

		if (IS_ERR(rdev[i])) {
			ret = PTR_ERR(rdev[i]);
			dev_err(max77818->dev, "regulator init failed for %d\n",
					id);
			rdev[i] = NULL;
			goto err;
		}
	}

	return 0;
err:
	pr_info("[%s:%d] err:\n", __FILE__, __LINE__);
	for (i = 0; i < max77818->num_regulators; i++)
		if (rdev[i])
			regulator_unregister(rdev[i]);
	pr_info("[%s:%d] err_alloc\n", __FILE__, __LINE__);
	kfree(max77818->rdev);
	kfree(max77818);

	return ret;
}

static int max77818_regulator_remove(struct platform_device *pdev)
{
	struct max77818_data *max77818 = platform_get_drvdata(pdev);
	struct regulator_dev **rdev = max77818->rdev;
	int i;

	dev_info(&pdev->dev, "%s\n", __func__);
	for (i = 0; i < max77818->num_regulators; i++)
		if (rdev[i])
			regulator_unregister(rdev[i]);

	kfree(max77818->rdev);
	kfree(max77818);

	return 0;
}

static const struct platform_device_id max77818_regulator_id[] = {
	{"max77818-regulator", 0},
	{},
};

MODULE_DEVICE_TABLE(platform, max77818_pmic_id);

static struct platform_driver max77818_regulator_driver = {
	.driver = {
		.name = MAX77818_REGULATOR_NAME,
		.owner = THIS_MODULE,
	},
	.probe = max77818_regulator_probe,
	.remove = max77818_regulator_remove,
	.id_table = max77818_regulator_id,
};

static int __init max77818_regulator_init(void)
{
	return platform_driver_register(&max77818_regulator_driver);
}

subsys_initcall(max77818_regulator_init);

static void __exit max77818_regulator_exit(void)
{
	platform_driver_unregister(&max77818_regulator_driver);
}

module_exit(max77818_regulator_exit);

MODULE_AUTHOR("opensource@maximintegrated.com ");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.1");
MODULE_DESCRIPTION("MAXIM 77818 Regulator Driver");
