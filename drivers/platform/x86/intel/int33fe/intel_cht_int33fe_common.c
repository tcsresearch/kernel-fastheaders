// SPDX-License-Identifier: GPL-2.0
/*
 * Common code for Intel Cherry Trail ACPI INT33FE pseudo device drivers
 * (USB Micro-B and Type-C connector variants).
 *
 * Copyright (c) 2019 Yauhen Kharuzhy <jekhor@gmail.com>
 */

#include <linux/device_api_lock.h>
#include <linux/acpi.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "intel_cht_int33fe_common.h"

#define EXPECTED_PTYPE		4

static int cht_int33fe_check_hw_type(struct device *dev)
{
	unsigned long long ptyp;
	acpi_status status;
	int ret;

	status = acpi_evaluate_integer(ACPI_HANDLE(dev), "PTYP", NULL, &ptyp);
	if (ACPI_FAILURE(status)) {
		dev_err(dev, "Error getting PTYPE\n");
		return -ENODEV;
	}

	/*
	 * The same ACPI HID is used for different configurations check PTYP
	 * to ensure that we are dealing with the expected config.
	 */
	if (ptyp != EXPECTED_PTYPE)
		return -ENODEV;

	/* Check presence of INT34D3 (hardware-rev 3) expected for ptype == 4 */
	if (!acpi_dev_present("INT34D3", "1", 3)) {
		dev_err(dev, "Error PTYPE == %d, but no INT34D3 device\n",
			EXPECTED_PTYPE);
		return -ENODEV;
	}

	ret = i2c_acpi_client_count(ACPI_COMPANION(dev));
	if (ret < 0)
		return ret;

	switch (ret) {
	case 2:
		return INT33FE_HW_MICROB;
	case 4:
		return INT33FE_HW_TYPEC;
	default:
		return -ENODEV;
	}
}

static int cht_int33fe_probe(struct platform_device *pdev)
{
	struct cht_int33fe_data *data;
	struct device *dev = &pdev->dev;
	int ret;

	ret = cht_int33fe_check_hw_type(dev);
	if (ret < 0)
		return ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->dev = dev;

	switch (ret) {
	case INT33FE_HW_MICROB:
		data->probe = cht_int33fe_microb_probe;
		data->remove = cht_int33fe_microb_remove;
		break;

	case INT33FE_HW_TYPEC:
		data->probe = cht_int33fe_typec_probe;
		data->remove = cht_int33fe_typec_remove;
		break;
	}

	platform_set_drvdata(pdev, data);

	return data->probe(data);
}

static int cht_int33fe_remove(struct platform_device *pdev)
{
	struct cht_int33fe_data *data = platform_get_drvdata(pdev);

	return data->remove(data);
}

static const struct acpi_device_id cht_int33fe_acpi_ids[] = {
	{ "INT33FE", },
	{ }
};
MODULE_DEVICE_TABLE(acpi, cht_int33fe_acpi_ids);

static struct platform_driver cht_int33fe_driver = {
	.driver	= {
		.name = "Intel Cherry Trail ACPI INT33FE driver",
		.acpi_match_table = ACPI_PTR(cht_int33fe_acpi_ids),
	},
	.probe = cht_int33fe_probe,
	.remove = cht_int33fe_remove,
};

module_platform_driver(cht_int33fe_driver);

MODULE_DESCRIPTION("Intel Cherry Trail ACPI INT33FE pseudo device driver");
MODULE_AUTHOR("Yauhen Kharuzhy <jekhor@gmail.com>");
MODULE_LICENSE("GPL v2");
