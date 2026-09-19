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

#include <QByteArray>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

/* The HTTP transport under RelayAuth. libcurl, not Qt Network: OBS ships
 * libcurl (with TLS built in) on every platform but does not ship Qt's TLS
 * backend plugins, so QNetworkAccessManager cannot open an https connection
 * inside OBS.
 *
 * Each request runs on its own short-lived thread; the call volume is a poll
 * every few seconds at most. The threads are owned, not detached: this code
 * lives in a module OBS unloads at shutdown, and a request thread still
 * running when the module goes away would return into unmapped code. The
 * destructor flags every in-flight transfer to abort (libcurl polls the flag
 * about once a second through its progress callback) and then joins, so
 * teardown is bounded by that poll rather than by any network timeout. */

struct DsrHttpReply {
	int status = 0;
	bool transportOk = false;
	/* libcurl's own words when the transport failed, for the log. */
	QByteArray transportError;
	QByteArray body;
};

/* One request run to completion on the calling thread. For code that is
 * already off the UI thread and needs the answer before it can go on, such as
 * an output's start thread. shouldAbort is polled through the transfer and a
 * true ends it early. */
DsrHttpReply dsrHttpRequestSync(const QByteArray &verb, const QByteArray &url, const QByteArray &payload, bool hasBody,
				const QByteArray &bearer, std::function<bool()> shouldAbort);

class DsrHttpClient {
public:
	using Completion = std::function<void(const DsrHttpReply &)>;

	DsrHttpClient();
	~DsrHttpClient();

	DsrHttpClient(const DsrHttpClient &) = delete;
	DsrHttpClient &operator=(const DsrHttpClient &) = delete;

	/* Runs the request on a worker thread and delivers the completion on
	 * the UI thread. Once teardown has begun completions are no longer
	 * delivered; callers guard their own lifetime with a QPointer. */
	void send(const QByteArray &verb, const QByteArray &url, const QByteArray &payload, bool hasBody,
		  const QByteArray &bearer, Completion completion);

private:
	struct Worker {
		std::thread thread;
		std::shared_ptr<std::atomic<bool>> finished;
	};

	/* Joins workers that have already run to completion, so the list stays
	 * as small as the number of requests actually in flight. */
	void reapFinished();

	std::mutex mutex;
	std::vector<Worker> workers;
	std::shared_ptr<std::atomic<bool>> aborting;
};
