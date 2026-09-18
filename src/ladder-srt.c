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

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#endif

#include <stdlib.h>
#include <string.h>

#include <obs-module.h>
#include <util/platform.h>
#include <util/threading.h>
#include <plugin-support.h>

/* libsrt's logging header defines syslog level names that collide with
 * libobs's own LOG_WARNING, LOG_INFO and LOG_DEBUG. Its include guard is
 * claimed here and the one declaration the public API still needs from it
 * is supplied by hand, the same way OBS's own SRT output does. */
#define INC_SRT_LOGGING_API_H
typedef void SRT_LOG_HANDLER_FN(void *opaque, int level, const char *file, int line, const char *area,
				const char *message);
#include <srt/srt.h>

#include "ladder-srt.h"
#include "relay-output.h"

/* Seven transport packets, the payload every live SRT message carries. */
#define PAYLOAD_SIZE 1316
/* The relay recommends a send buffer this size for the bitrates it takes. */
#define SEND_BUFFER_BYTES 1000000
/* A send that cannot be queued within the link's own retransmission window
 * plus this margin means the link has stopped carrying anything; erroring
 * out lets the output reconnect instead of standing still. */
#define SEND_TIMEOUT_MARGIN_MS 1000
/* How long one connection attempt may take before the next address is tried. */
#define CONNECT_TIMEOUT_MS 3000

struct dsr_srt_link {
	/* Guards the socket handle: the owner's thread swaps it while the
	 * connect or writer thread may be about to use it. */
	pthread_mutex_t mutex;
	SRTSOCKET socket;
	volatile bool aborted;
	bool started;
};

static void set_error(char *error, size_t error_len, const char *text)
{
	if (!error || error_len == 0)
		return;
	strncpy(error, text, error_len - 1);
	error[error_len - 1] = '\0';
}

/* srt://host:port?latency=<us>&peerlatency=<us>, as the route module prepares
 * it for OBS's own SRT output. Only the peer latency matters to a sender: it
 * is the window the relay waits for a retransmission. */
static bool parse_url(const char *url, char *host, size_t host_len, char *port, size_t port_len, int *latency_ms)
{
	const char *scheme = "srt://";
	if (!url || strncmp(url, scheme, strlen(scheme)) != 0)
		return false;

	const char *at = url + strlen(scheme);
	const char *host_end = at;
	if (*at == '[') {
		host_end = strchr(at, ']');
		if (!host_end)
			return false;
		at++;
		if ((size_t)(host_end - at) >= host_len)
			return false;
		memcpy(host, at, (size_t)(host_end - at));
		host[host_end - at] = '\0';
		host_end++;
	} else {
		while (*host_end && *host_end != ':' && *host_end != '/' && *host_end != '?')
			host_end++;
		if (host_end == at || (size_t)(host_end - at) >= host_len)
			return false;
		memcpy(host, at, (size_t)(host_end - at));
		host[host_end - at] = '\0';
	}

	if (*host_end != ':')
		return false;
	const char *port_start = host_end + 1;
	const char *port_end = port_start;
	while (*port_end >= '0' && *port_end <= '9')
		port_end++;
	if (port_end == port_start || (size_t)(port_end - port_start) >= port_len)
		return false;
	memcpy(port, port_start, (size_t)(port_end - port_start));
	port[port_end - port_start] = '\0';

	*latency_ms = DSR_SRT_LATENCY_MS;
	const char *query = strchr(port_end, '?');
	if (!query)
		return true;

	for (const char *segment = query + 1; *segment;) {
		const char *end = strchr(segment, '&');
		const size_t len = end ? (size_t)(end - segment) : strlen(segment);
		if (len > 12 && strncmp(segment, "peerlatency=", 12) == 0) {
			const long micros = strtol(segment + 12, NULL, 10);
			if (micros > 0)
				*latency_ms = (int)(micros / 1000);
		}
		if (!end)
			break;
		segment = end + 1;
	}
	return true;
}

static bool set_int_option(SRTSOCKET socket, SRT_SOCKOPT option, const char *name, int value)
{
	if (srt_setsockopt(socket, 0, option, &value, sizeof(value)) < 0) {
		obs_log(LOG_WARNING, "srt option %s could not be set: %s", name, srt_getlasterror_str());
		return false;
	}
	return true;
}

static bool set_socket_options(SRTSOCKET socket, const char *stream_id, int latency_ms)
{
	const int64_t no_limit = -1;
	const SRT_TRANSTYPE live = SRTT_LIVE;

	if (srt_setsockopt(socket, 0, SRTO_TRANSTYPE, &live, sizeof(live)) < 0)
		return false;
	if (!set_int_option(socket, SRTO_SENDER, "SRTO_SENDER", 1) ||
	    !set_int_option(socket, SRTO_PAYLOADSIZE, "SRTO_PAYLOADSIZE", PAYLOAD_SIZE) ||
	    !set_int_option(socket, SRTO_LATENCY, "SRTO_LATENCY", latency_ms) ||
	    !set_int_option(socket, SRTO_PEERLATENCY, "SRTO_PEERLATENCY", latency_ms) ||
	    !set_int_option(socket, SRTO_TLPKTDROP, "SRTO_TLPKTDROP", 1) ||
	    !set_int_option(socket, SRTO_SNDBUF, "SRTO_SNDBUF", SEND_BUFFER_BYTES) ||
	    !set_int_option(socket, SRTO_CONNTIMEO, "SRTO_CONNTIMEO", CONNECT_TIMEOUT_MS) ||
	    !set_int_option(socket, SRTO_SNDTIMEO, "SRTO_SNDTIMEO", latency_ms + SEND_TIMEOUT_MARGIN_MS))
		return false;
	if (srt_setsockopt(socket, 0, SRTO_MAXBW, &no_limit, sizeof(no_limit)) < 0)
		return false;
	/* The stream id names the ingest path. It opens with a '#', which is
	 * why it must never ride inside the URL. */
	return srt_setsockopt(socket, 0, SRTO_STREAMID, stream_id, (int)strlen(stream_id)) >= 0;
}

struct dsr_srt_link *dsr_srt_link_create(void)
{
	struct dsr_srt_link *link = bzalloc(sizeof(*link));
	pthread_mutex_init_value(&link->mutex);
	if (pthread_mutex_init(&link->mutex, NULL) != 0) {
		bfree(link);
		return NULL;
	}
	link->socket = SRT_INVALID_SOCK;
	return link;
}

/* Hand the link a candidate socket, unless it was broken meanwhile, in which
 * case the candidate is closed here and false comes back. */
static bool publish_socket(struct dsr_srt_link *link, SRTSOCKET candidate)
{
	pthread_mutex_lock(&link->mutex);
	const bool live = !link->aborted;
	if (live)
		link->socket = candidate;
	pthread_mutex_unlock(&link->mutex);
	if (!live)
		srt_close(candidate);
	return live;
}

static SRTSOCKET take_socket(struct dsr_srt_link *link)
{
	pthread_mutex_lock(&link->mutex);
	const SRTSOCKET socket = link->socket;
	link->socket = SRT_INVALID_SOCK;
	pthread_mutex_unlock(&link->mutex);
	return socket;
}

bool dsr_srt_connect(struct dsr_srt_link *link, const char *server_url, const char *stream_id, char *error,
		     size_t error_len)
{
	char host[256];
	char port[8];
	int latency_ms = 0;
	if (!parse_url(server_url, host, sizeof(host), port, sizeof(port), &latency_ms) || !stream_id || !*stream_id) {
		set_error(error, error_len, "relay address is not a usable SRT URL");
		return false;
	}

	if (srt_startup() < 0) {
		set_error(error, error_len, srt_getlasterror_str());
		return false;
	}
	link->started = true;

	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	struct addrinfo *addresses = NULL;
	if (getaddrinfo(host, port, &hints, &addresses) != 0 || !addresses) {
		set_error(error, error_len, "relay host could not be resolved");
		return false;
	}

	/* Every address the name resolves to gets one attempt on a fresh
	 * socket; a socket that failed to connect cannot be reused. The
	 * candidate is published before the attempt so a break from another
	 * thread reaches the connect in progress. */
	bool connected = false;
	const char *failure = "relay refused the SRT connection";
	for (struct addrinfo *address = addresses; address && !link->aborted; address = address->ai_next) {
		SRTSOCKET candidate = srt_create_socket();
		if (candidate == SRT_INVALID_SOCK)
			continue;
		if (!set_socket_options(candidate, stream_id, latency_ms)) {
			failure = srt_getlasterror_str();
			srt_close(candidate);
			continue;
		}
		if (!publish_socket(link, candidate))
			break;
		if (srt_connect(candidate, address->ai_addr, (int)address->ai_addrlen) != SRT_ERROR) {
			connected = true;
			break;
		}
		const int reason = srt_getrejectreason(candidate);
		failure = reason != SRT_REJ_UNKNOWN ? srt_rejectreason_str(reason) : srt_getlasterror_str();
		obs_log(LOG_WARNING, "srt connect to %s:%s failed: %s", host, port, failure);
		const SRTSOCKET failed = take_socket(link);
		if (failed != SRT_INVALID_SOCK)
			srt_close(failed);
	}
	freeaddrinfo(addresses);

	if (!connected) {
		set_error(error, error_len, link->aborted ? "stopped before the relay answered" : failure);
		return false;
	}

	obs_log(LOG_INFO, "srt link open to %s:%s (window %d ms)", host, port, latency_ms);
	return true;
}

bool dsr_srt_send(struct dsr_srt_link *link, const uint8_t *data, size_t len)
{
	if (!link)
		return false;

	/* The handle is read under the lock and used outside it: a send can
	 * block for the length of the window, and a break from another thread
	 * must be able to close the socket underneath it. */
	pthread_mutex_lock(&link->mutex);
	const SRTSOCKET socket = link->socket;
	pthread_mutex_unlock(&link->mutex);
	if (socket == SRT_INVALID_SOCK)
		return false;

	const int sent = srt_sendmsg2(socket, (const char *)data, (int)len, NULL);
	if (sent == (int)len)
		return true;

	if (!link->aborted)
		obs_log(LOG_WARNING, "srt send failed: %s", srt_getlasterror_str());
	return false;
}

void dsr_srt_abort(struct dsr_srt_link *link)
{
	if (!link)
		return;
	pthread_mutex_lock(&link->mutex);
	link->aborted = true;
	const SRTSOCKET socket = link->socket;
	link->socket = SRT_INVALID_SOCK;
	pthread_mutex_unlock(&link->mutex);
	if (socket != SRT_INVALID_SOCK)
		srt_close(socket);
}

void dsr_srt_link_destroy(struct dsr_srt_link *link)
{
	if (!link)
		return;
	dsr_srt_abort(link);
	if (link->started)
		srt_cleanup();
	pthread_mutex_destroy(&link->mutex);
	bfree(link);
}
