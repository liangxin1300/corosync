/*
 * Copyright (c) 2015-2016 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Jan Friesse (jfriesse@redhat.com)
 *
 * This software licensed under BSD license, the text of which follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * - Neither the name of the Red Hat, Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <poll.h>

#include "qdevice-config.h"
#include "qdevice-log.h"
#include "qdevice-votequorum.h"
#include "qdevice-model.h"
#include "utils.h"

static void
qdevice_votequorum_quorum_notify_callback(votequorum_handle_t votequorum_handle,
    uint64_t context, uint32_t quorate,
    uint32_t node_list_entries, votequorum_node_t node_list[])
{
	struct qdevice_instance *instance;
	uint32_t u32;

	if (votequorum_context_get(votequorum_handle, (void **)&instance) != CS_OK) {
		qdevice_log(LOG_CRIT, "Fatal error. Can't get votequorum context");
		exit(1);
	}

	instance->sync_in_progress = 0;

	qdevice_log(LOG_DEBUG, "Votequorum quorum notify callback:");
	qdevice_log(LOG_DEBUG, "  Quorate = %u", quorate);

	qdevice_log(LOG_DEBUG, "  Node list (size = %"PRIu32"):", node_list_entries);
	for (u32 = 0; u32 < node_list_entries; u32++) {
		qdevice_log(LOG_DEBUG, "    %"PRIu32" nodeid = "UTILS_PRI_NODE_ID", state = %"PRIu32,
		    u32, node_list[u32].nodeid, node_list[u32].state);
	}

	if (qdevice_model_votequorum_quorum_notify(instance, quorate, node_list_entries,
	    node_list) != 0) {
		qdevice_log(LOG_DEBUG, "qdevice_model_votequorum_quorum_notify returned error -> exit");
		exit(2);
	}

	instance->vq_quorum_quorate = quorate;
	instance->vq_quorum_node_list_entries = node_list_entries;

	free(instance->vq_quorum_node_list);
	instance->vq_quorum_node_list = malloc(sizeof(*node_list) * node_list_entries);
	if (instance->vq_quorum_node_list == NULL) {
		qdevice_log(LOG_CRIT, "Can't alloc votequorum node list memory");
		exit(1);
	}
	memcpy(instance->vq_quorum_node_list, node_list, sizeof(*node_list) * node_list_entries);
}

static void
qdevice_votequorum_node_list_notify_callback(votequorum_handle_t votequorum_handle,
    uint64_t context, votequorum_ring_id_t votequorum_ring_id,
    uint32_t node_list_entries, uint32_t node_list[])
{
	struct qdevice_instance *instance;
	uint32_t u32;

	if (votequorum_context_get(votequorum_handle, (void **)&instance) != CS_OK) {
		qdevice_log(LOG_CRIT, "Fatal error. Can't get votequorum context");
		exit(1);
	}

	instance->sync_in_progress = 1;

	qdevice_log(LOG_DEBUG, "Votequorum nodelist notify callback:");
	qdevice_log(LOG_DEBUG, "  Ring_id = ("UTILS_PRI_RING_ID")",
	    votequorum_ring_id.nodeid, votequorum_ring_id.seq);

	qdevice_log(LOG_DEBUG, "  Node list (size = %"PRIu32"):", node_list_entries);
	for (u32 = 0; u32 < node_list_entries; u32++) {
		qdevice_log(LOG_DEBUG, "    %"PRIu32" nodeid = "UTILS_PRI_NODE_ID,
		    u32, node_list[u32]);
	}

	if (qdevice_model_votequorum_node_list_notify(instance, votequorum_ring_id, node_list_entries,
	    node_list) != 0) {
		qdevice_log(LOG_DEBUG, "qdevice_votequorum_node_list_notify_callback returned error -> exit");
		exit(2);
	}

	memcpy(&instance->vq_node_list_ring_id, &votequorum_ring_id, sizeof(votequorum_ring_id));
	instance->vq_node_list_entries = node_list_entries;
	free(instance->vq_node_list);
	instance->vq_node_list = malloc(sizeof(*node_list) * node_list_entries);
	if (instance->vq_node_list == NULL) {
		qdevice_log(LOG_CRIT, "Can't alloc votequorum node list memory");
		exit(1);
	}
	memcpy(instance->vq_node_list, node_list, sizeof(*node_list) * node_list_entries);
}

static void
qdevice_votequorum_expected_votes_notify_callback(votequorum_handle_t votequorum_handle,
    uint64_t context, uint32_t expected_votes)
{
	struct qdevice_instance *instance;

	if (votequorum_context_get(votequorum_handle, (void **)&instance) != CS_OK) {
		qdevice_log(LOG_CRIT, "Fatal error. Can't get votequorum context");
		exit(1);
	}

	qdevice_log(LOG_DEBUG, "Votequorum expected_votes notify callback:");
	qdevice_log(LOG_DEBUG, "  Expected_votes: "UTILS_PRI_EXPECTED_VOTES, expected_votes);

	if (qdevice_model_votequorum_expected_votes_notify(instance, expected_votes) != 0) {
		qdevice_log(LOG_DEBUG, "qdevice_votequorum_expected_votes_notify_callback returned error -> exit");
		exit(2);
	}

	instance->vq_expected_votes = expected_votes;
}

void
qdevice_votequorum_init(struct qdevice_instance *instance)
{
	votequorum_callbacks_t votequorum_callbacks;
	votequorum_handle_t votequorum_handle;
	cs_error_t res;
	int no_retries;
	struct votequorum_info vq_info;

	memset(&votequorum_callbacks, 0, sizeof(votequorum_callbacks));

	votequorum_callbacks.votequorum_quorum_notify_fn =
	    qdevice_votequorum_quorum_notify_callback;

	votequorum_callbacks.votequorum_nodelist_notify_fn =
	    qdevice_votequorum_node_list_notify_callback;

	votequorum_callbacks.votequorum_expectedvotes_notify_fn =
	    qdevice_votequorum_expected_votes_notify_callback;

	no_retries = 0;

	while ((res = votequorum_initialize(&votequorum_handle,
	    &votequorum_callbacks)) == CS_ERR_TRY_AGAIN &&
	    no_retries++ < instance->advanced_settings->max_cs_try_again) {
		(void)poll(NULL, 0, 1000);
	}

        if (res != CS_OK) {
		qdevice_log(LOG_CRIT, "Failed to initialize the votequorum API. Error %s", cs_strerror(res));
		exit(1);
	}

	if ((res = votequorum_qdevice_register(votequorum_handle,
	    instance->advanced_settings->votequorum_device_name)) != CS_OK) {
		qdevice_log(LOG_CRIT, "Can't register votequorum device. Error %s", cs_strerror(res));
		exit(1);
	}

	if ((res = votequorum_context_set(votequorum_handle, (void *)instance)) != CS_OK) {
		qdevice_log(LOG_CRIT, "Can't set votequorum context. Error %s", cs_strerror(res));
		exit(1);
	}

	if ((res = votequorum_getinfo(votequorum_handle, VOTEQUORUM_QDEVICE_NODEID,
	    &vq_info)) != CS_OK) {
		qdevice_log(LOG_CRIT, "Can't get votequorum information. Error %s", cs_strerror(res));
		exit(1);
	}
	instance->vq_expected_votes = vq_info.node_expected_votes;

	instance->votequorum_handle = votequorum_handle;

	votequorum_fd_get(votequorum_handle, &instance->votequorum_poll_fd);

	if ((res = votequorum_trackstart(instance->votequorum_handle, 0,
	    CS_TRACK_CHANGES)) != CS_OK) {
		qdevice_log(LOG_CRIT, "Can't start tracking votequorum changes. Error %s",
		    cs_strerror(res));
		exit(1);
	}
}

void
qdevice_votequorum_destroy(struct qdevice_instance *instance)
{
	cs_error_t res;

	free(instance->vq_quorum_node_list); instance->vq_quorum_node_list = NULL;
	free(instance->vq_node_list); instance->vq_node_list = NULL;

	res = votequorum_trackstop(instance->votequorum_handle);
	if (res != CS_OK) {
		qdevice_log(LOG_WARNING, "Can't start tracking votequorum changes. Error %s",
		    cs_strerror(res));
	}

	res = votequorum_qdevice_unregister(instance->votequorum_handle,
		instance->advanced_settings->votequorum_device_name);

        if (res != CS_OK) {
                qdevice_log(LOG_WARNING, "Unable to unregister votequorum device. Error %s", cs_strerror(res));
	}

	res = votequorum_finalize(instance->votequorum_handle);
	if (res != CS_OK) {
		qdevice_log(LOG_WARNING, "Unable to finalize votequorum. Error %s", cs_strerror(res));
	}
}

int
qdevice_votequorum_dispatch(struct qdevice_instance *instance)
{
	cs_error_t res;

	res = votequorum_dispatch(instance->votequorum_handle, CS_DISPATCH_ALL);

	if (res != CS_OK && res != CS_ERR_TRY_AGAIN) {
		qdevice_log(LOG_ERR, "Can't dispatch votequorum messages");

		return (-1);
	}

	return (0);
}

int
qdevice_votequorum_poll(struct qdevice_instance *instance, int cast_vote)
{
	cs_error_t res;

	instance->vq_last_poll = time(NULL);
	instance->vq_last_poll_cast_vote = cast_vote;

	res = votequorum_qdevice_poll(instance->votequorum_handle,
	    instance->advanced_settings->votequorum_device_name, cast_vote,
	    instance->vq_node_list_ring_id);

	if (res != CS_OK && res != CS_ERR_TRY_AGAIN) {
		if (res == CS_ERR_MESSAGE_ERROR) {
			qdevice_log(LOG_INFO, "qdevice_votequorum_poll called with old ring id");
		} else {
			qdevice_log(LOG_CRIT, "Can't call votequorum_qdevice_poll. Error %s",
			    cs_strerror(res));

			return (-1);
		}
	}

	return (0);
}
