/*
 * s2mps11.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd
 *              http://www.samsung.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/bug.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/mfd/samsung/core.h>
#include <linux/mfd/samsung/s2mps11.h>

#define S2MPS11_CLOCK_OPMODE_MASK 	0x1
#define S2MPS11_CLOCK_EN32KHZ_AP_SHIFT	0x0
#define S2MPS11_CLOCK_EN32KHZ_CP_SHIFT	0x1
#define S2MPS11_CLOCK_EN32KHZ_BT_SHIFT	0x2

struct s2mps11_info {
	struct regulator_dev *rdev[S2MPS11_REGULATOR_MAX];

	int ramp_delay2;
	int ramp_delay34;
	int ramp_delay5;
	int ramp_delay16;
	int ramp_delay7810;
	int ramp_delay9;

	bool buck6_ramp;
	bool buck2_ramp;
	bool buck3_ramp;
	bool buck4_ramp;
};

static int get_ramp_delay(int ramp_delay)
{
	unsigned char cnt = 0;

	ramp_delay /= 6;

	while (true) {
		ramp_delay = ramp_delay >> 1;
		if (ramp_delay == 0)
			break;
		cnt++;
	}
	return cnt;
}

static struct regulator_ops s2mps11_ldo_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
};

static struct regulator_ops s2mps11_buck_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
};

static struct regulator_ops s2mps11_fixedvolt_ops = {
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
};

#define regulator_desc_ldo1(num)	{		\
	.name		= "LDO"#num,			\
	.id		= S2MPS11_LDO##num,		\
	.ops		= &s2mps11_ldo_ops,		\
	.type		= REGULATOR_VOLTAGE,		\
	.owner		= THIS_MODULE,			\
	.min_uV		= S2MPS11_LDO_MIN,		\
	.uV_step	= S2MPS11_LDO_STEP1,		\
	.n_voltages	= S2MPS11_LDO_N_VOLTAGES,	\
	.vsel_reg	= S2MPS11_REG_L1CTRL + num - 1,	\
	.vsel_mask	= S2MPS11_LDO_VSEL_MASK,	\
	.enable_reg	= S2MPS11_REG_L1CTRL + num - 1,	\
	.enable_mask	= S2MPS11_ENABLE_MASK		\
}
#define regulator_desc_ldo2(num)	{		\
	.name		= "LDO"#num,			\
	.id		= S2MPS11_LDO##num,		\
	.ops		= &s2mps11_ldo_ops,		\
	.type		= REGULATOR_VOLTAGE,		\
	.owner		= THIS_MODULE,			\
	.min_uV		= S2MPS11_LDO_MIN,		\
	.uV_step	= S2MPS11_LDO_STEP2,		\
	.n_voltages	= S2MPS11_LDO_N_VOLTAGES,	\
	.vsel_reg	= S2MPS11_REG_L1CTRL + num - 1,	\
	.vsel_mask	= S2MPS11_LDO_VSEL_MASK,	\
	.enable_reg	= S2MPS11_REG_L1CTRL + num - 1,	\
	.enable_mask	= S2MPS11_ENABLE_MASK		\
}

#define regulator_desc_buck1_4(num)	{			\
	.name		= "BUCK"#num,				\
	.id		= S2MPS11_BUCK##num,			\
	.ops		= &s2mps11_buck_ops,			\
	.type		= REGULATOR_VOLTAGE,			\
	.owner		= THIS_MODULE,				\
	.min_uV		= S2MPS11_BUCK_MIN1,			\
	.uV_step	= S2MPS11_BUCK_STEP1,			\
	.n_voltages	= S2MPS11_BUCK_N_VOLTAGES,		\
	.vsel_reg	= S2MPS11_REG_B1CTRL2 + (num - 1) * 2,	\
	.vsel_mask	= S2MPS11_BUCK_VSEL_MASK,		\
	.enable_reg	= S2MPS11_REG_B1CTRL1 + (num - 1) * 2,	\
	.enable_mask	= S2MPS11_ENABLE_MASK			\
}

#define regulator_desc_buck5	{				\
	.name		= "BUCK5",				\
	.id		= S2MPS11_BUCK5,			\
	.ops		= &s2mps11_buck_ops,			\
	.type		= REGULATOR_VOLTAGE,			\
	.owner		= THIS_MODULE,				\
	.min_uV		= S2MPS11_BUCK_MIN1,			\
	.uV_step	= S2MPS11_BUCK_STEP1,			\
	.n_voltages	= S2MPS11_BUCK_N_VOLTAGES,		\
	.vsel_reg	= S2MPS11_REG_B5CTRL2,			\
	.vsel_mask	= S2MPS11_BUCK_VSEL_MASK,		\
	.enable_reg	= S2MPS11_REG_B5CTRL1,			\
	.enable_mask	= S2MPS11_ENABLE_MASK			\
}

#define regulator_desc_buck6_8(num)	{			\
	.name		= "BUCK"#num,				\
	.id		= S2MPS11_BUCK##num,			\
	.ops		= &s2mps11_buck_ops,			\
	.type		= REGULATOR_VOLTAGE,			\
	.owner		= THIS_MODULE,				\
	.min_uV		= S2MPS11_BUCK_MIN1,			\
	.uV_step	= S2MPS11_BUCK_STEP1,			\
	.n_voltages	= S2MPS11_BUCK_N_VOLTAGES,		\
	.vsel_reg	= S2MPS11_REG_B6CTRL2 + (num - 6) * 2,	\
	.vsel_mask	= S2MPS11_BUCK_VSEL_MASK,		\
	.enable_reg	= S2MPS11_REG_B6CTRL1 + (num - 6) * 2,	\
	.enable_mask	= S2MPS11_ENABLE_MASK			\
}

#define regulator_desc_buck9	{				\
	.name		= "BUCK9",				\
	.id		= S2MPS11_BUCK9,			\
	.ops		= &s2mps11_buck_ops,			\
	.type		= REGULATOR_VOLTAGE,			\
	.owner		= THIS_MODULE,				\
	.min_uV		= S2MPS11_BUCK_MIN3,			\
	.uV_step	= S2MPS11_BUCK_STEP3,			\
	.n_voltages	= S2MPS11_BUCK_N_VOLTAGES,		\
	.vsel_reg	= S2MPS11_REG_B9CTRL2,			\
	.vsel_mask	= S2MPS11_BUCK_VSEL_MASK,		\
	.enable_reg	= S2MPS11_REG_B9CTRL1,			\
	.enable_mask	= S2MPS11_ENABLE_MASK			\
}

#define regulator_desc_buck10	{				\
	.name		= "BUCK10",				\
	.id		= S2MPS11_BUCK10,			\
	.ops		= &s2mps11_buck_ops,			\
	.type		= REGULATOR_VOLTAGE,			\
	.owner		= THIS_MODULE,				\
	.min_uV		= S2MPS11_BUCK_MIN2,			\
	.uV_step	= S2MPS11_BUCK_STEP2,			\
	.n_voltages	= S2MPS11_BUCK_N_VOLTAGES,		\
	.vsel_reg	= S2MPS11_REG_B10CTRL2,			\
	.vsel_mask	= S2MPS11_BUCK_VSEL_MASK,		\
	.enable_reg	= S2MPS11_REG_B10CTRL1,			\
	.enable_mask	= S2MPS11_ENABLE_MASK			\
}

static struct regulator_desc regulators[] = {
	regulator_desc_ldo2(1),
	regulator_desc_ldo1(2),
	regulator_desc_ldo1(3),
	regulator_desc_ldo1(4),
	regulator_desc_ldo1(5),
	regulator_desc_ldo2(6),
	regulator_desc_ldo1(7),
	regulator_desc_ldo1(8),
	regulator_desc_ldo1(9),
	regulator_desc_ldo1(10),
	regulator_desc_ldo2(11),
	regulator_desc_ldo1(12),
	regulator_desc_ldo1(13),
	regulator_desc_ldo1(14),
	regulator_desc_ldo1(15),
	regulator_desc_ldo1(16),
	regulator_desc_ldo1(17),
	regulator_desc_ldo1(18),
	regulator_desc_ldo1(19),
	regulator_desc_ldo1(20),
	regulator_desc_ldo1(21),
	regulator_desc_ldo2(22),
	regulator_desc_ldo2(23),
	regulator_desc_ldo1(24),
	regulator_desc_ldo1(25),
	regulator_desc_ldo1(26),
	regulator_desc_ldo2(27),
	regulator_desc_ldo1(28),
	regulator_desc_ldo1(29),
	regulator_desc_ldo1(30),
	regulator_desc_ldo1(31),
	regulator_desc_ldo1(32),
	regulator_desc_ldo1(33),
	regulator_desc_ldo1(34),
	regulator_desc_ldo1(35),
	regulator_desc_ldo1(36),
	regulator_desc_ldo1(37),
	regulator_desc_ldo1(38),
	regulator_desc_buck1_4(1),
	regulator_desc_buck1_4(2),
	regulator_desc_buck1_4(3),
	regulator_desc_buck1_4(4),
	regulator_desc_buck5,
	regulator_desc_buck6_8(6),
	regulator_desc_buck6_8(7),
	regulator_desc_buck6_8(8),
	regulator_desc_buck9,
	regulator_desc_buck10,
	{
		.name = "EN32KHZ_AP",
		.id = S2MPS11_AP_EN32KHZ,
		.ops = &s2mps11_fixedvolt_ops,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.enable_reg = S2MPS11_REG_RTC_CTRL,
		.enable_mask = S2MPS11_CLOCK_OPMODE_MASK	\
			       << S2MPS11_CLOCK_EN32KHZ_AP_SHIFT,
	}, {
		.name = "EN32KHZ_CP",
		.id = S2MPS11_CP_EN32KHZ,
		.ops = &s2mps11_fixedvolt_ops,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.enable_reg = S2MPS11_REG_RTC_CTRL,
		.enable_mask = S2MPS11_CLOCK_OPMODE_MASK	\
			       << S2MPS11_CLOCK_EN32KHZ_CP_SHIFT,
	}, {
		.name = "EN32KHZ_BT",
		.id = S2MPS11_BT_EN32KHZ,
		.ops = &s2mps11_fixedvolt_ops,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.enable_reg = S2MPS11_REG_RTC_CTRL,
		.enable_mask = S2MPS11_CLOCK_OPMODE_MASK	\
			       << S2MPS11_CLOCK_EN32KHZ_BT_SHIFT,
	},
};

#ifdef CONFIG_OF
static int s2mps11_pmic_dt_parse_pdata(struct sec_pmic_dev *iodev,
					struct sec_platform_data *pdata)
{
	struct device_node *pmic_np, *regulators_np, *reg_np;
	struct sec_regulator_data *rdata;
	unsigned int i;

	pmic_np = iodev->dev->of_node;
	if (!pmic_np) {
		dev_err(iodev->dev, "could not find pmic sub-node\n");
		return -ENODEV;
	}

	regulators_np = of_find_node_by_name(pmic_np, "regulators");
	if (!regulators_np) {
		dev_err(iodev->dev, "could not find regulators sub-node\n");
		return -EINVAL;
	}

	/* count the number of regulators to be supported in pmic */
	pdata->num_regulators = 0;
	for_each_child_of_node(regulators_np, reg_np) {
		pdata->num_regulators++;
	}

	rdata = devm_kzalloc(iodev->dev, sizeof(*rdata) *
				pdata->num_regulators, GFP_KERNEL);
	if (!rdata) {
		dev_err(iodev->dev,
			"could not allocate memory for regulator data\n");
		return -ENOMEM;
	}

	pdata->regulators = rdata;
	for_each_child_of_node(regulators_np, reg_np) {
		for (i = 0; i < ARRAY_SIZE(regulators); i++)
			if (!of_node_cmp(reg_np->name, regulators[i].name))
				break;

		if (i == ARRAY_SIZE(regulators)) {
			dev_warn(iodev->dev,
			"don't know how to configure regulator %s\n",
			reg_np->name);
			continue;
		}

		rdata->id = i;
		rdata->initdata = of_get_regulator_init_data(
						iodev->dev, reg_np);
		rdata->reg_node = reg_np;
		rdata++;
	}

	if (!of_property_read_u32(pmic_np, "s2mps11,buck2-ramp-delay", &i))
		pdata->buck2_ramp_delay = i;
	if (!of_property_read_u32(pmic_np, "s2mps11,buck34-ramp-delay", &i))
		pdata->buck34_ramp_delay = i;
	if (!of_property_read_u32(pmic_np, "s2mps11,buck5-ramp-delay", &i))
		pdata->buck5_ramp_delay = i;
	if (!of_property_read_u32(pmic_np, "s2mps11,buck16-ramp-delay", &i))
		pdata->buck16_ramp_delay = i;
	if (!of_property_read_u32(pmic_np, "s2mps11,buck7810-ramp-delay", &i))
		pdata->buck7810_ramp_delay = i;
	if (!of_property_read_u32(pmic_np, "s2mps11,buck9-ramp-delay", &i))
		pdata->buck9_ramp_delay = i;
	if (!of_property_read_u32(pmic_np, "s2mps11,buck6-ramp-enable", &i))
		pdata->buck6_ramp_enable = i;
	if (!of_property_read_u32(pmic_np, "s2mps11,buck2-ramp-enable", &i))
		pdata->buck2_ramp_enable = i;
	if (!of_property_read_u32(pmic_np, "s2mps11,buck3-ramp-enable", &i))
		pdata->buck3_ramp_enable = i;
	if (!of_property_read_u32(pmic_np, "s2mps11,buck4-ramp-enable", &i))
		pdata->buck4_ramp_enable = i;

	return 0;
}
#else
static int s5m8767_pmic_dt_parse_pdata(struct sec_pmic_dev *iodev,
					struct sec_platform_data *pdata)
{
	return 0;
}
#endif /* CONFIG_OF */

static int s2mps11_pmic_probe(struct platform_device *pdev)
{
	struct sec_pmic_dev *iodev = dev_get_drvdata(pdev->dev.parent);
	struct sec_platform_data *pdata = iodev->pdata;
	struct regulator_config config = { };
	struct s2mps11_info *s2mps11;
	int i, ret;
	unsigned char ramp_enable, ramp_reg = 0;

	if (iodev->dev->of_node) {
		ret = s2mps11_pmic_dt_parse_pdata(iodev, pdata);
		if (ret)
			return ret;
	}

	if (!pdata) {
		dev_err(pdev->dev.parent, "Platform data not supplied\n");
		return -ENODEV;
	}

	s2mps11 = devm_kzalloc(&pdev->dev, sizeof(struct s2mps11_info),
				GFP_KERNEL);
	if (!s2mps11)
		return -ENOMEM;

	platform_set_drvdata(pdev, s2mps11);

	s2mps11->ramp_delay2 = pdata->buck2_ramp_delay;
	s2mps11->ramp_delay34 = pdata->buck34_ramp_delay;
	s2mps11->ramp_delay5 = pdata->buck5_ramp_delay;
	s2mps11->ramp_delay16 = pdata->buck16_ramp_delay;
	s2mps11->ramp_delay7810 = pdata->buck7810_ramp_delay;
	s2mps11->ramp_delay9 = pdata->buck9_ramp_delay;

	s2mps11->buck6_ramp = pdata->buck6_ramp_enable;
	s2mps11->buck2_ramp = pdata->buck2_ramp_enable;
	s2mps11->buck3_ramp = pdata->buck3_ramp_enable;
	s2mps11->buck4_ramp = pdata->buck4_ramp_enable;

	ramp_enable = (s2mps11->buck2_ramp << 3) | (s2mps11->buck3_ramp << 2) |
		(s2mps11->buck4_ramp << 1) | s2mps11->buck6_ramp ;

	if (ramp_enable) {
		if (s2mps11->buck2_ramp)
			ramp_reg |= get_ramp_delay(s2mps11->ramp_delay2) << 6;
		if (s2mps11->buck3_ramp || s2mps11->buck4_ramp)
			ramp_reg |= get_ramp_delay(s2mps11->ramp_delay34) << 4;
		regmap_write(iodev->pmic, S2MPS11_REG_RAMP,
			     ramp_reg | ramp_enable);
	}

	ramp_reg &= 0x00;
	ramp_reg |= get_ramp_delay(s2mps11->ramp_delay5) << 6;
	ramp_reg |= get_ramp_delay(s2mps11->ramp_delay16) << 4;
	ramp_reg |= get_ramp_delay(s2mps11->ramp_delay7810) << 2;
	ramp_reg |= get_ramp_delay(s2mps11->ramp_delay9);
	regmap_write(iodev->pmic, S2MPS11_REG_RAMP_BUCK, ramp_reg);

	for (i = 0; i < pdata->num_regulators; i++) {

		int id = pdata->regulators[i].id;
		config.dev = &pdev->dev;
		config.regmap = iodev->pmic;
		config.init_data = pdata->regulators[i].initdata;
		config.driver_data = s2mps11;
		config.of_node = pdata->regulators[i].reg_node;

		s2mps11->rdev[i] = regulator_register(&regulators[id], &config);
		if (IS_ERR(s2mps11->rdev[i])) {
			ret = PTR_ERR(s2mps11->rdev[i]);
			dev_err(&pdev->dev, "regulator init failed for %d\n",
				i);
			s2mps11->rdev[i] = NULL;
			goto err;
		}
	}

	return 0;
err:
	for (i = 0; i < S2MPS11_REGULATOR_MAX; i++)
		regulator_unregister(s2mps11->rdev[i]);

	return ret;
}

static int s2mps11_pmic_remove(struct platform_device *pdev)
{
	struct s2mps11_info *s2mps11 = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < S2MPS11_REGULATOR_MAX; i++)
		regulator_unregister(s2mps11->rdev[i]);

	return 0;
}

static const struct platform_device_id s2mps11_pmic_id[] = {
	{ "s2mps11-pmic", 0},
	{ },
};
MODULE_DEVICE_TABLE(platform, s2mps11_pmic_id);

static struct platform_driver s2mps11_pmic_driver = {
	.driver = {
		.name = "s2mps11-pmic",
		.owner = THIS_MODULE,
	},
	.probe = s2mps11_pmic_probe,
	.remove = s2mps11_pmic_remove,
	.id_table = s2mps11_pmic_id,
};

static int __init s2mps11_pmic_init(void)
{
	return platform_driver_register(&s2mps11_pmic_driver);
}
subsys_initcall(s2mps11_pmic_init);

static void __exit s2mps11_pmic_exit(void)
{
	platform_driver_unregister(&s2mps11_pmic_driver);
}
module_exit(s2mps11_pmic_exit);

/* Module information */
MODULE_AUTHOR("Sangbeom Kim <sbkim73@samsung.com>");
MODULE_DESCRIPTION("SAMSUNG S2MPS11 Regulator Driver");
MODULE_LICENSE("GPL");
