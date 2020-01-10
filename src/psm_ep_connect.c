/*

  This file is provided under a dual BSD/GPLv2 license.  When using or
  redistributing this file, you may do so under either license.

  GPL LICENSE SUMMARY

  Copyright(c) 2015 Intel Corporation.

  This program is free software; you can redistribute it and/or modify
  it under the terms of version 2 of the GNU General Public License as
  published by the Free Software Foundation.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  Contact Information:
  Intel Corporation, www.intel.com

  BSD LICENSE

  Copyright(c) 2015 Intel Corporation.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the
      distribution.
    * Neither the name of Intel Corporation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

/* Copyright (c) 2003-2014 Intel Corporation. All rights reserved. */

#include "psm_user.h"

int psmi_ep_device_is_enabled(const psm2_ep_t ep, int devid);

psm2_error_t
__psm2_ep_connect(psm2_ep_t ep, int num_of_epid, psm2_epid_t const *array_of_epid,
		 int const *array_of_epid_mask,	/* can be NULL */
		 psm2_error_t *array_of_errors, psm2_epaddr_t *array_of_epaddr,
		 int64_t timeout)
{
	psm2_error_t err = PSM2_OK;
	ptl_ctl_t *ptlctl;
	ptl_t *ptl;
	int i, j, dup_idx;
	int num_toconnect = 0;
	int *epid_mask = NULL;
	int *epid_mask_isdupof = NULL;
	char *device;
	uint64_t t_start = get_cycles();
	uint64_t t_left;
	union psmi_envvar_val timeout_intval;

	PSM2_LOG_MSG("entering");
	PSMI_ERR_UNLESS_INITIALIZED(ep);

	PSMI_PLOCK();

	/*
	 * Normally we would lock here, but instead each implemented ptl component
	 * does its own locking.  This is mostly because the ptl components are
	 * ahead of the PSM interface in that they can disconnect their peers.
	 */
	if (ep == NULL || array_of_epaddr == NULL || array_of_epid == NULL ||
	    num_of_epid < 1) {
		err = psmi_handle_error(ep, PSM2_PARAM_ERR,
					"Invalid psm2_ep_connect parameters");
		goto fail;
	}

	/* We need two of these masks to detect duplicates */
	err = PSM2_NO_MEMORY;
	epid_mask =
	    (int *)psmi_malloc(ep, UNDEFINED, sizeof(int) * num_of_epid);
	if (epid_mask == NULL)
		goto fail;
	epid_mask_isdupof =
	    (int *)psmi_malloc(ep, UNDEFINED, sizeof(int) * num_of_epid);
	if (epid_mask_isdupof == NULL)
		goto fail;
	err = PSM2_OK;

	/* Eventually handle timeouts across all connects. */
	for (j = 0; j < num_of_epid; j++) {
		if (array_of_epid_mask != NULL && !array_of_epid_mask[j])
			epid_mask[j] = 0;
		else {
			epid_mask[j] = 1;
			array_of_errors[j] = PSM2_EPID_UNKNOWN;
			array_of_epaddr[j] = NULL;
			num_toconnect++;
		}
		epid_mask_isdupof[j] = -1;
	}

	psmi_getenv("PSM2_CONNECT_TIMEOUT",
		    "End-point connection timeout over-ride. 0 for no time-out.",
		    PSMI_ENVVAR_LEVEL_USER, PSMI_ENVVAR_TYPE_UINT,
		    (union psmi_envvar_val)0, &timeout_intval);

	if (getenv("PSM2_CONNECT_TIMEOUT")) {
		timeout = timeout_intval.e_uint * SEC_ULL;
	} else if (timeout > 0) {
		/* The timeout parameter provides the minimum timeout. A heuristic
		 * is used to scale up the timeout linearly with the number of
		 * endpoints, and we allow one second per 100 endpoints. */
		timeout = max(timeout, (num_toconnect * SEC_ULL) / 100);
	}

	if (timeout > 0 && timeout < PSMI_MIN_EP_CONNECT_TIMEOUT)
		timeout = PSMI_MIN_EP_CONNECT_TIMEOUT;
	_HFI_PRDBG("Connect to %d endpoints with time-out of %.2f secs\n",
		   num_toconnect, (double)timeout / 1e9);

	/* Look for duplicates in input array */
	for (i = 0; i < num_of_epid; i++) {
		for (j = i + 1; j < num_of_epid; j++) {
			if (array_of_epid[i] == array_of_epid[j] &&
			    epid_mask[i] && epid_mask[j]) {
				epid_mask[j] = 0;	/* don't connect more than once */
				epid_mask_isdupof[j] = i;
			}
		}
	}

	for (i = 0; i < PTL_MAX_INIT; i++) {
		if (ep->devid_enabled[i] == -1)
			continue;
		/* Set up the right connect ptrs */
		switch (ep->devid_enabled[i]) {
		case PTL_DEVID_IPS:
			ptlctl = &ep->ptl_ips;
			ptl = ep->ptl_ips.ptl;
			device = "ips";
			break;
		case PTL_DEVID_AMSH:
			ptlctl = &ep->ptl_amsh;
			ptl = ep->ptl_amsh.ptl;
			device = "amsh";
			break;
		case PTL_DEVID_SELF:
			ptlctl = &ep->ptl_self;
			ptl = ep->ptl_self.ptl;
			device = "self";
			break;
		default:
			device = "unknown";
			ptlctl = &ep->ptl_ips;	/*no-unused */
			ptl = ep->ptl_ips.ptl;	/*no-unused */
			device = "ips";	/*no-unused */
			psmi_handle_error(PSMI_EP_NORETURN, PSM2_INTERNAL_ERR,
					  "Unknown/unhandled PTL id %d\n",
					  ep->devid_enabled[i]);
			break;
		}
		t_left = psmi_cycles_left(t_start, timeout);

		_HFI_VDBG("Trying to connect with device %s\n", device);
		if ((err = ptlctl->ep_connect(ptl, num_of_epid, array_of_epid,
					      epid_mask, array_of_errors,
					      array_of_epaddr,
					      cycles_to_nanosecs(t_left)))) {
			_HFI_PRDBG("Connect failure in device %s err=%d\n",
				   device, err);
			goto connect_fail;
		}

		/* Now process what's been connected */
		for (j = 0; j < num_of_epid; j++) {
			dup_idx = epid_mask_isdupof[j];
			if (!epid_mask[j] && dup_idx == -1)
				continue;

			if (dup_idx != -1) {	/* dup */
				array_of_epaddr[j] = array_of_epaddr[dup_idx];
				array_of_errors[j] = array_of_errors[dup_idx];
				epid_mask_isdupof[j] = -1;
			}

			if (array_of_errors[j] == PSM2_OK) {
				epid_mask[j] = 0;	/* don't try on next ptl */
				ep->connections++;
			}
		}
	}

	for (i = 0; i < num_of_epid; i++) {
		ptl_ctl_t *c = NULL;
		if (array_of_epid_mask != NULL && !array_of_epid_mask[i])
			continue;
		/* If we see unreachable here, that means some PTLs were not enabled */
		if (array_of_errors[i] == PSM2_EPID_UNREACHABLE) {
			err = PSM2_EPID_UNREACHABLE;
			break;
		}

		psmi_assert_always(array_of_epaddr[i] != NULL);
		c = array_of_epaddr[i]->ptlctl;
		psmi_assert_always(c != NULL);
		_HFI_VDBG("%-20s DEVICE %s (%p)\n",
			  psmi_epaddr_get_name(array_of_epid[i]),
			  c == &ep->ptl_ips ? "hfi" :
			  (c == &ep->ptl_amsh ? "amsh" : "self"),
			  (void *)array_of_epaddr[i]->ptlctl->ptl);
	}

connect_fail:
	/* If the error is a timeout (at worse) and the client is OPA MPI,
	 * just return timeout to let OPA MPI handle the hostnames that
	 * timed out */
	if (err != PSM2_OK) {
		char errbuf[PSM2_ERRSTRING_MAXLEN];
		size_t len;
		int j = 0;

		if (err == PSM2_EPID_UNREACHABLE) {
			char *deverr = "of an incorrect setting";
			char *eperr = " ";
			char *devname = NULL;
			if (!psmi_ep_device_is_enabled(ep, PTL_DEVID_AMSH)) {
				deverr =
				    "there is no shared memory PSM device (shm)";
				eperr = " shared memory ";
			} else
			    if (!psmi_ep_device_is_enabled(ep, PTL_DEVID_IPS)) {
				deverr =
				    "there is no OPA PSM device (hfi)";
				eperr = " OPA ";
			}

			len = snprintf(errbuf, sizeof(errbuf) - 1,
				       "Some%sendpoints could not be connected because %s "
				       "in the currently enabled PSM_DEVICES (",
				       eperr, deverr);
			for (i = 0; i < PTL_MAX_INIT && len < sizeof(errbuf) - 1;
			     i++) {
				switch (ep->devid_enabled[i]) {
				case PTL_DEVID_IPS:
					devname = "hfi";
					break;
				case PTL_DEVID_AMSH:
					devname = "shm";
					break;
				case PTL_DEVID_SELF:
				default:
					devname = "self";
					break;
				}
				len +=
				    snprintf(errbuf + len,
					     sizeof(errbuf) - len - 1, "%s,",
					     devname);
			}
			if (len < sizeof(errbuf) - 1 && devname != NULL)
				/* parsed something, remove trailing comma */
				errbuf[len - 1] = ')';
		} else
			len = snprintf(errbuf, sizeof(errbuf) - 1,
				       "%s", err == PSM2_TIMEOUT ?
				       "Dectected connection timeout" :
				       psm2_error_get_string(err));

		/* first pass, look for all nodes with the error */
		for (i = 0; i < num_of_epid && len < sizeof(errbuf) - 1; i++) {
			if (array_of_epid_mask != NULL
			    && !array_of_epid_mask[i])
				continue;
			if (array_of_errors[i] == PSM2_OK)
				continue;
			if (array_of_errors[i] == PSM2_EPID_UNREACHABLE &&
			    err != PSM2_EPID_UNREACHABLE)
				continue;
			if (err == array_of_errors[i]) {
				len +=
				    snprintf(errbuf + len,
					     sizeof(errbuf) - len - 1, "%c %s",
					     j == 0 ? ':' : ',',
					     psmi_epaddr_get_hostname
					     (array_of_epid[i]));
				j++;
			}
		}
		errbuf[sizeof(errbuf) - 1] = '\0';
		err = psmi_handle_error(ep, err, errbuf);
	}

fail:
	PSMI_PUNLOCK();

	if (epid_mask != NULL)
		psmi_free(epid_mask);
	if (epid_mask_isdupof != NULL)
		psmi_free(epid_mask_isdupof);

	PSM2_LOG_MSG("leaving");
	return err;
}
PSMI_API_DECL(psm2_ep_connect)
