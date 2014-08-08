/* Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/err.h>
#include <linux/regulator/consumer.h>
#include <linux/lcd_notify.h>

#include "mdss.h"
#include "mdss_panel.h"
#include "mdss_dsi.h"
#include "mdss_debug.h"

static unsigned char *mdss_dsi_base;
extern struct mdss_panel_data *cmds_panel_data;

static int mdss_dsi_regulator_init(struct platform_device *pdev)
{
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;

	if (!pdev) {
		pr_err("%s: invalid input\n", __func__);
		return -EINVAL;
	}

	ctrl_pdata = platform_get_drvdata(pdev);
	if (!ctrl_pdata) {
		pr_err("%s: invalid driver data\n", __func__);
		return -EINVAL;
	}

	return msm_dss_config_vreg(&pdev->dev,
			ctrl_pdata->power_data.vreg_config,
			ctrl_pdata->power_data.num_vreg, 1);
}

static int mdss_dsi_panel_power_on(struct mdss_panel_data *pdata, int enable)
{
	int ret;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		ret = -EINVAL;
		goto error;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);
	pr_debug("%s: enable=%d\n", __func__, enable);

	cmds_panel_data = pdata;

	if (enable) {
		ret = msm_dss_enable_vreg(
			ctrl_pdata->power_data.vreg_config,
			ctrl_pdata->power_data.num_vreg, 1);
		if (ret) {
			pr_err("%s:Failed to enable vregs.rc=%d\n",
				__func__, ret);
			goto error;
		}

		ret = mdss_dsi_panel_reset(pdata, 1);
		if (ret) {
			pr_err("%s: Panel reset failed. rc=%d\n",
					__func__, ret);
			if (msm_dss_enable_vreg(
			ctrl_pdata->power_data.vreg_config,
			ctrl_pdata->power_data.num_vreg, 0))
				pr_err("Disable vregs failed\n");
			goto error;
		}
	} else {

		mdss_dsi_panel_reset(pdata, 0);

		ret = msm_dss_enable_vreg(
			ctrl_pdata->power_data.vreg_config,
			ctrl_pdata->power_data.num_vreg, 0);
		if (ret) {
			pr_err("%s: Failed to disable vregs.rc=%d\n",
				__func__, ret);
		}
	}
error:
	return ret;
}

static void mdss_dsi_put_dt_vreg_data(struct device *dev,
	struct dss_module_power *module_power)
{
	if (!module_power) {
		pr_err("%s: invalid input\n", __func__);
		return;
	}

	if (module_power->vreg_config) {
		devm_kfree(dev, module_power->vreg_config);
		module_power->vreg_config = NULL;
	}
	module_power->num_vreg = 0;
}

static int mdss_dsi_get_dt_vreg_data(struct device *dev,
	struct dss_module_power *mp)
{
	int i = 0, rc = 0;
	u32 tmp = 0;
	struct device_node *of_node = NULL, *supply_node = NULL;

	if (!dev || !mp) {
		pr_err("%s: invalid input\n", __func__);
		rc = -EINVAL;
		return rc;
	}

	of_node = dev->of_node;

	mp->num_vreg = 0;
	for_each_child_of_node(of_node, supply_node) {
		if (!strncmp(supply_node->name, "qcom,platform-supply-entry",
						26))
			++mp->num_vreg;
	}
	if (mp->num_vreg == 0) {
		pr_debug("%s: no vreg\n", __func__);
		goto novreg;
	} else {
		pr_debug("%s: vreg found. count=%d\n", __func__, mp->num_vreg);
	}

	mp->vreg_config = devm_kzalloc(dev, sizeof(struct dss_vreg) *
		mp->num_vreg, GFP_KERNEL);
	if (!mp->vreg_config) {
		pr_err("%s: can't alloc vreg mem\n", __func__);
		rc = -ENOMEM;
		goto error;
	}

	for_each_child_of_node(of_node, supply_node) {
		if (!strncmp(supply_node->name, "qcom,platform-supply-entry",
						26)) {
			const char *st = NULL;
			/* vreg-name */
			rc = of_property_read_string(supply_node,
				"qcom,supply-name", &st);
			if (rc) {
				pr_err("%s: error reading name. rc=%d\n",
					__func__, rc);
				goto error;
			}
			snprintf(mp->vreg_config[i].vreg_name,
				ARRAY_SIZE((mp->vreg_config[i].vreg_name)),
				"%s", st);
			/* vreg-min-voltage */
			rc = of_property_read_u32(supply_node,
				"qcom,supply-min-voltage", &tmp);
			if (rc) {
				pr_err("%s: error reading min volt. rc=%d\n",
					__func__, rc);
				goto error;
			}
			mp->vreg_config[i].min_voltage = tmp;

			/* vreg-max-voltage */
			rc = of_property_read_u32(supply_node,
				"qcom,supply-max-voltage", &tmp);
			if (rc) {
				pr_err("%s: error reading max volt. rc=%d\n",
					__func__, rc);
				goto error;
			}
			mp->vreg_config[i].max_voltage = tmp;

			/* enable-load */
			rc = of_property_read_u32(supply_node,
				"qcom,supply-enable-load", &tmp);
			if (rc) {
				pr_err("%s: error reading enable load. rc=%d\n",
					__func__, rc);
				goto error;
			}
			mp->vreg_config[i].enable_load = tmp;

			/* disable-load */
			rc = of_property_read_u32(supply_node,
				"qcom,supply-disable-load", &tmp);
			if (rc) {
				pr_err("%s: error reading disable load. rc=%d\n",
					__func__, rc);
				goto error;
			}
			mp->vreg_config[i].disable_load = tmp;

			/* pre-sleep */
			rc = of_property_read_u32(supply_node,
				"qcom,supply-pre-on-sleep", &tmp);
			if (rc) {
				pr_debug("%s: error reading supply pre sleep value. rc=%d\n",
					__func__, rc);
			}
			mp->vreg_config[i].pre_on_sleep = (!rc ? tmp : 0);

			rc = of_property_read_u32(supply_node,
				"qcom,supply-pre-off-sleep", &tmp);
			if (rc) {
				pr_debug("%s: error reading supply pre sleep value. rc=%d\n",
					__func__, rc);
			}
			mp->vreg_config[i].pre_off_sleep = (!rc ? tmp : 0);

			/* post-sleep */
			rc = of_property_read_u32(supply_node,
				"qcom,supply-post-on-sleep", &tmp);
			if (rc) {
				pr_debug("%s: error reading supply post sleep value. rc=%d\n",
					__func__, rc);
			}
			mp->vreg_config[i].post_on_sleep = (!rc ? tmp : 0);

			rc = of_property_read_u32(supply_node,
				"qcom,supply-post-off-sleep", &tmp);
			if (rc) {
				pr_debug("%s: error reading supply post sleep value. rc=%d\n",
					__func__, rc);
			}
			mp->vreg_config[i].post_off_sleep = (!rc ? tmp : 0);

			pr_debug("%s: %s min=%d, max=%d, enable=%d, disable=%d, preonsleep=%d, postonsleep=%d, preoffsleep=%d, postoffsleep=%d\n",
				__func__,
				mp->vreg_config[i].vreg_name,
				mp->vreg_config[i].min_voltage,
				mp->vreg_config[i].max_voltage,
				mp->vreg_config[i].enable_load,
				mp->vreg_config[i].disable_load,
				mp->vreg_config[i].pre_on_sleep,
				mp->vreg_config[i].post_on_sleep,
				mp->vreg_config[i].pre_off_sleep,
				mp->vreg_config[i].post_off_sleep
				);
			++i;
		}
	}

	return rc;

error:
	if (mp->vreg_config) {
		devm_kfree(dev, mp->vreg_config);
		mp->vreg_config = NULL;
	}
novreg:
	mp->num_vreg = 0;

	return rc;
}

static int mdss_dsi_get_panel_cfg(char *panel_cfg)
{
	int rc;
	struct mdss_panel_cfg *pan_cfg = NULL;

	if (!panel_cfg)
		return MDSS_PANEL_INTF_INVALID;

	pan_cfg = mdss_panel_intf_type(MDSS_PANEL_INTF_DSI);
	if (IS_ERR(pan_cfg)) {
		return PTR_ERR(pan_cfg);
	} else if (!pan_cfg) {
		panel_cfg[0] = 0;
		return 0;
	}

	pr_debug("%s:%d: cfg:[%s]\n", __func__, __LINE__,
		 pan_cfg->arg_cfg);
	rc = strlcpy(panel_cfg, pan_cfg->arg_cfg,
		     sizeof(pan_cfg->arg_cfg));
	return rc;
}

static int mdss_dsi_off(struct mdss_panel_data *pdata)
{
	int ret = 0;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	if (!pdata->panel_info.panel_power_on) {
		pr_warn("%s:%d Panel already off.\n", __func__, __LINE__);
		return 0;
	}

	pdata->panel_info.panel_power_on = 0;

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	pr_debug("%s+: ctrl=%p ndx=%d\n", __func__,
				ctrl_pdata, ctrl_pdata->ndx);

	if (pdata->panel_info.type == MIPI_CMD_PANEL)
		mdss_dsi_clk_ctrl(ctrl_pdata, DSI_ALL_CLKS, 1);

	/* disable DSI controller */
	mdss_dsi_controller_cfg(0, pdata);

	/* disable DSI phy */
	mdss_dsi_phy_disable(ctrl_pdata);

	mdss_dsi_clk_ctrl(ctrl_pdata, DSI_ALL_CLKS, 0);

	ret = mdss_dsi_panel_power_on(pdata, 0);
	if (ret) {
		pr_err("%s: Panel power off failed\n", __func__);
		return ret;
	}

	pr_debug("%s-:\n", __func__);

	return ret;
}

static void __mdss_dsi_ctrl_setup(struct mdss_panel_data *pdata)
{
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	struct mdss_panel_info *pinfo;
	struct mipi_panel_info *mipi;
	u32 clk_rate;
	u32 hbp, hfp, vbp, vfp, hspw, vspw, width, height;
	u32 ystride, bpp, data, dst_bpp;
	u32 dummy_xres, dummy_yres;
	u32 hsync_period, vsync_period;

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	pinfo = &pdata->panel_info;

	clk_rate = pdata->panel_info.clk_rate;
	clk_rate = min(clk_rate, pdata->panel_info.clk_max);

	dst_bpp = pdata->panel_info.fbc.enabled ?
		(pdata->panel_info.fbc.target_bpp) : (pinfo->bpp);

	hbp = mult_frac(pdata->panel_info.lcdc.h_back_porch, dst_bpp,
			pdata->panel_info.bpp);
	hfp = mult_frac(pdata->panel_info.lcdc.h_front_porch, dst_bpp,
			pdata->panel_info.bpp);
	vbp = mult_frac(pdata->panel_info.lcdc.v_back_porch, dst_bpp,
			pdata->panel_info.bpp);
	vfp = mult_frac(pdata->panel_info.lcdc.v_front_porch, dst_bpp,
			pdata->panel_info.bpp);
	hspw = mult_frac(pdata->panel_info.lcdc.h_pulse_width, dst_bpp,
			pdata->panel_info.bpp);
	vspw = pdata->panel_info.lcdc.v_pulse_width;
	width = mult_frac(pdata->panel_info.xres, dst_bpp,
			pdata->panel_info.bpp);
	height = pdata->panel_info.yres;

	if (pdata->panel_info.type == MIPI_VIDEO_PANEL) {
		dummy_xres = pdata->panel_info.lcdc.xres_pad;
		dummy_yres = pdata->panel_info.lcdc.yres_pad;
	}

	vsync_period = vspw + vbp + height + dummy_yres + vfp;
	hsync_period = hspw + hbp + width + dummy_xres + hfp;

	mipi = &pdata->panel_info.mipi;
	if (pdata->panel_info.type == MIPI_VIDEO_PANEL) {
		MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x24,
			((hspw + hbp + width + dummy_xres) << 16 |
			(hspw + hbp)));
		MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x28,
			((vspw + vbp + height + dummy_yres) << 16 |
			(vspw + vbp)));
		MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x2C,
				((vsync_period - 1) << 16)
				| (hsync_period - 1));

		MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x30, (hspw << 16));
		MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x34, 0);
		MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x38, (vspw << 16));

	} else {		/* command mode */
		if (mipi->dst_format == DSI_CMD_DST_FORMAT_RGB888)
			bpp = 3;
		else if (mipi->dst_format == DSI_CMD_DST_FORMAT_RGB666)
			bpp = 3;
		else if (mipi->dst_format == DSI_CMD_DST_FORMAT_RGB565)
			bpp = 2;
		else
			bpp = 3;	/* Default format set to RGB888 */

		ystride = width * bpp + 1;

		/* DSI_COMMAND_MODE_MDP_STREAM_CTRL */
		data = (ystride << 16) | (mipi->vc << 8) | DTYPE_DCS_LWRITE;
		MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x60, data);
		MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x58, data);

		/* DSI_COMMAND_MODE_MDP_STREAM_TOTAL */
		data = height << 16 | width;
		MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x64, data);
		MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x5C, data);
	}
}

int mdss_dsi_on(struct mdss_panel_data *pdata)
{
	int ret = 0;
	struct mdss_panel_info *pinfo;
	struct mipi_panel_info *mipi;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	if (pdata->panel_info.panel_power_on) {
		pr_warn("%s:%d Panel already on.\n", __func__, __LINE__);
		return 0;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	pr_debug("%s+: ctrl=%p ndx=%d\n",
				__func__, ctrl_pdata, ctrl_pdata->ndx);

	pinfo = &pdata->panel_info;
	mipi = &pdata->panel_info.mipi;

	ret = mdss_dsi_panel_power_on(pdata, 1);
	if (ret) {
		pr_err("%s: Panel power on failed\n", __func__);
		pdata->panel_info.panel_power_on = 0;
		return ret;
	}

	ret = mdss_dsi_clk_ctrl(ctrl_pdata, DSI_BUS_CLKS, 1);
	if (ret) {
		pr_err("%s: failed to enable bus clocks. rc=%d\n", __func__,
			ret);
		ret = mdss_dsi_panel_power_on(pdata, 0);
		if (ret) {
			pr_err("%s: Panel reset failed. rc=%d\n",
					__func__, ret);
			return ret;
		}
		pdata->panel_info.panel_power_on = 0;
		return ret;
	}
	pdata->panel_info.panel_power_on = 1;

	mdss_dsi_phy_sw_reset((ctrl_pdata->ctrl_base));
	mdss_dsi_phy_init(pdata);
	mdss_dsi_clk_ctrl(ctrl_pdata, DSI_BUS_CLKS, 0);

	mdss_dsi_clk_ctrl(ctrl_pdata, DSI_ALL_CLKS, 1);

	__mdss_dsi_ctrl_setup(pdata);
	mdss_dsi_sw_reset(pdata);
	mdss_dsi_host_init(pdata);

	if (mipi->force_clk_lane_hs) {
		u32 tmp;

		tmp = MIPI_INP((ctrl_pdata->ctrl_base) + 0xac);
		tmp |= (1<<28);
		MIPI_OUTP((ctrl_pdata->ctrl_base) + 0xac, tmp);
		wmb();
	}

	if (pdata->panel_info.type == MIPI_CMD_PANEL)
		mdss_dsi_clk_ctrl(ctrl_pdata, DSI_ALL_CLKS, 0);

	pr_debug("%s-:\n", __func__);
	return 0;
}

static inline bool __mdss_dsi_ulps_feature_enabled(
	struct mdss_panel_data *pdata)
{
	return pdata->panel_info.ulps_feature_enabled;
}

static int mdss_dsi_ulps_config_sub(struct mdss_dsi_ctrl_pdata *ctrl_pdata,
	int enable)
{
	int ret = 0;
	struct mdss_panel_data *pdata = NULL;
	struct mipi_panel_info *pinfo = NULL;
	u32 lane_status = 0;
	u32 active_lanes = 0;

	if (!ctrl_pdata) {
		pr_err("%s: invalid input\n", __func__);
		return -EINVAL;
	}

	pdata = &ctrl_pdata->panel_data;
	if (!pdata) {
		pr_err("%s: Invalid panel data\n", __func__);
		return -EINVAL;
	}
	pinfo = &pdata->panel_info.mipi;

	if (!__mdss_dsi_ulps_feature_enabled(pdata)) {
		pr_debug("%s: ULPS feature not supported. enable=%d\n",
			__func__, enable);
		return -ENOTSUPP;
	}

	if (enable && !ctrl_pdata->ulps) {
		/* No need to configure ULPS mode when entering suspend state */
		if (!pdata->panel_info.panel_power_on) {
			pr_err("%s: panel off. returning\n", __func__);
			goto error;
		}

		if (__mdss_dsi_clk_enabled(ctrl_pdata, DSI_LINK_CLKS)) {
			pr_err("%s: cannot enter ulps mode if dsi clocks are on\n",
				__func__);
			ret = -EPERM;
			goto error;
		}

		ret = mdss_dsi_clk_ctrl(ctrl_pdata, DSI_ALL_CLKS, 1);
		if (ret) {
			pr_err("%s: Failed to enable clocks. rc=%d\n",
				__func__, ret);
			goto error;
		}

		/*
		 * ULPS Entry Request.
		 * Wait for a short duration to ensure that the lanes
		 * enter ULP state.
		 */
		MIPI_OUTP(ctrl_pdata->ctrl_base + 0x0AC, 0x01F);
		usleep(100);

		/* Check to make sure that all active data lanes are in ULPS */
		if (pinfo->data_lane3)
			active_lanes |= BIT(11);
		if (pinfo->data_lane2)
			active_lanes |= BIT(10);
		if (pinfo->data_lane1)
			active_lanes |= BIT(9);
		if (pinfo->data_lane0)
			active_lanes |= BIT(8);
		active_lanes |= BIT(12); /* clock lane */
		lane_status = MIPI_INP(ctrl_pdata->ctrl_base + 0xA8);
		if (lane_status & active_lanes) {
			pr_err("%s: ULPS entry req failed. Lane status=0x%08x\n",
				__func__, lane_status);
			ret = -EINVAL;
			mdss_dsi_clk_ctrl(ctrl_pdata, DSI_ALL_CLKS, 0);
			goto error;
		}

		/* Enable MMSS DSI Clamps */
		MIPI_OUTP(ctrl_pdata->mmss_misc_io.base + 0x14, 0x3FF);
		MIPI_OUTP(ctrl_pdata->mmss_misc_io.base + 0x14, 0x83FF);

		wmb();

		MIPI_OUTP(ctrl_pdata->mmss_misc_io.base + 0x108, 0x1);
		/* disable DSI controller */
		mdss_dsi_controller_cfg(0, pdata);

		mdss_dsi_clk_ctrl(ctrl_pdata, DSI_ALL_CLKS, 0);
		ctrl_pdata->ulps = true;
	} else if (ctrl_pdata->ulps) {
		ret = mdss_dsi_clk_ctrl(ctrl_pdata, DSI_BUS_CLKS, 1);
		if (ret) {
			pr_err("%s: Failed to enable bus clocks. rc=%d\n",
				__func__, ret);
			goto error;
		}

		MIPI_OUTP(ctrl_pdata->mmss_misc_io.base + 0x108, 0x0);
		mdss_dsi_phy_init(pdata);

		__mdss_dsi_ctrl_setup(pdata);
		mdss_dsi_sw_reset(pdata);
		mdss_dsi_host_init(pdata);
		mdss_dsi_op_mode_config(pdata->panel_info.mipi.mode,
			pdata);

		/*
		 * ULPS Entry Request. This is needed because, after power
		 * collapse and reset, the DSI controller resets back to
		 * idle state and not ULPS.
		 * Wait for a short duration to ensure that the lanes
		 * enter ULP state.
		 */
		MIPI_OUTP(ctrl_pdata->ctrl_base + 0x0AC, 0x01F);
		usleep(100);

		/* Disable MMSS DSI Clamps */
		MIPI_OUTP(ctrl_pdata->mmss_misc_io.base + 0x14, 0x3FF);
		MIPI_OUTP(ctrl_pdata->mmss_misc_io.base + 0x14, 0x0);

		ret = mdss_dsi_clk_ctrl(ctrl_pdata, DSI_LINK_CLKS, 1);
		if (ret) {
			pr_err("%s: Failed to enable link clocks. rc=%d\n",
				__func__, ret);
			mdss_dsi_clk_ctrl(ctrl_pdata, DSI_BUS_CLKS, 0);
			goto error;
		}

		/*
		 * Clear out any phy errors prior to exiting ULPS
		 * This fixes certain instances where phy does not exit
		 * ULPS cleanly.
		 */
		mdss_dsi_dln0_phy_err(ctrl_pdata);

		/*
		 * ULPS Exit Request
		 * Hardware requirement is to wait for at least 1ms
		 */
		MIPI_OUTP(ctrl_pdata->ctrl_base + 0x0AC, 0x1F00);
		usleep(1000);

		/*
		 * Sometimes when exiting ULPS, it is possible that some DSI
		 * lanes are not in the stop state which could lead to DSI
		 * commands not going through. To avoid this, force the lanes
		 * to be in stop state.
		 */
		MIPI_OUTP(ctrl_pdata->ctrl_base + 0x0AC, 0x1F0000);

		MIPI_OUTP(ctrl_pdata->ctrl_base + 0x0AC, 0x0);

		/*
		 * Wait for a short duration before enabling
		 * data transmission
		 */
		usleep(100);

		lane_status = MIPI_INP(ctrl_pdata->ctrl_base + 0xA8);
		mdss_dsi_clk_ctrl(ctrl_pdata, DSI_LINK_CLKS, 0);
		mdss_dsi_clk_ctrl(ctrl_pdata, DSI_BUS_CLKS, 0);
		ctrl_pdata->ulps = false;
	}

	pr_debug("%s: DSI lane status = 0x%08x. Ulps %s\n", __func__,
		lane_status, enable ? "enabled" : "disabled");

error:
	return ret;
}

static int mdss_dsi_ulps_config(struct mdss_dsi_ctrl_pdata *ctrl,
	int enable)
{
	int rc;
	struct mdss_dsi_ctrl_pdata *sctrl = NULL;

	if (&ctrl->mmss_misc_io == NULL) {
		pr_err("%s: mmss_misc_io is NULL. ULPS not valid\n", __func__);
		return -EINVAL;
	}

	if (ctrl->flags & DSI_FLAG_CLOCK_MASTER)
		sctrl = mdss_dsi_ctrl_slave(ctrl);

	if (sctrl) {
		pr_debug("%s: configuring ulps (%s) for slave ctrl\n",
			__func__, (enable ? "on" : "off"));
		rc = mdss_dsi_ulps_config_sub(sctrl, enable);
		if (rc)
			return rc;
	}

	pr_debug("%s: configuring ulps (%s) for master ctrl\n",
		__func__, (enable ? "on" : "off"));
	return mdss_dsi_ulps_config_sub(ctrl, enable);
}

static int mdss_dsi_unblank(struct mdss_panel_data *pdata)
{
	int ret = 0;
	struct mipi_panel_info *mipi;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;

	pr_debug("%s+:\n", __func__);

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);
	mipi  = &pdata->panel_info.mipi;

	if (!(ctrl_pdata->ctrl_state & CTRL_STATE_PANEL_INIT)) {
		ret = ctrl_pdata->on(pdata);
		if (ret) {
			pr_err("%s: unable to initialize the panel\n",
							__func__);
			return ret;
		}
		ctrl_pdata->ctrl_state |= CTRL_STATE_PANEL_INIT;
	}

	if (pdata->panel_info.type == MIPI_CMD_PANEL) {
		if (mipi->vsync_enable && mipi->hw_vsync_mode
			&& gpio_is_valid(ctrl_pdata->disp_te_gpio)) {
				mdss_dsi_set_tear_on(ctrl_pdata);
		}
	}

	pr_debug("%s-:\n", __func__);

	return ret;
}

static int mdss_dsi_blank(struct mdss_panel_data *pdata)
{
	int ret = 0;
	struct mipi_panel_info *mipi;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;

	pr_debug("%s+:\n", __func__);

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);
	mipi = &pdata->panel_info.mipi;

	if (__mdss_dsi_ulps_feature_enabled(pdata) &&
		(ctrl_pdata->ulps)) {
		/* Disable ULPS mode before blanking the panel */
		ret = mdss_dsi_ulps_config(ctrl_pdata, 0);
		if (ret) {
			pr_err("%s: failed to exit ULPS mode. rc=%d\n",
				__func__, ret);
			return ret;
		}
	}

	if (pdata->panel_info.type == MIPI_VIDEO_PANEL &&
			ctrl_pdata->off_cmds.link_state == DSI_LP_MODE) {
		mdss_dsi_sw_reset(pdata);
		mdss_dsi_host_init(pdata);
	}

	mdss_dsi_op_mode_config(DSI_CMD_MODE, pdata);

	if (pdata->panel_info.type == MIPI_CMD_PANEL) {
		if (mipi->vsync_enable && mipi->hw_vsync_mode
			&& gpio_is_valid(ctrl_pdata->disp_te_gpio)) {
			mdss_dsi_set_tear_off(ctrl_pdata);
		}
	}

	if (ctrl_pdata->ctrl_state & CTRL_STATE_PANEL_INIT) {
		ret = ctrl_pdata->off(pdata);
		if (ret) {
			pr_err("%s: Panel OFF failed\n", __func__);
			return ret;
		}
		ctrl_pdata->ctrl_state &= ~CTRL_STATE_PANEL_INIT;
	}
	pr_debug("%s-:End\n", __func__);
	return ret;
}

int mdss_dsi_cont_splash_on(struct mdss_panel_data *pdata)
{
	int ret = 0;
	struct mipi_panel_info *mipi;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;

	pr_info("%s:%d DSI on for continuous splash.\n", __func__, __LINE__);

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	mipi = &pdata->panel_info.mipi;

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	pr_debug("%s+: ctrl=%p ndx=%d\n", __func__,
				ctrl_pdata, ctrl_pdata->ndx);

	WARN(!(ctrl_pdata->ctrl_state & CTRL_STATE_PANEL_INIT),
		"Incorrect Ctrl state=0x%x\n", ctrl_pdata->ctrl_state);

	mdss_dsi_sw_reset(pdata);
	mdss_dsi_host_init(pdata);
	mdss_dsi_op_mode_config(mipi->mode, pdata);
	pr_debug("%s-:End\n", __func__);
	return ret;
}

static int mdss_dsi_ctl_partial_update(struct mdss_panel_data *pdata)
{
	int rc = -EINVAL;
	u32 data;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	/* DSI_COMMAND_MODE_MDP_STREAM_CTRL */
	data = (((pdata->panel_info.roi_w * 3) + 1) << 16) |
			(pdata->panel_info.mipi.vc << 8) | DTYPE_DCS_LWRITE;
	MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x60, data);
	MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x58, data);

	/* DSI_COMMAND_MODE_MDP_STREAM_TOTAL */
	data = pdata->panel_info.roi_h << 16 | pdata->panel_info.roi_w;
	MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x64, data);
	MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x5C, data);

	if (ctrl_pdata->partial_update_fnc)
		rc = ctrl_pdata->partial_update_fnc(pdata);

	if (rc) {
		pr_err("%s: unable to initialize the panel\n",
				__func__);
		return rc;
	}

	return rc;
}

int mdss_dsi_register_recovery_handler(struct mdss_dsi_ctrl_pdata *ctrl,
	struct mdss_panel_recovery *recovery)
{
	mutex_lock(&ctrl->mutex);
	ctrl->recovery = recovery;
	mutex_unlock(&ctrl->mutex);
	return 0;
}

static int mdss_dsi_event_handler(struct mdss_panel_data *pdata,
				  int event, void *arg)
{
	int rc = 0;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}
	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);
	pr_debug("%s+:event=%d\n", __func__, event);

	MDSS_XLOG(event, arg, ctrl_pdata->ndx, 0x3333);

	switch (event) {
	case MDSS_EVENT_UNBLANK:
		lcd_notifier_call_chain(LCD_EVENT_ON_START, NULL);
		rc = mdss_dsi_on(pdata);
		mdss_dsi_op_mode_config(pdata->panel_info.mipi.mode,
							pdata);
		if (ctrl_pdata->on_cmds.link_state == DSI_LP_MODE)
			rc = mdss_dsi_unblank(pdata);
		break;
	case MDSS_EVENT_PANEL_ON:
		ctrl_pdata->ctrl_state |= CTRL_STATE_MDP_ACTIVE;
		if (ctrl_pdata->on_cmds.link_state == DSI_HS_MODE)
			rc = mdss_dsi_unblank(pdata);
		lcd_notifier_call_chain(LCD_EVENT_ON_END, NULL);
		break;
	case MDSS_EVENT_BLANK:
		lcd_notifier_call_chain(LCD_EVENT_OFF_START, NULL);
		if (ctrl_pdata->off_cmds.link_state == DSI_HS_MODE)
			rc = mdss_dsi_blank(pdata);
		break;
	case MDSS_EVENT_PANEL_OFF:
		ctrl_pdata->ctrl_state &= ~CTRL_STATE_MDP_ACTIVE;
		if (ctrl_pdata->off_cmds.link_state == DSI_LP_MODE)
			rc = mdss_dsi_blank(pdata);
		rc = mdss_dsi_off(pdata);
		lcd_notifier_call_chain(LCD_EVENT_OFF_END, NULL);
		break;
	case MDSS_EVENT_CONT_SPLASH_FINISH:
		if (ctrl_pdata->off_cmds.link_state == DSI_LP_MODE)
			rc = mdss_dsi_blank(pdata);
		ctrl_pdata->ctrl_state &= ~CTRL_STATE_MDP_ACTIVE;
		rc = mdss_dsi_cont_splash_on(pdata);
		break;
	case MDSS_EVENT_PANEL_CLK_CTRL:
		mdss_dsi_clk_req(ctrl_pdata, (int)arg);
		break;
	case MDSS_EVENT_DSI_CMDLIST_KOFF:
		mdss_dsi_cmdlist_commit(ctrl_pdata, 1);
		break;
	case MDSS_EVENT_CONT_SPLASH_BEGIN:
		if (ctrl_pdata->off_cmds.link_state == DSI_HS_MODE) {
			/* Panel is Enabled in Bootloader */
			rc = mdss_dsi_blank(pdata);
		}
		break;
	case MDSS_EVENT_ENABLE_PARTIAL_UPDATE:
		rc = mdss_dsi_ctl_partial_update(pdata);
		break;
	case MDSS_EVENT_DSI_ULPS_CTRL:
		rc = mdss_dsi_ulps_config(ctrl_pdata, (int)arg);
		break;
	case MDSS_EVENT_REGISTER_RECOVERY_HANDLER:
		rc = mdss_dsi_register_recovery_handler(ctrl_pdata,
			(struct mdss_panel_recovery *)arg);
		break;
	default:
		pr_debug("%s: unhandled event=%d\n", __func__, event);
		break;
	}
	pr_debug("%s-:event=%d, rc=%d\n", __func__, event, rc);
	return rc;
}

/**
 * mdss_dsi_find_panel_of_node(): find device node of dsi panel
 * @pdev: platform_device of the dsi ctrl node
 * @panel_cfg: string containing intf specific config data
 *
 * Function finds the panel device node using the interface
 * specific configuration data. This configuration data is
 * could be derived from the result of bootloader's GCDB
 * panel detection mechanism. If such config data doesn't
 * exist then this panel returns the default panel configured
 * in the device tree.
 *
 * returns pointer to panel node on success, NULL on error.
 */
static struct device_node *mdss_dsi_find_panel_of_node(
		struct platform_device *pdev, char *panel_cfg)
{
	int l;
	int ctrl_id = -1;
	char *panel_name;
	struct device_node *dsi_pan_node = NULL, *mdss_node = NULL;

	l = strlen(panel_cfg);
	if (!l) {
		/* no panel cfg chg, parse dt */
		pr_debug("%s:%d: no cmd line cfg present\n",
			 __func__, __LINE__);
		dsi_pan_node = of_parse_phandle(
			pdev->dev.of_node,
			"qcom,dsi-pref-prim-pan", 0);
		if (!dsi_pan_node) {
			pr_err("%s:can't find panel phandle\n",
			       __func__);
			return NULL;
		}
	} else {
		if (panel_cfg[0] == '0') {
			pr_debug("%s:%d: DSI ctrl 1\n", __func__, __LINE__);
			ctrl_id = 0;
		} else if (panel_cfg[0] == '1') {
			pr_debug("%s:%d: DSI ctrl 2\n", __func__, __LINE__);
			ctrl_id = 1;
		}
		if ((pdev->id - 1) != ctrl_id) {
			pr_err("%s:%d:pdev_ID=[%d]\n",
			       __func__, __LINE__, pdev->id);
			return NULL;
		}
		/*
		 * skip first two chars '<dsi_ctrl_id>' and
		 * ':' to get to the panel name
		 */
		panel_name = panel_cfg + 2;
		pr_debug("%s:%d:%s:%s\n", __func__, __LINE__,
			 panel_cfg, panel_name);

		mdss_node = of_parse_phandle(pdev->dev.of_node,
					     "qcom,mdss-mdp", 0);

		if (!mdss_node) {
			pr_err("%s: %d: mdss_node null\n",
			       __func__, __LINE__);
			return NULL;
		}
		dsi_pan_node = of_find_node_by_name(mdss_node,
						    panel_name);
		if (!dsi_pan_node) {
			pr_err("%s: invalid pan node\n",
			       __func__);
			return NULL;
		}
	}
	return dsi_pan_node;
}

static int __devinit mdss_dsi_ctrl_probe(struct platform_device *pdev)
{
	int rc = 0;
	u32 index;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	struct device_node *dsi_pan_node = NULL;
	char panel_cfg[MDSS_MAX_PANEL_LEN];
	struct resource *mdss_dsi_mres;
	const char *ctrl_name;
	static struct mdss_panel_common_pdata vendor_pdata;
	bool cmd_cfg_cont_splash = true;

	if (!mdss_is_ready()) {
		pr_err("%s: MDP not probed yet!\n", __func__);
		return -EPROBE_DEFER;
	}

	if (!pdev->dev.of_node) {
		pr_err("DSI driver only supports device tree probe\n");
		return -ENOTSUPP;
	}

	ctrl_pdata = platform_get_drvdata(pdev);
	if (!ctrl_pdata) {
		ctrl_pdata = devm_kzalloc(&pdev->dev,
					  sizeof(struct mdss_dsi_ctrl_pdata),
					  GFP_KERNEL);
		if (!ctrl_pdata) {
			pr_err("%s: FAILED: cannot alloc dsi ctrl\n",
			       __func__);
			rc = -ENOMEM;
			goto error_no_mem;
		}
		platform_set_drvdata(pdev, ctrl_pdata);
	}

	ctrl_name = of_get_property(pdev->dev.of_node, "label", NULL);
	if (!ctrl_name)
		pr_info("%s:%d, DSI Ctrl name not specified\n",
			__func__, __LINE__);
	else
		pr_info("%s: DSI Ctrl name = %s\n",
			__func__, ctrl_name);

	rc = of_property_read_u32(pdev->dev.of_node,
				  "cell-index", &index);
	if (rc) {
		dev_err(&pdev->dev,
			"%s: Cell-index not specified, rc=%d\n",
			__func__, rc);
		goto error_no_mem;
	}

	if (index == 0)
		pdev->id = 1;
	else
		pdev->id = 2;

	mdss_dsi_mres = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mdss_dsi_mres) {
		pr_err("%s:%d unable to get the MDSS resources",
		       __func__, __LINE__);
		rc = -ENOMEM;
		goto error_no_mem;
	}

	mdss_dsi_base = ioremap(mdss_dsi_mres->start,
				resource_size(mdss_dsi_mres));
	if (!mdss_dsi_base) {
		pr_err("%s:%d unable to remap dsi resources",
		       __func__, __LINE__);
		rc = -ENOMEM;
		goto error_no_mem;
	}

	rc = of_platform_populate(pdev->dev.of_node,
				  NULL, NULL, &pdev->dev);
	if (rc) {
		dev_err(&pdev->dev,
			"%s: failed to add child nodes, rc=%d\n",
			__func__, rc);
		goto error_ioremap;
	}

	/* Parse the regulator information */
	rc = mdss_dsi_get_dt_vreg_data(&pdev->dev,
				       &ctrl_pdata->power_data);
	if (rc) {
		pr_err("%s: failed to get vreg data from dt. rc=%d\n",
		       __func__, rc);
		goto error_vreg;
	}

	/* DSI panels can be different between controllers */
	rc = mdss_dsi_get_panel_cfg(panel_cfg);
	if (!rc)
		/* dsi panel cfg not present */
		pr_warn("%s:%d:dsi specific cfg not present\n",
			__func__, __LINE__);

	/* find panel device node */
	dsi_pan_node = mdss_dsi_find_panel_of_node(pdev, panel_cfg);
	if (!dsi_pan_node) {
		pr_err("%s: can't find panel node %s\n", __func__, panel_cfg);
		goto error_pan_node;
	}

	cmd_cfg_cont_splash = mdss_panel_get_boot_cfg() ? true : false;

	rc = mdss_dsi_panel_init(dsi_pan_node, &vendor_pdata);
	if (rc) {
		pr_err("%s: dsi panel init failed\n", __func__);
		goto error_pan_node;
	}

	rc = dsi_panel_device_register(dsi_pan_node, &vendor_pdata,
				       cmd_cfg_cont_splash);
	if (rc) {
		pr_err("%s: dsi panel dev reg failed\n", __func__);
		goto error_pan_node;
	}

	pr_debug("%s: Dsi Ctrl->%d initialized\n", __func__, index);
	return 0;

error_pan_node:
	of_node_put(dsi_pan_node);
error_vreg:
	mdss_dsi_put_dt_vreg_data(&pdev->dev, &ctrl_pdata->power_data);
error_ioremap:
	iounmap(mdss_dsi_base);
error_no_mem:
	devm_kfree(&pdev->dev, ctrl_pdata);

	return rc;
}

static int __devexit mdss_dsi_ctrl_remove(struct platform_device *pdev)
{
	struct msm_fb_data_type *mfd;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = platform_get_drvdata(pdev);

	if (!ctrl_pdata) {
		pr_err("%s: no driver data\n", __func__);
		return -ENODEV;
	}

	if (msm_dss_config_vreg(&pdev->dev,
			ctrl_pdata->power_data.vreg_config,
			ctrl_pdata->power_data.num_vreg, 1) < 0)
		pr_err("%s: failed to de-init vregs\n", __func__);
	mdss_dsi_put_dt_vreg_data(&pdev->dev, &ctrl_pdata->power_data);
	mfd = platform_get_drvdata(pdev);
	msm_dss_iounmap(&ctrl_pdata->mmss_misc_io);
	iounmap(mdss_dsi_base);
	msm_dss_iounmap(&ctrl_pdata->phy_io);
	msm_dss_iounmap(&ctrl_pdata->ctrl_io);
	return 0;
}

struct device dsi_dev;

int mdss_dsi_retrieve_ctrl_resources(struct platform_device *pdev, int mode,
			struct mdss_dsi_ctrl_pdata *ctrl)
{
	int rc = 0;
	u32 index;

	rc = of_property_read_u32(pdev->dev.of_node, "cell-index", &index);
	if (rc) {
		dev_err(&pdev->dev,
			"%s: Cell-index not specified, rc=%d\n",
						__func__, rc);
		return rc;
	}

	if (index == 0) {
		if (mode != DISPLAY_1) {
			pr_err("%s:%d Panel->Ctrl mapping is wrong",
				       __func__, __LINE__);
			return -EPERM;
		}
	} else if (index == 1) {
		if (mode != DISPLAY_2) {
			pr_err("%s:%d Panel->Ctrl mapping is wrong",
				       __func__, __LINE__);
			return -EPERM;
		}
	} else {
		pr_err("%s:%d Unknown Ctrl mapped to panel",
			       __func__, __LINE__);
		return -EPERM;
	}

	rc = msm_dss_ioremap_byname(pdev, &ctrl->ctrl_io, "dsi_ctrl");
	if (rc) {
		pr_err("%s:%d unable to remap dsi ctrl resources",
			       __func__, __LINE__);
		return rc;
	}

	ctrl->ctrl_base = ctrl->ctrl_io.base;
	ctrl->reg_size = ctrl->ctrl_io.len;

	rc = msm_dss_ioremap_byname(pdev, &ctrl->phy_io, "dsi_phy");
	if (rc) {
		pr_err("%s:%d unable to remap dsi phy resources",
			       __func__, __LINE__);
		return rc;
	}

	pr_info("%s: ctrl_base=%p ctrl_size=%x phy_base=%p phy_size=%x\n",
		__func__, ctrl->ctrl_base, ctrl->reg_size, ctrl->phy_io.base,
		ctrl->phy_io.len);

	rc = msm_dss_ioremap_byname(pdev, &ctrl->mmss_misc_io,
		"mmss_misc_phys");
	if (rc) {
		pr_debug("%s:%d mmss_misc IO remap failed\n",
			__func__, __LINE__);
	}

	return 0;
}

int dsi_panel_device_register(struct device_node *pan_node,
			      struct mdss_panel_common_pdata *panel_data,
			      bool cmd_cfg_cont_splash)
{
	struct mipi_panel_info *mipi;
	int rc, i, len;
	u8 lanes = 0, bpp;
	u32 h_period, v_period, dsi_pclk_rate, tmp[9];
	struct mdss_dsi_ctrl_pdata *ctrl_pdata;
	struct device_node *dsi_ctrl_np = NULL;
	struct platform_device *ctrl_pdev = NULL;
	bool cont_splash_enabled = false;
	const char *data;

	h_period = ((panel_data->panel_info.lcdc.h_pulse_width)
			+ (panel_data->panel_info.lcdc.h_back_porch)
			+ (panel_data->panel_info.xres)
			+ (panel_data->panel_info.lcdc.h_front_porch));

	v_period = ((panel_data->panel_info.lcdc.v_pulse_width)
			+ (panel_data->panel_info.lcdc.v_back_porch)
			+ (panel_data->panel_info.yres)
			+ (panel_data->panel_info.lcdc.v_front_porch));

	mipi  = &panel_data->panel_info.mipi;

	panel_data->panel_info.type =
		((mipi->mode == DSI_VIDEO_MODE)
			? MIPI_VIDEO_PANEL : MIPI_CMD_PANEL);

	if (mipi->data_lane3)
		lanes += 1;
	if (mipi->data_lane2)
		lanes += 1;
	if (mipi->data_lane1)
		lanes += 1;
	if (mipi->data_lane0)
		lanes += 1;


	if ((mipi->dst_format == DSI_CMD_DST_FORMAT_RGB888)
	    || (mipi->dst_format == DSI_VIDEO_DST_FORMAT_RGB888)
	    || (mipi->dst_format == DSI_VIDEO_DST_FORMAT_RGB666_LOOSE))
		bpp = 3;
	else if ((mipi->dst_format == DSI_CMD_DST_FORMAT_RGB565)
		 || (mipi->dst_format == DSI_VIDEO_DST_FORMAT_RGB565))
		bpp = 2;
	else
		bpp = 3;		/* Default format set to RGB888 */

	if (!panel_data->panel_info.clk_rate) {
		h_period += panel_data->panel_info.lcdc.xres_pad;
		v_period += panel_data->panel_info.lcdc.yres_pad;

		if (lanes > 0) {
			panel_data->panel_info.clk_rate =
			((h_period * v_period * (mipi->frame_rate) * bpp * 8)
			   / lanes);
		} else {
			pr_err("%s: forcing mdss_dsi lanes to 1\n", __func__);
			panel_data->panel_info.clk_rate =
				(h_period * v_period
					 * (mipi->frame_rate) * bpp * 8);
		}
	}
	pll_divider_config.clk_rate = panel_data->panel_info.clk_rate;

	rc = mdss_dsi_clk_div_config(bpp, lanes, &dsi_pclk_rate);
	if (rc) {
		pr_err("%s: unable to initialize the clk dividers\n", __func__);
		return rc;
	}

	if ((dsi_pclk_rate < 3300000) || (dsi_pclk_rate > 250000000))
		dsi_pclk_rate = 35000000;
	mipi->dsi_pclk_rate = dsi_pclk_rate;

	dsi_ctrl_np = of_parse_phandle(pan_node,
				"qcom,mdss-dsi-panel-controller", 0);
	if (!dsi_ctrl_np) {
		pr_err("%s: Dsi controller node not initialized\n", __func__);
		return -EPROBE_DEFER;
	}

	ctrl_pdev = of_find_device_by_node(dsi_ctrl_np);
	ctrl_pdata = platform_get_drvdata(ctrl_pdev);
	if (!ctrl_pdata) {
		pr_err("%s: no dsi ctrl driver data\n", __func__);
		return -EINVAL;
	}

	rc = mdss_dsi_regulator_init(ctrl_pdev);
	if (rc) {
		pr_err("%s: failed to init regulator, rc=%d\n",
						__func__, rc);
		return rc;
	}

	data = of_get_property(ctrl_pdev->dev.of_node,
		"qcom,platform-strength-ctrl", &len);
	if ((!data) || (len != 2)) {
		pr_err("%s:%d, Unable to read Phy Strength ctrl settings",
			__func__, __LINE__);
		return -EINVAL;
	}
	(panel_data->panel_info.mipi.dsi_phy_db)->strength[0] = data[0];
	(panel_data->panel_info.mipi.dsi_phy_db)->strength[1] = data[1];

	data = of_get_property(ctrl_pdev->dev.of_node,
		"qcom,platform-regulator-settings", &len);
	if ((!data) || (len != 7)) {
		pr_err("%s:%d, Unable to read Phy regulator settings",
			__func__, __LINE__);
		return -EINVAL;
	}
	for (i = 0; i < len; i++) {
		(panel_data->panel_info.mipi.dsi_phy_db)->regulator[i]
			= data[i];
	}

	data = of_get_property(ctrl_pdev->dev.of_node,
		"qcom,platform-bist-ctrl", &len);
	if ((!data) || (len != 6)) {
		pr_err("%s:%d, Unable to read Phy Bist Ctrl settings",
			__func__, __LINE__);
		return -EINVAL;
	}
	for (i = 0; i < len; i++) {
		(panel_data->panel_info.mipi.dsi_phy_db)->bistCtrl[i]
			= data[i];
	}

	data = of_get_property(ctrl_pdev->dev.of_node,
		"qcom,platform-lane-config", &len);
	if ((!data) || (len != 45)) {
		pr_err("%s:%d, Unable to read Phy lane configure settings",
			__func__, __LINE__);
		return -EINVAL;
	}
	for (i = 0; i < len; i++) {
		(panel_data->panel_info.mipi.dsi_phy_db)->laneCfg[i] =
			data[i];
	}

	ctrl_pdata->shared_pdata.broadcast_enable = of_property_read_bool(
		pan_node, "qcom,mdss-dsi-panel-broadcast-mode");

	ctrl_pdata->disp_en_gpio = of_get_named_gpio(ctrl_pdev->dev.of_node,
		"qcom,platform-enable-gpio", 0);

	if (!gpio_is_valid(ctrl_pdata->disp_en_gpio))
		pr_err("%s:%d, Disp_en gpio not specified\n",
						__func__, __LINE__);

	if (panel_data->panel_info.type == MIPI_CMD_PANEL) {
		ctrl_pdata->disp_te_gpio = of_get_named_gpio(ctrl_pdev->dev.of_node,
						"qcom,platform-te-gpio", 0);
		if (!gpio_is_valid(ctrl_pdata->disp_te_gpio)) {
			pr_err("%s:%d, Disp_te gpio not specified\n",
						__func__, __LINE__);
		}
	}

	if (gpio_is_valid(ctrl_pdata->disp_te_gpio)) {
		rc = gpio_request(ctrl_pdata->disp_te_gpio, "disp_te");
		if (rc) {
			pr_err("request TE gpio failed, rc=%d\n",
			       rc);
			return -ENODEV;
		}
		rc = gpio_tlmm_config(GPIO_CFG(
				ctrl_pdata->disp_te_gpio, 1,
				GPIO_CFG_INPUT,
				GPIO_CFG_PULL_DOWN,
				GPIO_CFG_2MA),
				GPIO_CFG_ENABLE);

		if (rc) {
			pr_err("%s: unable to config tlmm = %d\n",
				__func__, ctrl_pdata->disp_te_gpio);
			gpio_free(ctrl_pdata->disp_te_gpio);
			return -ENODEV;
		}

		rc = gpio_direction_input(ctrl_pdata->disp_te_gpio);
		if (rc) {
			pr_err("set_direction for disp_en gpio failed, rc=%d\n",
			       rc);
			gpio_free(ctrl_pdata->disp_te_gpio);
			return -ENODEV;
		}
		pr_debug("%s: te_gpio=%d\n", __func__,
					ctrl_pdata->disp_te_gpio);
	}

	rc = of_property_read_u32_array(ctrl_pdev->dev.of_node,
		"qcom,platform-reset-sequence", tmp, MDSS_DSI_RST_SEQ_LEN);
	if (rc)
		pr_err("%s:%d, unable to read gpio reset sequence\n",
						__func__, __LINE__);
	else
		for (i = 0; i < MDSS_DSI_RST_SEQ_LEN; ++i)
			ctrl_pdata->rst_seq[i] = tmp[i];

	ctrl_pdata->rst_gpio = of_get_named_gpio(ctrl_pdev->dev.of_node,
			 "qcom,platform-reset-gpio", 0);
	if (!gpio_is_valid(ctrl_pdata->rst_gpio))
		pr_err("%s:%d, reset gpio not specified\n",
						__func__, __LINE__);

	if (mdss_dsi_clk_init(ctrl_pdev, ctrl_pdata)) {
		pr_err("%s: unable to initialize Dsi ctrl clks\n", __func__);
		return -EPERM;
	}

	if (mdss_dsi_retrieve_ctrl_resources(ctrl_pdev,
					     panel_data->panel_info.pdest,
					     ctrl_pdata)) {
		pr_err("%s: unable to get Dsi controller res\n", __func__);
		return -EPERM;
	}

	ctrl_pdata->panel_data.event_handler = mdss_dsi_event_handler;
	ctrl_pdata->check_status = mdss_dsi_bta_status_check;

	ctrl_pdata->on_cmds = panel_data->on_cmds;
	ctrl_pdata->off_cmds = panel_data->off_cmds;

	memcpy(&((ctrl_pdata->panel_data).panel_info),
				&(panel_data->panel_info),
				       sizeof(struct mdss_panel_info));

	ctrl_pdata->panel_data.set_backlight = panel_data->bl_fnc;
	ctrl_pdata->bklt_ctrl = panel_data->panel_info.bklt_ctrl;
	ctrl_pdata->pwm_pmic_gpio = panel_data->panel_info.pwm_pmic_gpio;
	ctrl_pdata->pwm_period = panel_data->panel_info.pwm_period;
	ctrl_pdata->pwm_lpg_chan = panel_data->panel_info.pwm_lpg_chan;
	ctrl_pdata->bklt_max = panel_data->panel_info.bl_max;

	if (ctrl_pdata->bklt_ctrl == BL_PWM)
		mdss_dsi_panel_pwm_cfg(ctrl_pdata);

	mdss_dsi_ctrl_init(ctrl_pdata);
	/*
	 * register in mdp driver
	 */

	ctrl_pdata->pclk_rate = dsi_pclk_rate;
	ctrl_pdata->byte_clk_rate = panel_data->panel_info.clk_rate / 8;
	pr_debug("%s: pclk=%d, bclk=%d\n", __func__,
			ctrl_pdata->pclk_rate, ctrl_pdata->byte_clk_rate);

	ctrl_pdata->ctrl_state = CTRL_STATE_UNKNOWN;
	if (cmd_cfg_cont_splash)
		cont_splash_enabled = of_property_read_bool(pan_node,
				"qcom,cont-splash-enabled");
	else
		cont_splash_enabled = false;
	if (!cont_splash_enabled) {
		pr_info("%s:%d Continuous splash flag not found.\n",
				__func__, __LINE__);
		ctrl_pdata->panel_data.panel_info.cont_splash_enabled = 0;
		ctrl_pdata->panel_data.panel_info.panel_power_on = 0;
	} else {
		pr_info("%s:%d Continuous splash flag enabled.\n",
				__func__, __LINE__);

		ctrl_pdata->panel_data.panel_info.cont_splash_enabled = 1;
		ctrl_pdata->panel_data.panel_info.panel_power_on = 1;
		rc = mdss_dsi_panel_power_on(&(ctrl_pdata->panel_data), 1);
		if (rc) {
			pr_err("%s: Panel power on failed\n", __func__);
			return rc;
		}

		mdss_dsi_clk_ctrl(ctrl_pdata, DSI_ALL_CLKS, 1);
		ctrl_pdata->ctrl_state |=
			(CTRL_STATE_PANEL_INIT | CTRL_STATE_MDP_ACTIVE);
	}

	rc = mdss_register_panel(ctrl_pdev, &(ctrl_pdata->panel_data));
	if (rc) {
		pr_err("%s: unable to register MIPI DSI panel\n", __func__);
		return rc;
	}

	ctrl_pdata->on = panel_data->on;
	ctrl_pdata->off = panel_data->off;

	if (panel_data->panel_info.pdest == DISPLAY_1) {
		mdss_debug_register_base("dsi0",
			ctrl_pdata->ctrl_base, ctrl_pdata->reg_size);
		ctrl_pdata->ndx = 0;
	} else {
		mdss_debug_register_base("dsi1",
			ctrl_pdata->ctrl_base, ctrl_pdata->reg_size);
		ctrl_pdata->ndx = 1;
	}

	ctrl_pdata->partial_update_fnc = panel_data->partial_update_fnc;

	pr_debug("%s: Panel data initialized\n", __func__);
	return 0;
}

static const struct of_device_id mdss_dsi_ctrl_dt_match[] = {
	{.compatible = "qcom,mdss-dsi-ctrl"},
	{}
};
MODULE_DEVICE_TABLE(of, mdss_dsi_ctrl_dt_match);

static struct platform_driver mdss_dsi_ctrl_driver = {
	.probe = mdss_dsi_ctrl_probe,
	.remove = __devexit_p(mdss_dsi_ctrl_remove),
	.shutdown = NULL,
	.driver = {
		.name = "mdss_dsi_ctrl",
		.of_match_table = mdss_dsi_ctrl_dt_match,
	},
};

static int mdss_dsi_register_driver(void)
{
	return platform_driver_register(&mdss_dsi_ctrl_driver);
}

static int __init mdss_dsi_driver_init(void)
{
	int ret;

	ret = mdss_dsi_register_driver();
	if (ret) {
		pr_err("mdss_dsi_register_driver() failed!\n");
		return ret;
	}

	return ret;
}
module_init(mdss_dsi_driver_init);

static void __exit mdss_dsi_driver_cleanup(void)
{
	iounmap(mdss_dsi_base);
	platform_driver_unregister(&mdss_dsi_ctrl_driver);
}
module_exit(mdss_dsi_driver_cleanup);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DSI controller driver");
MODULE_AUTHOR("Chandan Uddaraju <chandanu@codeaurora.org>");
