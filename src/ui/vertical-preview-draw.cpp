/*
DualStream Relay for OBS
Copyright (C) 2026 Dual Stream Studio Inc <hello@dualstream.gg>

SPDX-License-Identifier: GPL-2.0-or-later

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

/* Everything the preview draws on top of the canvas: the overflow hatch for a
 * source that reaches past the frame, and the selection box with its resize
 * handles. Ported from the routines OBS uses on its own preview so the two
 * read identically, down to the handle size and the shared overflow texture. */

#include "vertical-preview.hpp"

#include <QColor>

#include <graphics/matrix4.h>
#include <graphics/vec3.h>
#include <graphics/vec4.h>

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>
#include <util/config-file.h>

#include "../vertical-canvas.hpp"
#include "vertical-common.hpp"

namespace {

/* OBSBasicPreview's own constants. */
const float kHandleRadius = 4.0f;

/* Canvas units per hatch tile, the divisor OBSBasicPreview uses. Keeping its
 * formula means the pitch tracks how far the preview is zoomed in, exactly as
 * OBS's does. */
const float kOverflowTile = 96.0f;

/* Grey the hatch stripes are drawn in. OBS arrives at its own by washing white
 * over its grey preview background; this draws the result directly, because
 * the wash has nothing to lift on a near-black surround. */
const int kHatchGrey = 0x80;

const float kSpacingLabelMargin = 6.0f;

/* Selection and hover colours, honouring the accessibility override the same
 * way OBSBasic::GetSelectionColor and GetHoverColor do, so a user who has
 * changed them sees the change here too. */
struct vec4 markerColor(bool selected)
{
	QColor colour = selected ? QColor::fromRgb(255, 0, 0) : QColor::fromRgb(0, 127, 255);

	config_t *config = obs_frontend_get_user_config();
	if (config && config_get_bool(config, "Accessibility", "OverrideColors")) {
		const long long packed = config_get_int(config, "Accessibility", selected ? "SelectRed" : "SelectBlue");
		colour = QColor(packed & 0xff, (packed >> 8) & 0xff, (packed >> 16) & 0xff);
	}

	struct vec4 out;
	vec4_set(&out, (float)colour.redF(), (float)colour.greenF(), (float)colour.blueF(), 1.0f);
	return out;
}

QColor cropMarkerColor()
{
	config_t *config = obs_frontend_get_user_config();
	if (config && config_get_bool(config, "Accessibility", "OverrideColors")) {
		const long long packed = config_get_int(config, "Accessibility", "SelectGreen");
		return QColor(packed & 0xff, (packed >> 8) & 0xff, (packed >> 16) & 0xff);
	}
	return QColor::fromRgb(0, 255, 0);
}

struct vec3 transformedCorner(float x, float y, const struct matrix4 &transform)
{
	struct vec3 pos;
	vec3_set(&pos, x, y, 0.0f);
	vec3_transform(&pos, &pos, &transform);
	return pos;
}

/* A line of constant on-screen thickness inside a transformed unit box. */
void drawLine(float x1, float y1, float x2, float y2, float thickness, struct vec2 scale)
{
	struct vec2 relative;
	vec2_abs(&scale, &scale);
	relative.x = thickness / scale.x;
	relative.y = thickness / scale.y;

	const bool horizontal = fabsf(y2 - y1) < fabsf(x2 - x1) || fabsf(y2 - y1) < 0.0001f;
	const float cx = horizontal ? fabsf(x2 - x1) + relative.x : relative.x;
	const float cy = horizontal ? relative.y : fabsf(y2 - y1) + relative.y;

	gs_matrix_push();
	gs_matrix_translate3f(x1 - relative.x * 0.5f, y1 - relative.y * 0.5f, 0.0f);
	gs_draw_quadf(NULL, 0, cx, cy);
	gs_matrix_pop();
}

void drawBorder(float thickness, struct vec2 scale)
{
	drawLine(0.0f, 0.0f, 0.0f, 1.0f, thickness, scale);
	drawLine(0.0f, 0.0f, 1.0f, 0.0f, thickness, scale);
	drawLine(1.0f, 0.0f, 1.0f, 1.0f, thickness, scale);
	drawLine(0.0f, 1.0f, 1.0f, 1.0f, thickness, scale);
}

/* Square handle centred on a point of the unit box, sized in canvas units so
 * it stays the same size on screen at any zoom. */
void drawHandle(float x, float y, float pixelRatio, gs_vertbuffer_t *quad)
{
	struct vec3 pos;
	struct matrix4 matrix;
	vec3_set(&pos, x, y, 0.0f);
	gs_matrix_get(&matrix);
	vec3_transform(&pos, &pos, &matrix);

	const float side = kHandleRadius * pixelRatio * 2.0f;

	gs_matrix_push();
	gs_matrix_identity();
	gs_matrix_translate3f(pos.x - side / 2.0f, pos.y - side / 2.0f, 0.0f);
	gs_matrix_scale3f(side, side, 1.0f);
	gs_load_vertexbuffer(quad);
	gs_draw(GS_TRISTRIP, 0, 0);
	gs_matrix_pop();
}

/* A dashed line of the same pitch OBS uses: fifteen segments along the run,
 * driven by the striped_line effect it ships. */
void drawStripedLine(gs_effect_t *effect, float x1, float y1, float x2, float y2, float thickness, struct vec2 scale)
{
	struct vec2 relative;
	vec2_abs(&scale, &scale);
	relative.x = thickness / scale.x;
	relative.y = thickness / scale.y;

	const bool horizontal = fabsf(y2 - y1) < fabsf(x2 - x1) || fabsf(y2 - y1) < 0.0001f;
	const float dist = horizontal ? fabsf(x2 - x1) : fabsf(y2 - y1);
	const float distScaled = dist * (horizontal ? scale.x : scale.y);
	const float cx = horizontal ? dist + relative.x : relative.x;
	const float cy = horizontal ? relative.y : dist + relative.y;

	struct vec2 size;
	struct vec2 countInv;
	const float stripeLength = distScaled / 15.0f;
	const float stripesInv = stripeLength > 0.0f ? 1.0f / (distScaled / stripeLength) : 0.0f;
	if (horizontal) {
		vec2_set(&size, distScaled, 0.0f);
		vec2_set(&countInv, stripesInv, 0.0f);
	} else {
		vec2_set(&size, 0.0f, distScaled);
		vec2_set(&countInv, 0.0f, stripesInv);
	}

	gs_effect_set_vec2(gs_effect_get_param_by_name(effect, "size"), &size);
	gs_effect_set_vec2(gs_effect_get_param_by_name(effect, "count_inv"), &countInv);

	gs_matrix_push();
	gs_matrix_translate3f(x1 - relative.x * 0.5f, y1 - relative.y * 0.5f, 0.0f);
	while (gs_effect_loop(effect, "StripedLine"))
		gs_draw_quadf(nullptr, 0, cx, cy);
	gs_matrix_pop();
}

} // namespace

/* Diagonal hatch over the selected item's whole box. Drawn before the canvas
 * fill, so only the part reaching past the frame survives, which is what makes
 * it read as out of bounds. */
void VerticalPreview::drawOverflow(obs_sceneitem_t *item)
{
	if (!overflowTexture) {
		/* OBS's own hatch, carried in this plugin's data directory.
		 * obs_find_data_file cannot reach the frontend's copy: it only
		 * searches libobs's data path, and the frontend never registers
		 * its own. */
		char *path = obs_module_file("images/overflow.png");
		if (!path)
			return;
		overflowTexture = gs_texture_create_from_file(path);
		if (!overflowTexture)
			obs_log(LOG_WARNING, "overflow texture failed to load from %s", path);
		bfree(path);
		if (!overflowTexture)
			return;
	}

	if (!overflowEffect) {
		char *path = obs_module_file("effects/overflow.effect");
		if (!path)
			return;
		char *errors = nullptr;
		overflowEffect = gs_effect_create_from_file(path, &errors);
		if (!overflowEffect)
			obs_log(LOG_WARNING, "overflow effect failed to compile: %s", errors ? errors : "unknown");
		bfree(errors);
		bfree(path);
		if (!overflowEffect)
			return;
	}

	struct matrix4 boxTransform;
	obs_sceneitem_get_box_transform(item, &boxTransform);

	struct vec2 tiling;
	vec2_set(&tiling, fabsf(boxTransform.x.x) / kOverflowTile, fabsf(boxTransform.y.y) / kOverflowTile);
	gs_effect_set_vec2(gs_effect_get_param_by_name(overflowEffect, "scale"), &tiling);
	gs_effect_set_texture(gs_effect_get_param_by_name(overflowEffect, "image"), overflowTexture);

	struct vec4 stripe;
	vec4_set(&stripe, kHatchGrey / 255.0f, kHatchGrey / 255.0f, kHatchGrey / 255.0f, 1.0f);
	gs_effect_set_vec4(gs_effect_get_param_by_name(overflowEffect, "color"), &stripe);

	gs_matrix_push();
	gs_matrix_mul(&boxTransform);
	while (gs_effect_loop(overflowEffect, "Draw"))
		gs_draw_sprite(overflowTexture, 0, 1, 1);
	gs_matrix_pop();
}

/* One side of the selection box: dashed and green where a crop is in effect,
 * solid in the selection colour otherwise. */
void VerticalPreview::drawCropSide(bool cropped, float x1, float y1, float x2, float y2, float thickness,
				   struct vec2 boxScale, const struct vec4 &colour)
{
	if (cropped && !stripedEffect) {
		char *path = obs_module_file("effects/striped-line.effect");
		if (path) {
			char *errors = nullptr;
			stripedEffect = gs_effect_create_from_file(path, &errors);
			if (!stripedEffect)
				obs_log(LOG_WARNING, "striped line effect failed to compile: %s",
					errors ? errors : "unknown");
			bfree(errors);
			bfree(path);
		}
	}

	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);

	if (!cropped || !stripedEffect) {
		gs_effect_set_vec4(gs_effect_get_param_by_name(solid, "color"), &colour);
		while (gs_effect_loop(solid, "Solid"))
			drawLine(x1, y1, x2, y2, thickness, boxScale);
		return;
	}

	struct vec4 green;
	const QColor cropColour = cropMarkerColor();
	vec4_set(&green, (float)cropColour.redF(), (float)cropColour.greenF(), (float)cropColour.blueF(), 1.0f);
	gs_effect_set_vec4(gs_effect_get_param_by_name(stripedEffect, "color"), &green);
	drawStripedLine(stripedEffect, x1, y1, x2, y2, thickness, boxScale);
}

/* Border plus the eight resize handles, in the same positions and the same
 * colour OBS uses. */
void VerticalPreview::drawSelection(obs_sceneitem_t *item, bool selected, float pixelRatio)
{
	struct matrix4 boxTransform;
	struct matrix4 curTransform;
	struct vec2 boxScale;
	obs_sceneitem_get_box_transform(item, &boxTransform);
	obs_sceneitem_get_box_scale(item, &boxScale);
	/* The caller has already scaled canvas units into display pixels, so
	 * fold that in: the border thickness is measured in the same space. */
	gs_matrix_get(&curTransform);
	boxScale.x *= curTransform.x.x;
	boxScale.y *= curTransform.y.y;

	struct obs_sceneitem_crop crop;
	obs_sceneitem_get_crop(item, &crop);
	const bool cropped = selected && obs_sceneitem_get_bounds_type(item) == OBS_BOUNDS_NONE &&
			     (crop.left > 0 || crop.top > 0 || crop.right > 0 || crop.bottom > 0);

	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	const struct vec4 colour = markerColor(selected);
	const float thickness = kHandleRadius * pixelRatio / 2.0f;

	gs_matrix_push();
	gs_matrix_mul(&boxTransform);

	/* Sides first, each running its own technique, then the handles in
	 * one. Drawing a side inside an already open technique nests them,
	 * which is why the dashed edges never appeared. */
	if (cropped) {
		drawCropSide(crop.left > 0, 0.0f, 0.0f, 0.0f, 1.0f, thickness, boxScale, colour);
		drawCropSide(crop.top > 0, 0.0f, 0.0f, 1.0f, 0.0f, thickness, boxScale, colour);
		drawCropSide(crop.right > 0, 1.0f, 0.0f, 1.0f, 1.0f, thickness, boxScale, colour);
		drawCropSide(crop.bottom > 0, 0.0f, 1.0f, 1.0f, 1.0f, thickness, boxScale, colour);
	} else {
		gs_effect_set_vec4(gs_effect_get_param_by_name(solid, "color"), &colour);
		while (gs_effect_loop(solid, "Solid"))
			drawBorder(thickness, boxScale);
	}

	if (selected) {
		gs_effect_set_vec4(gs_effect_get_param_by_name(solid, "color"), &colour);
		gs_technique_t *tech = gs_effect_get_technique(solid, "Solid");
		gs_technique_begin(tech);
		gs_technique_begin_pass(tech, 0);

		const float handles[8][2] = {{0.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 0.0f}, {1.0f, 1.0f},
					     {0.5f, 0.0f}, {0.0f, 0.5f}, {0.5f, 1.0f}, {1.0f, 0.5f}};
		for (const float *handle : handles)
			drawHandle(handle[0], handle[1], pixelRatio, quad);

		gs_technique_end_pass(tech);
		gs_technique_end(tech);
	}

	gs_matrix_pop();
	gs_load_vertexbuffer(nullptr);
}

/* Distance from each side of the item to the matching canvas edge, drawn as a
 * line with a "N px" label, the way OBS's DrawSpacingHelpers does. Rotation is
 * not handled: nothing in this dock can rotate an item, so the four sides are
 * always the four sides. */
void VerticalPreview::drawSpacingHelpers(obs_sceneitem_t *item, float viewWidth, float viewHeight, float pixelRatio)
{
	struct matrix4 boxTransform;
	obs_sceneitem_get_box_transform(item, &boxTransform);

	const struct vec3 left = transformedCorner(0.0f, 0.5f, boxTransform);
	const struct vec3 right = transformedCorner(1.0f, 0.5f, boxTransform);
	const struct vec3 top = transformedCorner(0.5f, 0.0f, boxTransform);
	const struct vec3 bottom = transformedCorner(0.5f, 1.0f, boxTransform);

	/* Gap in canvas units on each side; negative means the item overhangs
	 * that edge, and OBS draws nothing for it. */
	const struct {
		float gap;
		float x1, y1, x2, y2;
		bool horizontal;
	} sides[4] = {
		{top.y, top.x, 0.0f, top.x, top.y, false},
		{(float)kPortraitHeight - bottom.y, bottom.x, bottom.y, bottom.x, (float)kPortraitHeight, false},
		{left.x, 0.0f, left.y, left.x, left.y, true},
		{(float)kPortraitWidth - right.x, right.x, right.y, (float)kPortraitWidth, right.y, true},
	};

	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_technique_t *tech = gs_effect_get_technique(solid, "Solid");
	const struct vec4 colour = markerColor(true);

	/* Lines are drawn in canvas units, so the thickness has to be scaled
	 * back out of them to land at a constant size on screen. */
	const float canvasPerPixelX = (float)kPortraitWidth / viewWidth;
	const float canvasPerPixelY = (float)kPortraitHeight / viewHeight;

	for (int i = 0; i < 4; i++) {
		if (sides[i].gap <= 0.0f)
			continue;

		/* Set per line, not once: rendering a label between them runs
		 * its own effects and leaves this parameter dirty. */
		gs_effect_set_vec4(gs_effect_get_param_by_name(solid, "color"), &colour);
		gs_technique_begin(tech);
		gs_technique_begin_pass(tech, 0);
		struct vec2 scale;
		vec2_set(&scale, 1.0f / canvasPerPixelX, 1.0f / canvasPerPixelY);
		drawLine(sides[i].x1, sides[i].y1, sides[i].x2, sides[i].y2, kHandleRadius * pixelRatio / 2.0f, scale);
		gs_technique_end_pass(tech);
		gs_technique_end(tech);

		if (!spacingLabel[i]) {
			obs_data_t *settings = obs_data_create();
			obs_data_t *font = obs_data_create();
#if defined(_WIN32)
			obs_data_set_string(font, "face", "Arial");
#elif defined(__APPLE__)
			obs_data_set_string(font, "face", "Helvetica");
#else
			obs_data_set_string(font, "face", "Monospace");
#endif
			obs_data_set_int(font, "flags", 1);
			obs_data_set_int(font, "size", (int)(16 * pixelRatio));
			obs_data_set_obj(settings, "font", font);
			obs_data_set_bool(settings, "outline", true);
#ifdef _WIN32
			obs_data_set_int(settings, "outline_color", 0x000000);
			obs_data_set_int(settings, "outline_size", 3);
			const char *textId = "text_gdiplus";
#else
			const char *textId = "text_ft2_source";
#endif
			char name[64];
			snprintf(name, sizeof(name), "dsr vertical spacing %d", i);
			spacingLabel[i] = obs_source_create_private(textId, name, settings);
			obs_data_release(font);
			obs_data_release(settings);
		}
		if (!spacingLabel[i])
			continue;

		const int px = (int)(sides[i].horizontal ? sides[i].gap : sides[i].gap);
		if (px != spacingPx[i]) {
			obs_data_t *settings = obs_source_get_settings(spacingLabel[i]);
			char text[32];
			snprintf(text, sizeof(text), "%d px", px);
			obs_data_set_string(settings, "text", text);
			obs_source_update(spacingLabel[i], settings);
			obs_data_release(settings);
			spacingPx[i] = px;
		}

		/* The label is rendered at its own pixel size, so it is placed
		 * in canvas units and scaled back down to stay legible. */
		const float labelW = (float)obs_source_get_width(spacingLabel[i]) * canvasPerPixelX;
		const float labelH = (float)obs_source_get_height(spacingLabel[i]) * canvasPerPixelY;
		const float margin = kSpacingLabelMargin * pixelRatio;

		float labelX;
		float labelY;
		if (sides[i].horizontal) {
			labelX = (sides[i].x1 + sides[i].x2) / 2.0f - labelW / 2.0f;
			labelY = sides[i].y1 - labelH - margin * canvasPerPixelY;
		} else {
			labelX = sides[i].x1 + margin * canvasPerPixelX;
			labelY = (sides[i].y1 + sides[i].y2) / 2.0f - labelH / 2.0f;
		}

		gs_matrix_push();
		gs_matrix_translate3f(labelX, labelY, 0.0f);
		gs_matrix_scale3f(canvasPerPixelX, canvasPerPixelY, 1.0f);
		obs_source_video_render(spacingLabel[i]);
		gs_matrix_pop();
	}

	gs_load_vertexbuffer(nullptr);
}

void VerticalPreview::releaseSpacingLabels()
{
	for (int i = 0; i < 4; i++) {
		if (spacingLabel[i]) {
			obs_source_release(spacingLabel[i]);
			spacingLabel[i] = nullptr;
		}
		spacingPx[i] = -1;
	}
}
