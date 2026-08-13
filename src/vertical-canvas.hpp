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

#pragma once

#include <QHash>
#include <QObject>
#include <QString>

#include <obs.h>
#include <obs-frontend-api.h>

/* The portrait canvas.
 *
 * The relay's portrait feed is a real second ingest, not a crop the server
 * derives, so somebody has to composite a 9:16 program and publish it. This
 * class owns that: a 1080x1920 canvas registered with the frontend, one
 * portrait scene per landscape scene holding items that reference the same
 * sources with their own transforms and visibility, and an SRT output that
 * follows OBS's own stream start and stop.
 *
 * A portrait failure must never take the landscape stream down; the output
 * here shares nothing with the one OBS owns.
 *
 * The publish has two paths. Through the relay it is slaved to OBS: it starts
 * and stops with OBS's own Start Streaming and never adds a second control for
 * it. Straight to an RTMP server it cannot be, because OBS's Start Streaming
 * always carries the main canvas, so that path is driven from this class and
 * the dock offers the only control that can put a portrait program on air. */
class VerticalCanvas : public QObject {
	Q_OBJECT

public:
	static VerticalCanvas *instance();

	explicit VerticalCanvas(QObject *parent = nullptr);
	~VerticalCanvas() override;

	bool enabled() const { return canvas != nullptr; }
	bool publishing() const;
	void setEnabled(bool on);

	/* True once the frontend has finished loading a collection. The dock
	 * must not create the canvas before this: docks restore their
	 * visibility during startup, before the collection loads, and a canvas
	 * created that early is destroyed by the load. */
	bool ready() const { return frontendReady; }

	/* Pushed by the relay dock: the prepared portrait SRT server and
	 * stream id from the ingest-target response, and whether any enabled
	 * destination takes the portrait canvas. Both gate the output. */
	void setPortraitTarget(const QString &server, const QString &key);
	void setHasPortraitDestinations(bool has);

	/* Where a direct publish is in its life. Connecting and disconnecting
	 * both take a moment on RTMP, and a control that says nothing during
	 * either reads as one that did nothing. */
	enum class DirectPhase { Idle, Starting, Live, Stopping };
	DirectPhase directPhase() const { return directPhaseValue; }

	/* Publish the portrait program straight to an RTMP server. Driven by
	 * its own control because OBS's Start Streaming always carries the main
	 * canvas, so nothing it does can put this one on air. */
	bool startDirect(const QString &server, const QString &key);
	void stopDirect();

	/* True while the portrait program is going straight to an RTMP server
	 * rather than through the relay. The two are told apart because only the
	 * direct one has a control of its own to show. */
	bool publishingDirect() const { return publishing() && directPublish; }

	/* Whether this account may publish straight to an RTMP server. Pushed by
	 * the relay dock, which is what knows the subscription state, so a
	 * destination left over from a lapsed period stops offering its own
	 * control once the relay is carrying the portrait program. */
	void setDirectAllowed(bool allowed);
	bool directAllowed() const { return directAllowedFlag; }

	/* The locally held destination was saved or removed. */
	void notifyDirectChanged();

	void handleFrontendEvent(enum obs_frontend_event event);

	/* Strong reference for the preview; the caller releases it. */
	obs_canvas_t *canvasRef() const;

	/* Scene source of the portrait counterpart of the current landscape
	 * scene, referenced; the caller releases it. NULL when there is none. */
	obs_source_t *currentCounterpart() const;

	/* Selection is shared state: the preview dock draws it and handles the
	 * mouse, the sources dock highlights its row. It lives here so neither
	 * dock has to know about the other. -1 means nothing selected. */
	int64_t selectedItemId() const { return selectedItem; }
	void setSelectedItemId(int64_t id);

	/* Center an item in the 9:16 frame as a plain transform: fill covers
	 * with edge overflow, fit letterboxes. The one arrangement primitive,
	 * shared by seeding and the sources dock's actions. */
	static void placeItem(obs_sceneitem_t *item, bool fill);

signals:
	void changed();
	void selectionChanged(qint64 itemId);
	/* Raised from the portrait scene's own item_visible signal, so a
	 * visibility change made anywhere in OBS reaches the docks without
	 * anything polling for it. */
	void itemVisibilityChanged(qint64 itemId, bool visible);
	void itemLockChanged(qint64 itemId, bool locked);
	/* The portrait publish came up or went down, by either path. */
	void publishingChanged(bool active);
	/* A direct publish moved between idle, starting, live and stopping. */
	void directPhaseChanged();

private:
	void adopt();
	void teardown();
	void ensureVideo();
	void reconcileScenes();
	void seedCounterpart(obs_source_t *landscapeScene);
	void connectSceneSignals(obs_source_t *landscapeScene);
	void disconnectAllSceneSignals();
	void syncMembership(const QString &sceneUuid);
	void renameCounterpart(const QString &prevName, const QString &newName);

	/* Implemented in vertical-output.cpp. */
	void maybeStartOutput();
	bool startOutput(const QString &server, const QString &key, bool srt);
	void setDirectPhase(DirectPhase phase);
	void stopOutput(bool force);
	void releaseOutput();

	/* Implemented in vertical-transition.cpp. */
	void showCurrentScene();
	void hookCurrentTransition();
	void hookMainTransition(obs_source_t *next);
	void matchTransition(obs_source_t *main);
	void mirrorTransition(obs_source_t *main);
	int mirroredDuration(obs_source_t *destScene) const;
	void releaseTransition();

	/* Portrait counterpart of a landscape scene, referenced; the caller
	 * releases it. NULL when there is none. */
	obs_source_t *counterpartOf(obs_source_t *landscapeScene) const;

	void connectCounterpartSignals(obs_scene_t *portrait);

	static void onItemsChanged(void *data, calldata_t *cd);
	static void onSceneRenamed(void *data, calldata_t *cd);
	static void onItemVisible(void *data, calldata_t *cd);
	static void onItemLocked(void *data, calldata_t *cd);
	static void onSceneReordered(void *data, calldata_t *cd);
	static void onOutputStarted(void *data, calldata_t *cd);
	static void onOutputStopped(void *data, calldata_t *cd);
	static void onChannelChange(void *data, calldata_t *cd);
	static void onMainTransitionStart(void *data, calldata_t *cd);

	obs_canvas_t *canvas = nullptr;

	/* The canvas program channel holds this, not a scene: it is a private
	 * copy of OBS's own transition, driven from the same signal, so a scene
	 * switch animates on both mixes together. */
	obs_source_t *transition = nullptr;
	obs_weak_source_t *hookedTransition = nullptr;
	QString transitionSettings;

	/* Landscape scene uuid to weak scene source, for the scenes whose
	 * signals are connected. Weak because the scene's death is exactly the
	 * moment the entry becomes stale. */
	QHash<QString, obs_weak_source_t *> connectedScenes;

	QString portraitServer;
	QString portraitKey;
	bool hasPortraitDests = false;

	obs_output_t *output = nullptr;
	obs_encoder_t *videoEncoder = nullptr;
	obs_encoder_t *audioEncoder = nullptr;
	obs_service_t *service = nullptr;
	bool frontendReady = false;
	bool directPublish = false;
	DirectPhase directPhaseValue = DirectPhase::Idle;
	bool directAllowedFlag = false;
	int64_t selectedItem = -1;
};
