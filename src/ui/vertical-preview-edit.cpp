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

/* Stretching and cropping, ported from OBSBasicPreview so the two previews
 * behave the same under the hand: eight handles, aspect held unless shift is
 * down, and alt to crop instead of scale.
 *
 * Rotation and groups are deliberately absent. Nothing in this dock can rotate
 * an item or nest one in a group, so the branches OBS carries for those would
 * be dead code here. */

#include "vertical-preview.hpp"

#include <QGuiApplication>

#include <algorithm>
#include <cmath>

#include <graphics/matrix4.h>
#include <graphics/vec3.h>

#include "vertical-common.hpp"

namespace {

/* Grab radius for a handle, HANDLE_SEL_RADIUS in OBSBasicPreview: the squares
 * are drawn at radius 4 and taken within 1.5 times that. */
const float kHandleGrabPx = 6.0f;

/* Size of the item on the canvas, after cropping, the way GetItemSize does. */
struct vec2 itemSize(obs_sceneitem_t *item)
{
	struct vec2 size;

	if (obs_sceneitem_get_bounds_type(item) != OBS_BOUNDS_NONE) {
		obs_sceneitem_get_bounds(item, &size);
		return size;
	}

	obs_source_t *source = obs_sceneitem_get_source(item);
	struct obs_sceneitem_crop crop;
	struct vec2 scale;
	obs_sceneitem_get_scale(item, &scale);
	obs_sceneitem_get_crop(item, &crop);
	size.x = fmaxf((float)((int)obs_source_get_width(source) - crop.left - crop.right), 0.0f);
	size.y = fmaxf((float)((int)obs_source_get_height(source) - crop.top - crop.bottom), 0.0f);
	vec2_mul(&size, &size, &scale);
	return size;
}

/* Where the item's origin lands for a given box, honouring its alignment.
 * CalculateStretchPos in OBS. */
struct vec3 stretchPos(obs_sceneitem_t *item, const struct vec3 &tl, const struct vec3 &br)
{
	const uint32_t alignment = obs_sceneitem_get_alignment(item);
	struct vec3 pos;
	vec3_zero(&pos);

	if (alignment & OBS_ALIGN_LEFT)
		pos.x = tl.x;
	else if (alignment & OBS_ALIGN_RIGHT)
		pos.x = br.x;
	else
		pos.x = (br.x - tl.x) * 0.5f + tl.x;

	if (alignment & OBS_ALIGN_TOP)
		pos.y = tl.y;
	else if (alignment & OBS_ALIGN_BOTTOM)
		pos.y = br.y;
	else
		pos.y = (br.y - tl.y) * 0.5f + tl.y;

	return pos;
}

/* Hold the source's own aspect. OBS applies this unless shift is held, and
 * which axis follows which depends on the handle. */
void clampAspect(uint32_t handle, struct vec2 &size, const struct vec2 &baseSize)
{
	if (baseSize.x == 0.0f || baseSize.y == 0.0f || size.y == 0.0f)
		return;

	const float baseAspect = baseSize.x / baseSize.y;
	const float aspect = size.x / size.y;
	const bool sameSign = (size.y >= 0.0f && size.x >= 0.0f) || (size.y <= 0.0f && size.x <= 0.0f);
	const float sign = sameSign ? 1.0f : -1.0f;

	const bool horizontal = (handle & DSR_ITEM_LEFT) || (handle & DSR_ITEM_RIGHT);
	const bool vertical = (handle & DSR_ITEM_TOP) || (handle & DSR_ITEM_BOTTOM);

	if (horizontal && vertical) {
		if (aspect < baseAspect)
			size.x = size.y * baseAspect * sign;
		else
			size.y = size.x / baseAspect * sign;
	} else if (vertical) {
		size.x = size.y * baseAspect * sign;
	} else if (horizontal) {
		size.y = size.x / baseAspect * sign;
	}
}

} // namespace

/* Which handle sits under a canvas point, as a bitmask of the four sides.
 * Zero when the point is not on one. */
uint32_t VerticalPreview::handleAt(obs_sceneitem_t *item, const QPointF &canvasPos) const
{
	const float widgetScale = qMin((float)width() / kPortraitWidth, (float)height() / kPortraitHeight);
	if (widgetScale <= 0.0f)
		return 0;
	const double tolerance = kHandleGrabPx / widgetScale;

	struct matrix4 boxTransform;
	obs_sceneitem_get_box_transform(item, &boxTransform);

	const struct {
		float x, y;
		uint32_t flags;
	} handles[8] = {
		{0.0f, 0.0f, DSR_ITEM_TOP | DSR_ITEM_LEFT},
		{0.5f, 0.0f, DSR_ITEM_TOP},
		{1.0f, 0.0f, DSR_ITEM_TOP | DSR_ITEM_RIGHT},
		{0.0f, 0.5f, DSR_ITEM_LEFT},
		{1.0f, 0.5f, DSR_ITEM_RIGHT},
		{0.0f, 1.0f, DSR_ITEM_BOTTOM | DSR_ITEM_LEFT},
		{0.5f, 1.0f, DSR_ITEM_BOTTOM},
		{1.0f, 1.0f, DSR_ITEM_BOTTOM | DSR_ITEM_RIGHT},
	};

	uint32_t found = 0;
	double closest = tolerance;
	for (const auto &handle : handles) {
		struct vec3 corner;
		vec3_set(&corner, handle.x, handle.y, 0.0f);
		vec3_transform(&corner, &corner, &boxTransform);

		const double dist = std::hypot(canvasPos.x() - corner.x, canvasPos.y() - corner.y);
		if (dist <= closest) {
			closest = dist;
			found = handle.flags;
		}
	}
	return found;
}

/* Capture what a stretch or crop needs to run from, the way
 * GetStretchHandleData does on mouse down. */
void VerticalPreview::beginStretch(obs_sceneitem_t *item, uint32_t handle)
{
	stretchHandle = handle;
	stretchSize = itemSize(item);

	struct matrix4 boxTransform;
	struct vec3 itemUL;
	obs_sceneitem_get_box_transform(item, &boxTransform);
	vec3_from_vec4(&itemUL, &boxTransform.t);
	const float rot = obs_sceneitem_get_rot(item);

	matrix4_identity(&itemToScreen);
	matrix4_rotate_aa4f(&itemToScreen, &itemToScreen, 0.0f, 0.0f, 1.0f, RAD(rot));
	matrix4_translate3f(&itemToScreen, &itemToScreen, itemUL.x, itemUL.y, 0.0f);

	matrix4_identity(&screenToItem);
	matrix4_translate3f(&screenToItem, &screenToItem, -itemUL.x, -itemUL.y, 0.0f);
	matrix4_rotate_aa4f(&screenToItem, &screenToItem, 0.0f, 0.0f, 1.0f, RAD(-rot));

	obs_sceneitem_get_crop(item, &startCrop);
}

/* Scale the item so the dragged side follows the cursor. Aspect is held unless
 * shift is down, which is why an edge handle moves both axes. */
void VerticalPreview::stretchItem(obs_sceneitem_t *item, const QPointF &canvasPos)
{
	const bool shiftDown = QGuiApplication::keyboardModifiers() & Qt::ShiftModifier;
	const enum obs_bounds_type boundsType = obs_sceneitem_get_bounds_type(item);

	struct vec3 tl;
	struct vec3 br;
	struct vec3 pos3;
	vec3_zero(&tl);
	vec3_set(&br, stretchSize.x, stretchSize.y, 0.0f);
	vec3_set(&pos3, (float)canvasPos.x(), (float)canvasPos.y(), 0.0f);
	vec3_transform(&pos3, &pos3, &screenToItem);

	if (stretchHandle & DSR_ITEM_LEFT)
		tl.x = pos3.x;
	else if (stretchHandle & DSR_ITEM_RIGHT)
		br.x = pos3.x;

	if (stretchHandle & DSR_ITEM_TOP)
		tl.y = pos3.y;
	else if (stretchHandle & DSR_ITEM_BOTTOM)
		br.y = pos3.y;

	obs_source_t *source = obs_sceneitem_get_source(item);
	const uint32_t sourceCx = obs_source_get_width(source);
	const uint32_t sourceCy = obs_source_get_height(source);
	/* A source that has momentarily reported no size would otherwise be
	 * scaled to nothing and left invisible until a manual reset. */
	if (!sourceCx || !sourceCy)
		return;

	struct vec2 baseSize;
	struct vec2 size;
	vec2_set(&baseSize, (float)sourceCx, (float)sourceCy);
	vec2_set(&size, br.x - tl.x, br.y - tl.y);

	if (boundsType != OBS_BOUNDS_NONE) {
		if (shiftDown)
			clampAspect(stretchHandle, size, baseSize);
		if (tl.x > br.x)
			std::swap(tl.x, br.x);
		if (tl.y > br.y)
			std::swap(tl.y, br.y);
		vec2_abs(&size, &size);
		obs_sceneitem_set_bounds(item, &size);
	} else {
		struct obs_sceneitem_crop crop;
		obs_sceneitem_get_crop(item, &crop);
		baseSize.x -= (float)(crop.left + crop.right);
		baseSize.y -= (float)(crop.top + crop.bottom);

		if (!shiftDown)
			clampAspect(stretchHandle, size, baseSize);

		if (baseSize.x == 0.0f || baseSize.y == 0.0f)
			return;
		vec2_div(&size, &size, &baseSize);
		obs_sceneitem_set_scale(item, &size);
	}

	pos3 = stretchPos(item, tl, br);
	vec3_transform(&pos3, &pos3, &itemToScreen);

	struct vec2 newPos;
	vec2_set(&newPos, std::round(pos3.x), std::round(pos3.y));
	obs_sceneitem_set_pos(item, &newPos);
}

/* Alt drag: move the source's crop instead of its scale, so the item stays put
 * and its content is trimmed. Ported from CropItem. */
void VerticalPreview::cropItem(obs_sceneitem_t *item, const QPointF &canvasPos)
{
	const enum obs_bounds_type boundsType = obs_sceneitem_get_bounds_type(item);
	const uint32_t align = obs_sceneitem_get_alignment(item);

	struct vec3 tl;
	struct vec3 br;
	struct vec3 pos3;
	vec3_zero(&tl);
	vec3_set(&br, stretchSize.x, stretchSize.y, 0.0f);
	vec3_set(&pos3, (float)canvasPos.x(), (float)canvasPos.y(), 0.0f);
	vec3_transform(&pos3, &pos3, &screenToItem);

	struct vec2 rawScale;
	struct vec2 scale;
	obs_sceneitem_get_scale(item, &rawScale);
	vec2_set(&scale, boundsType == OBS_BOUNDS_NONE ? rawScale.x : fabsf(rawScale.x),
		 boundsType == OBS_BOUNDS_NONE ? rawScale.y : fabsf(rawScale.y));
	if (scale.x == 0.0f || scale.y == 0.0f)
		return;

	/* A crop can never pull past the source's own edges. */
	struct vec2 maxTl;
	struct vec2 maxBr;
	vec2_set(&maxTl, (float)(-startCrop.left) * scale.x, (float)(-startCrop.top) * scale.y);
	vec2_set(&maxBr, stretchSize.x + startCrop.right * scale.x, stretchSize.y + startCrop.bottom * scale.y);

	const bool flipX = scale.x < 0.0f && boundsType == OBS_BOUNDS_NONE;
	const bool flipY = scale.y < 0.0f && boundsType == OBS_BOUNDS_NONE;
	auto minX = [flipX](float a, float b) {
		return flipX ? fmaxf(a, b) : fminf(a, b);
	};
	auto maxX = [flipX](float a, float b) {
		return flipX ? fminf(a, b) : fmaxf(a, b);
	};
	auto minY = [flipY](float a, float b) {
		return flipY ? fmaxf(a, b) : fminf(a, b);
	};
	auto maxY = [flipY](float a, float b) {
		return flipY ? fminf(a, b) : fmaxf(a, b);
	};

	pos3.x = minX(pos3.x, maxBr.x);
	pos3.x = maxX(pos3.x, maxTl.x);
	pos3.y = minY(pos3.y, maxBr.y);
	pos3.y = maxY(pos3.y, maxTl.y);

	/* Two pixels of source always survive, so the item never collapses. */
	if (stretchHandle & DSR_ITEM_LEFT)
		pos3.x = tl.x = minX(pos3.x, stretchSize.x - (2.0f * scale.x));
	else if (stretchHandle & DSR_ITEM_RIGHT)
		pos3.x = br.x = maxX(pos3.x, 2.0f * scale.x);

	if (stretchHandle & DSR_ITEM_TOP)
		pos3.y = tl.y = minY(pos3.y, stretchSize.y - (2.0f * scale.y));
	else if (stretchHandle & DSR_ITEM_BOTTOM)
		pos3.y = br.y = maxY(pos3.y, 2.0f * scale.y);

	const uint32_t alignX = align & (DSR_ITEM_LEFT | DSR_ITEM_RIGHT);
	const uint32_t alignY = align & (DSR_ITEM_TOP | DSR_ITEM_BOTTOM);

	struct vec3 newPos;
	vec3_zero(&newPos);

	if (alignX == (stretchHandle & (DSR_ITEM_LEFT | DSR_ITEM_RIGHT)) && alignX != 0)
		newPos.x = pos3.x;
	else if (align & DSR_ITEM_RIGHT)
		newPos.x = stretchSize.x;
	else if (!(align & DSR_ITEM_LEFT))
		newPos.x = stretchSize.x * 0.5f;

	if (alignY == (stretchHandle & (DSR_ITEM_TOP | DSR_ITEM_BOTTOM)) && alignY != 0)
		newPos.y = pos3.y;
	else if (align & DSR_ITEM_BOTTOM)
		newPos.y = stretchSize.y;
	else if (!(align & DSR_ITEM_TOP))
		newPos.y = stretchSize.y * 0.5f;

	struct obs_sceneitem_crop crop = startCrop;
	if (stretchHandle & DSR_ITEM_LEFT)
		crop.left += (int)std::round(tl.x / scale.x);
	else if (stretchHandle & DSR_ITEM_RIGHT)
		crop.right += (int)std::round((stretchSize.x - br.x) / scale.x);

	if (stretchHandle & DSR_ITEM_TOP)
		crop.top += (int)std::round(tl.y / scale.y);
	else if (stretchHandle & DSR_ITEM_BOTTOM)
		crop.bottom += (int)std::round((stretchSize.y - br.y) / scale.y);

	vec3_transform(&newPos, &newPos, &itemToScreen);
	newPos.x = std::round(newPos.x);
	newPos.y = std::round(newPos.y);

	obs_sceneitem_defer_update_begin(item);
	obs_sceneitem_set_crop(item, &crop);
	if (boundsType == OBS_BOUNDS_NONE)
		obs_sceneitem_set_pos(item, (struct vec2 *)&newPos);
	obs_sceneitem_defer_update_end(item);
}
