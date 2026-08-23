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

#include "relay-http.hpp"

#include <QCoreApplication>
#include <QMetaObject>

#include <curl/curl.h>

#include <string>

#include <plugin-support.h>

namespace {

const long kConnectTimeoutMs = 5000;
const long kRequestTimeoutMs = 15000;

void ensureCurlInit()
{
	/* No matching curl_global_cleanup on purpose: OBS keeps libcurl
	 * resident for the life of the process anyway, and the request
	 * threads themselves are joined at teardown. */
	static std::once_flag once;
	std::call_once(once, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

size_t appendBody(char *data, size_t size, size_t nmemb, void *userdata)
{
	auto *out = static_cast<std::string *>(userdata);
	out->append(data, size * nmemb);
	return size * nmemb;
}

/* Nonzero aborts the transfer. libcurl calls this during every phase of the
 * request, connect included, roughly once a second; it is what turns the
 * teardown flag into a prompt exit instead of a wait on a network timeout. */
int checkAbort(void *clientp, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
	const auto *aborting = static_cast<std::atomic<bool> *>(clientp);
	return aborting->load() ? 1 : 0;
}

DsrHttpReply runRequest(const QByteArray &verb, const QByteArray &url, const QByteArray &payload, bool hasBody,
			const QByteArray &bearer, std::atomic<bool> *aborting)
{
	DsrHttpReply reply;
	std::string body;

	CURL *curl = curl_easy_init();
	if (!curl)
		return reply;

	struct curl_slist *headers = nullptr;
	headers = curl_slist_append(headers, "Accept: application/json");
	if (hasBody)
		headers = curl_slist_append(headers, "Content-Type: application/json");
	if (!bearer.isEmpty()) {
		headers = curl_slist_append(headers, ("Authorization: " + bearer).constData());
		/* Some deployments strip the Authorization header; the API
		 * reads this fallback header first. */
		headers = curl_slist_append(headers, ("X-Auth-Token: " + bearer).constData());
	}

	const QByteArray userAgent = "obs-dualstream-relay/" + QByteArray(PLUGIN_VERSION);

	curl_easy_setopt(curl, CURLOPT_URL, url.constData());
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, userAgent.constData());
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendBody);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, kConnectTimeoutMs);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, kRequestTimeoutMs);
	/* Required for timeouts on threads: signal-based DNS timeout handling
	 * is not thread safe. */
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, checkAbort);
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, aborting);

	/* The bearer travels as a custom header, and curl repeats custom headers
	 * to a redirect target whatever host or scheme it names. Redirects are
	 * therefore refused outright rather than merely capped: the API does not
	 * use them, and the token never leaves the host the user configured. */
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);

	/* curl's default protocol set is far wider than this needs, and includes
	 * schemes that read local files. */
#if LIBCURL_VERSION_NUM >= 0x075500
	curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https,http");
#else
	curl_easy_setopt(curl, CURLOPT_PROTOCOLS, (long)(CURLPROTO_HTTPS | CURLPROTO_HTTP));
#endif

	/* Both default to on, and both are stated anyway: a build linking a
	 * differently configured libcurl must not quietly stop verifying. */
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

	if (verb == "POST") {
		curl_easy_setopt(curl, CURLOPT_POST, 1L);
		curl_easy_setopt(curl, CURLOPT_COPYPOSTFIELDS, hasBody ? payload.constData() : "");
	} else if (verb != "GET") {
		curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, verb.constData());
		if (hasBody)
			curl_easy_setopt(curl, CURLOPT_COPYPOSTFIELDS, payload.constData());
	}

	const CURLcode result = curl_easy_perform(curl);
	long status = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
	reply.status = (int)status;
	reply.transportOk = result == CURLE_OK && status > 0;
	reply.body = QByteArray(body.data(), int(body.size()));

	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	return reply;
}

} // namespace

DsrHttpClient::DsrHttpClient() : aborting(std::make_shared<std::atomic<bool>>(false))
{
	ensureCurlInit();
}

DsrHttpClient::~DsrHttpClient()
{
	aborting->store(true);

	std::vector<Worker> drained;
	{
		std::lock_guard<std::mutex> lock(mutex);
		drained.swap(workers);
	}
	for (Worker &worker : drained) {
		if (worker.thread.joinable())
			worker.thread.join();
	}
}

void DsrHttpClient::reapFinished()
{
	for (auto it = workers.begin(); it != workers.end();) {
		if (it->finished->load()) {
			if (it->thread.joinable())
				it->thread.join();
			it = workers.erase(it);
		} else {
			++it;
		}
	}
}

void DsrHttpClient::send(const QByteArray &verb, const QByteArray &url, const QByteArray &payload, bool hasBody,
			 const QByteArray &bearer, Completion completion)
{
	std::lock_guard<std::mutex> lock(mutex);
	if (aborting->load())
		return;
	reapFinished();

	Worker worker;
	worker.finished = std::make_shared<std::atomic<bool>>(false);

	/* Everything the worker touches is captured by value, plus the two
	 * shared flags. Delivery goes through the application object because
	 * the caller can be destroyed while the request is in flight; the
	 * completion carries its own QPointer guard for that. */
	auto abort = aborting;
	auto finished = worker.finished;
	worker.thread = std::thread(
		[verb, url, payload, hasBody, bearer, completion = std::move(completion), abort, finished]() {
			const DsrHttpReply reply = runRequest(verb, url, payload, hasBody, bearer, abort.get());
			if (!abort->load() && completion) {
				QMetaObject::invokeMethod(
					QCoreApplication::instance(), [completion, reply]() { completion(reply); },
					Qt::QueuedConnection);
			}
			finished->store(true);
		});

	workers.push_back(std::move(worker));
}
