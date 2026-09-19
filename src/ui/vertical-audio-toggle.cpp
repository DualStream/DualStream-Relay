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

#include "vertical-audio-toggle.hpp"

#include <QLabel>
#include <QMetaObject>
#include <QVBoxLayout>
#include <QVector>

#include "../vertical-audio.hpp"
#include "dsr-ui-common.hpp"

VerticalAudioToggle::VerticalAudioToggle(obs_source_t *source, int mixer, QWidget *parent)
	: QCheckBox(QString::fromUtf8(obs_source_get_name(source)), parent),
	  weak(obs_source_get_weak_source(source)),
	  mixer(mixer)
{
	setObjectName(QStringLiteral("vertAudioBox"));
	setChecked(dsrVerticalAudioOn(source, mixer));

	connect(this, &QAbstractButton::clicked, this, [this](bool heard) {
		obs_source_t *target = obs_weak_source_get_source(weak);
		if (target) {
			dsrSetVerticalAudioOn(target, this->mixer, heard);
			obs_source_release(target);
		}
	});

	signal_handler_connect(obs_source_get_signal_handler(source), "audio_mixers", onMixersChanged, this);
}

VerticalAudioToggle::~VerticalAudioToggle()
{
	obs_source_t *source = obs_weak_source_get_source(weak);
	if (source) {
		signal_handler_disconnect(obs_source_get_signal_handler(source), "audio_mixers", onMixersChanged, this);
		obs_source_release(source);
	}
	obs_weak_source_release(weak);
}

/* Raised on whichever thread changed the membership, with the new mask in
 * the call data, since the source itself is updated only afterwards. */
void VerticalAudioToggle::onMixersChanged(void *data, calldata_t *cd)
{
	VerticalAudioToggle *self = static_cast<VerticalAudioToggle *>(data);
	const uint32_t mixers = (uint32_t)calldata_int(cd, "mixers");
	const bool heard = (mixers & (1u << self->mixer)) != 0;
	QMetaObject::invokeMethod(
		self,
		[self, heard]() {
			if (self->isChecked() != heard)
				self->setChecked(heard);
		},
		Qt::QueuedConnection);
}

namespace {

/* Every source with audio the portrait scene shows, groups included, each
 * once: membership is a property of the source, so two items of the same
 * source share one box. */
struct AudioSources {
	QVector<obs_source_t *> sources;

	void add(obs_source_t *source)
	{
		if (!dsrSourceHasAudio(source) || sources.contains(source))
			return;
		sources.append(obs_source_get_ref(source));
	}

	~AudioSources()
	{
		for (obs_source_t *source : sources)
			obs_source_release(source);
	}
};

bool collectAudioSources(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	AudioSources *found = static_cast<AudioSources *>(param);
	if (obs_sceneitem_is_group(item))
		obs_sceneitem_group_enum_items(item, collectAudioSources, param);
	else
		found->add(obs_sceneitem_get_source(item));
	return true;
}

QLabel *makeText(const char *key, const char *objectName)
{
	QLabel *label = new QLabel(dsrText(key));
	label->setObjectName(QString::fromUtf8(objectName));
	label->setWordWrap(true);
	return label;
}

} // namespace

QWidget *dsrMakeVerticalAudioSection(obs_source_t *portraitScene, int mixer, bool dedicated,
				     DsrVerticalDelivery delivery)
{
	AudioSources found;
	if (dedicated) {
		obs_scene_enum_items(obs_scene_from_source(portraitScene), collectAudioSources, &found);
		dsrEnumAudioDevices([&](obs_source_t *device) { found.add(device); });
		if (found.sources.isEmpty())
			return nullptr;
	}

	QWidget *section = new QWidget;
	section->setObjectName(QStringLiteral("vertAudioSection"));
	QVBoxLayout *layout = new QVBoxLayout(section);
	layout->setContentsMargins(10, 12, 10, 4);
	layout->setSpacing(6);

	layout->addWidget(makeText("Vertical.AudioHeading", "vertAudioHeading"));
	layout->addWidget(makeText(dedicated ? "Vertical.AudioNote" : "Vertical.AudioShared", "mutedText"));
	/* Said plainly and every time, because a streamer who hears the
	 * desktop on a phone after unticking it here has no other way to
	 * know that Twitch dual format never took these choices. */
	if (dedicated && delivery.dualFormat) {
		layout->addWidget(makeText(delivery.separateStream ? "Vertical.AudioDualFormatAlso"
								   : "Vertical.AudioDualFormatOnly",
					   "vertAudioWarning"));
	}
	for (obs_source_t *source : found.sources)
		layout->addWidget(new VerticalAudioToggle(source, mixer));
	return section;
}
