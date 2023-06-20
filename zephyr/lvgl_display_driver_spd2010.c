/*
 * Copyright (c) 2023 ListenAI
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 说明：
 * 		1. 此驱动程序用于SPD2010,此屏在x方向上需要按照4像素点对齐的方式写入数据
 * 		2. 在横屏竖用时，不要在lvgl层进行软件旋转，由驱动层进行旋转加速即可
 *
 */

#include "lvgl_display.h"

#include <zephyr/kernel.h>

#include <lvgl.h>

struct flush_msq {
	lv_area_t area;
	lv_color_t *color_p;
	const struct device *display_dev;
	lv_disp_drv_t *disp_drv;
};

#ifdef CONFIG_LV_Z_DOUBLE_VDB
K_MSGQ_DEFINE(flush_msgq, sizeof(struct flush_msq), 2, 4);

static void flush_thread(void *p1, void *p2, void *p3)
{
	struct flush_msq msg;
	while (1) {
		k_msgq_get(&flush_msgq, &msg, K_FOREVER);
		uint16_t w = msg.area.x2 - msg.area.x1 + 1;
		uint16_t h = msg.area.y2 - msg.area.y1 + 1;
		struct display_buffer_descriptor desc;

		desc.buf_size = w * 2U * h;
		desc.width = w;
		desc.pitch = w;
		desc.height = h;
		display_write(msg.display_dev, msg.area.x1, msg.area.y1, &desc,
			      (void *)msg.color_p);
		lv_disp_flush_ready(msg.disp_drv);
	}
}

K_THREAD_DEFINE(flush_thread_id, 1024, flush_thread, NULL, NULL, NULL, CONFIG_LV_Z_DRIVER_SPD2010_THREAD_PRIORITY, 0, 0);
#endif

void lvgl_flush_cb_16bit_spd2010(lv_disp_drv_t *disp_drv, const lv_area_t *area,
				 lv_color_t *color_p)
{
	const struct device *display_dev = (const struct device *)disp_drv->user_data;
#if !CONFIG_LV_Z_DOUBLE_VDB
	uint16_t w = area->x2 - area->x1 + 1;
	uint16_t h = area->y2 - area->y1 + 1;
	struct display_buffer_descriptor desc;

	desc.buf_size = w * 2U * h;
	desc.width = w;
	desc.pitch = w;
	desc.height = h;
	display_write(display_dev, area->x1, area->y1, &desc, (void *)color_p);
	lv_disp_flush_ready(disp_drv);
#else
	struct flush_msq msg;

	msg.area.x1 = area->x1;
	msg.area.x2 = area->x2;
	msg.area.y1 = area->y1;
	msg.area.y2 = area->y2;

	msg.display_dev = display_dev;
	msg.color_p = color_p;
	msg.disp_drv = disp_drv;

	k_msgq_put(&flush_msgq, &msg, K_FOREVER);
#endif
}

#ifndef CONFIG_LV_COLOR_DEPTH_16
void lvgl_set_px_cb_16bit_spd2010(lv_disp_drv_t *disp_drv, uint8_t *buf, lv_coord_t buf_w, lv_coord_t x,
			  lv_coord_t y, lv_color_t color, lv_opa_t opa)
{
	uint16_t *buf_xy = (uint16_t *)(buf + x * 2U + y * 2U * buf_w);
	*buf_xy = lv_color_to16(color);
}
#endif

void lvgl_rounder_cb_16bit_spd2010(struct _lv_disp_drv_t *disp_drv, lv_area_t *area)
{
	if (disp_drv->rotated == LV_DISP_ROT_90 || disp_drv->rotated == LV_DISP_ROT_270) {
		area->y1 = ROUND_DOWN(area->y1, 4);
		area->y2 = ROUND_UP(area->y2, 4) - 1;
		area->x1 = ROUND_DOWN(area->x1, 4);
		area->x2 = ROUND_UP(area->x2, 4) - 1;
	} else if (disp_drv->rotated == LV_DISP_ROT_NONE) {
		area->x1 = ROUND_DOWN(area->x1, 4);
		area->x2 = ROUND_UP(area->x2, 4) - 1;
	} else if (disp_drv->rotated == LV_DISP_ROT_180) {
		area->x1 = ROUND_DOWN(area->x1, 4);
		area->x2 = ROUND_UP(area->x2, 4) - 1;
	}
}

int set_lvgl_rendering_cb(lv_disp_drv_t *disp_drv)
{
	int err = 0;
	const struct device *display_dev = (const struct device *)disp_drv->user_data;
	struct display_capabilities cap;

	display_get_capabilities(display_dev, &cap);

#if CONFIG_LV_Z_DRIVER_SPD2010_FULL_REFRESH
	disp_drv->full_refresh = 1;
#endif

#if CONFIG_LV_Z_DRIVER_SPD2010_ROTATE_90
	disp_drv->rotated = LV_DISP_ROT_90;
#endif

	switch (cap.current_pixel_format) {
	case PIXEL_FORMAT_RGB_565:
	case PIXEL_FORMAT_BGR_565:
		disp_drv->flush_cb = lvgl_flush_cb_16bit_spd2010;
		disp_drv->rounder_cb = lvgl_rounder_cb_16bit_spd2010;
#ifdef CONFIG_LV_COLOR_DEPTH_16
		disp_drv->set_px_cb = NULL;
#else
		disp_drv->set_px_cb = lvgl_set_px_cb_16bit_spd2010;
#endif
		break;
	default:
		disp_drv->flush_cb = NULL;
		disp_drv->rounder_cb = NULL;
		disp_drv->set_px_cb = NULL;
		err = -ENOTSUP;
		break;
	}

	return err;
}
