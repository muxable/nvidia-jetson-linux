/*
 * Copyright (c) 2013-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/io.h>

#include <soc/tegra/common.h>
#include <soc/tegra/fuse.h>

#include "fuse.h"

struct tegra_sku_info tegra_sku_info = {
	.cpu_iddq_value = -ENOTSUPP,
	.gpu_iddq_value = -ENOTSUPP,
	.soc_iddq_value = -ENOTSUPP,
	.speedo_rev = -ENOTSUPP,
};
EXPORT_SYMBOL(tegra_sku_info);

static const char *tegra_revision_name[TEGRA_REVISION_MAX] = {
	[TEGRA_REVISION_UNKNOWN] = "unknown",
	[TEGRA_REVISION_A01]     = "A01",
	[TEGRA_REVISION_A01q]    = "A01q",
	[TEGRA_REVISION_A02]     = "A02",
	[TEGRA_REVISION_A02p]	 = "A02p",
	[TEGRA_REVISION_A03]     = "A03",
	[TEGRA_REVISION_A03p]    = "A03p",
	[TEGRA_REVISION_A04]     = "A04",
	[TEGRA_REVISION_A04p]     = "A04p",
	[TEGRA_REVISION_QT]     = "QT",
	[TEGRA_REVISION_SIM]     = "SIM",
};

static u8 fuse_readb(struct tegra_fuse *fuse, unsigned int offset)
{
	u32 val;

	val = fuse->read(fuse, round_down(offset, 4));
	val >>= (offset % 4) * 8;
	val &= 0xff;

	return val;
}

static ssize_t fuse_read(struct file *fd, struct kobject *kobj,
			 struct bin_attribute *attr, char *buf,
			 loff_t pos, size_t size)
{
	struct device *dev = kobj_to_dev(kobj);
	struct tegra_fuse *fuse = dev_get_drvdata(dev);
	int i;

	if (pos < 0 || pos >= attr->size)
		return 0;

	if (size > attr->size - pos)
		size = attr->size - pos;

	for (i = 0; i < size; i++)
		buf[i] = fuse_readb(fuse, pos + i);

	return i;
}

static struct bin_attribute fuse_bin_attr = {
	.attr = { .name = "fuse", .mode = S_IRUGO, },
	.read = fuse_read,
};

static int tegra_fuse_create_sysfs(struct device *dev, unsigned int size,
				   const struct tegra_fuse_info *info)
{
	fuse_bin_attr.size = size;

	return device_create_bin_file(dev, &fuse_bin_attr);
}

static const struct of_device_id car_match[] __initconst = {
	{ .compatible = "nvidia,tegra20-car", },
	{ .compatible = "nvidia,tegra30-car", },
	{ .compatible = "nvidia,tegra114-car", },
	{ .compatible = "nvidia,tegra124-car", },
	{ .compatible = "nvidia,tegra132-car", },
	{ .compatible = "nvidia,tegra210-car", },
	{},
};

static struct tegra_fuse *fuse = &(struct tegra_fuse) {
	.base = NULL,
	.soc = NULL,
};

static const struct of_device_id tegra_fuse_match[] = {
	{ .compatible = "nvidia,tegra194-efuse", .data = &tegra194_fuse_soc },
	{ .compatible = "nvidia,tegra186-efuse", .data = &tegra186_fuse_soc },
	{ .compatible = "nvidia,tegra210-efuse", .data = &tegra210_fuse_soc },
	{ .compatible = "nvidia,tegra132-efuse", .data = &tegra124_fuse_soc },
	{ .compatible = "nvidia,tegra124-efuse", .data = &tegra124_fuse_soc },
	{ .compatible = "nvidia,tegra114-efuse", .data = &tegra114_fuse_soc },
	{ .compatible = "nvidia,tegra30-efuse", .data = &tegra30_fuse_soc },
	{ .compatible = "nvidia,tegra20-efuse", .data = &tegra20_fuse_soc },
	{ /* sentinel */ }
};

int tegra_fuse_clock_enable(void)
{
	int err;

	err = clk_prepare_enable(fuse->clk);
	if (err < 0) {
		dev_err(fuse->dev, "failed to enable FUSE clock: %d\n", err);
		return err;
	}

	return 0;
}
EXPORT_SYMBOL(tegra_fuse_clock_enable);

int tegra_fuse_clock_disable(void)
{
	clk_disable_unprepare(fuse->clk);

	return 0;
}
EXPORT_SYMBOL(tegra_fuse_clock_disable);

static int tegra_fuse_probe(struct platform_device *pdev)
{
	void __iomem *base = fuse->base;
	struct resource *res;
	int err;
	bool is_clkon_always;

	/* take over the memory region from the early initialization */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	fuse->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(fuse->base)) {
		err = PTR_ERR(fuse->base);
		fuse->base = base;
		return err;
	}

	is_clkon_always = of_property_read_bool(pdev->dev.of_node,
						"nvidia,clock-always-on");

	fuse->clk = devm_clk_get(&pdev->dev, "fuse");
	if (IS_ERR(fuse->clk)) {
		dev_err(&pdev->dev, "failed to get FUSE clock: %ld",
			PTR_ERR(fuse->clk));
		fuse->base = base;
		return PTR_ERR(fuse->clk);
	}

	platform_set_drvdata(pdev, fuse);
	fuse->dev = &pdev->dev;

	if (fuse->soc->probe) {
		err = fuse->soc->probe(fuse);
		if (err < 0) {
			fuse->base = base;
			return err;
		}
	}

	if (tegra_fuse_create_sysfs(&pdev->dev, fuse->soc->info->size,
				    fuse->soc->info))
		return -ENODEV;

	if (is_clkon_always) {
		err = clk_prepare_enable(fuse->clk);
		if (err < 0) {
			dev_err(fuse->dev, "failed to enable FUSE clock: %d\n",
				err);
			return err;
		}
	}

	err = of_platform_default_populate(pdev->dev.of_node, NULL, &pdev->dev);
	if (err < 0)
		dev_dbg(&pdev->dev, "fuse child node not available\n");

	/* release the early I/O memory mapping */
	iounmap(base);

	return 0;
}

static struct platform_driver tegra_fuse_driver = {
	.driver = {
		.name = "tegra-fuse",
		.of_match_table = tegra_fuse_match,
		.suppress_bind_attrs = true,
	},
	.probe = tegra_fuse_probe,
};

static int __init tegra_fuse_init(void)
{
	return platform_driver_register(&tegra_fuse_driver);
}
subsys_initcall(tegra_fuse_init);

bool tegra_fuse_read_spare(unsigned int spare)
{
	unsigned int offset = fuse->soc->info->spare + spare * 4;

	return fuse->read_early(fuse, offset) & 1;
}

u32 tegra_fuse_read_early(unsigned int offset)
{
	return fuse->read_early(fuse, offset);
}

int tegra_fuse_readl(unsigned long offset, u32 *value)
{
	if (!fuse->read)
		return -EPROBE_DEFER;

	*value = fuse->read(fuse, offset);

	return 0;
}
EXPORT_SYMBOL(tegra_fuse_readl);

void tegra_fuse_writel(u32 value, unsigned long offset)
{
	if (!fuse->write)
		return;

	fuse->write(fuse, value, offset);
}
EXPORT_SYMBOL(tegra_fuse_writel);

int tegra_fuse_control_read(unsigned long offset, u32 *value)
{
	if (!fuse->control_read)
		return -EPROBE_DEFER;

	*value = fuse->control_read(fuse, offset);

	return 0;
}
EXPORT_SYMBOL_GPL(tegra_fuse_control_read);

void tegra_fuse_control_write(u32 value, unsigned long offset)
{
	if (!fuse->control_write)
		return;

	fuse->control_write(fuse, value, offset);
}
EXPORT_SYMBOL_GPL(tegra_fuse_control_write);

u32 tegra_fuse_get_subrevision(void)
{
	u32 reg;
	int ret;

	ret = tegra_fuse_readl(FUSE_OPT_SUBREVISION, &reg);
	if (ret)
		return ret;

	return reg & FUSE_OPT_SUBREVISION_MASK;
}

int tegra_fuse_get_cpu_iddq(void)
{
	if (!fuse->soc || !fuse->base)
		return -ENODEV;

	return tegra_sku_info.cpu_iddq_value;
}

int tegra_fuse_get_gpu_iddq(void)
{
	if (!fuse->soc || !fuse->base)
		return -ENODEV;

	return tegra_sku_info.gpu_iddq_value;
}

int tegra_fuse_get_soc_iddq(void)
{
	if (!fuse->soc || !fuse->base)
		return -ENODEV;

	return tegra_sku_info.soc_iddq_value;
}

static void tegra_enable_fuse_clk(void __iomem *base)
{
	u32 reg;

	reg = readl_relaxed(base + 0x48);
	reg |= 1 << 28;
	writel(reg, base + 0x48);

	/*
	 * Enable FUSE clock. This needs to be hardcoded because the clock
	 * subsystem is not active during early boot.
	 */
	reg = readl(base + 0x14);
	reg |= 1 << 7;
	writel(reg, base + 0x14);
}

static int __init tegra_init_fuse(void)
{
	const struct of_device_id *match;
	struct device_node *np;
	struct resource regs;

	tegra_set_tegraid_from_hw();

	np = of_find_matching_node_and_match(NULL, tegra_fuse_match, &match);
	if (!np) {
		/*
		 * Fall back to legacy initialization for 32-bit ARM only. All
		 * 64-bit ARM device tree files for Tegra are required to have
		 * a FUSE node.
		 *
		 * This is for backwards-compatibility with old device trees
		 * that didn't contain a FUSE node.
		 */
		if (IS_ENABLED(CONFIG_ARM) && soc_is_tegra()) {
			u8 chip = tegra_get_chip_id();

			regs.start = 0x7000f800;
			regs.end = 0x7000fbff;
			regs.flags = IORESOURCE_MEM;

			switch (chip) {
			case TEGRA20:
				fuse->soc = &tegra20_fuse_soc;
				break;
			case TEGRA30:
				fuse->soc = &tegra30_fuse_soc;
				break;
			case TEGRA114:
				fuse->soc = &tegra114_fuse_soc;
				break;
			case TEGRA124:
				fuse->soc = &tegra124_fuse_soc;
				break;
			default:
				pr_warn("Unsupported SoC: %02x\n", chip);
				break;
			}
		} else {
			/*
			 * At this point we're not running on Tegra, so play
			 * nice with multi-platform kernels.
			 */
			return 0;
		}
	} else {
		/*
		 * Extract information from the device tree if we've found a
		 * matching node.
		 */
		if (of_address_to_resource(np, 0, &regs) < 0) {
			pr_err("failed to get FUSE register\n");
			return -ENXIO;
		}

		fuse->soc = match->data;
	}

	np = of_find_matching_node(NULL, car_match);
	if (np) {
		void __iomem *base = of_iomap(np, 0);
		if (base) {
			tegra_enable_fuse_clk(base);
			iounmap(base);
		} else {
			pr_err("failed to map clock registers\n");
			return -ENXIO;
		}
	}

	fuse->base = ioremap_nocache(regs.start, resource_size(&regs));
	if (!fuse->base) {
		pr_err("failed to map FUSE registers\n");
		return -ENXIO;
	}

	fuse->soc->init(fuse);

	pr_info("Tegra Revision: %s SKU: 0x%x CPU Process: %d SoC Process: %d\n",
		tegra_revision_name[tegra_sku_info.revision],
		tegra_sku_info.sku_id, tegra_sku_info.cpu_process_id,
		tegra_sku_info.soc_process_id);
	pr_debug("Tegra CPU Speedo ID %d, SoC Speedo ID %d\n",
		 tegra_sku_info.cpu_speedo_id, tegra_sku_info.soc_speedo_id);

	return 0;
}
early_initcall(tegra_init_fuse);
