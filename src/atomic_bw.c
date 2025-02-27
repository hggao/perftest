/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2006 Mellanox Technologies Ltd.  All rights reserved.
 * Copyright (c) 2009 HNR Consulting.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "perftest_logging.h"
#include "perftest_parameters.h"
#include "perftest_resources.h"
#include "perftest_communication.h"

/******************************************************************************
 *
 ******************************************************************************/
int main(int argc, char *argv[])
{
	int ret_parser, i, rc;
	struct ibv_device		*ib_dev = NULL;
	struct pingpong_context		ctx;
	struct pingpong_dest		*my_dest = NULL;
	struct pingpong_dest		*rem_dest = NULL;
	struct perftest_parameters	user_param;
	struct perftest_comm		user_comm;
	struct bw_report_data		my_bw_rep, rem_bw_rep;

	/* init default values to user's parameters */
	memset(&ctx, 0, sizeof(struct pingpong_context));
	memset(&user_param, 0, sizeof(struct perftest_parameters));
	memset(&user_comm, 0, sizeof(struct perftest_comm));

	user_param.verb    = ATOMIC;
	user_param.tst     = BW;
	strncpy(user_param.version, VERSION, sizeof(user_param.version));

	ret_parser = parser(&user_param, argv, argc);
	if (ret_parser) {
		if (ret_parser != VERSION_EXIT && ret_parser != HELP_EXIT)
			log_ebt( " Parser function exited with Error\n");
		return FAILURE;
	}

	if ((user_param.connection_type == DC || user_param.use_xrc) && user_param.duplex) {
		user_param.num_of_qps *= 2;
	}

	ib_dev = ctx_find_dev(&user_param.ib_devname);
	if (!ib_dev)
		return 7;

	/* Getting the relevant context from the device */
	ctx.context = ibv_open_device(ib_dev);
	if (!ctx.context) {
		log_ebt( " Couldn't get context for the device\n");
		return FAILURE;
	}

	/* Verify user parameters that require the device context,
	 * the function will print the relevent error info. */
	if (verify_params_with_device_context(ctx.context, &user_param))
	{
		log_ebt( " Couldn't get context for the device\n");
		return FAILURE;
	}

	/* See if MTU and link type are valid and supported. */
	if (check_link(ctx.context, &user_param)) {
		log_ebt( " Couldn't get context for the device\n");
		return FAILURE;
	}

	/* copy the relevant user parameters to the comm struct + creating rdma_cm resources. */
	if (create_comm_struct(&user_comm, &user_param)) {
		log_ebt( " Unable to create RDMA_CM resources\n");
		return FAILURE;
	}

	if (user_param.output == FULL_VERBOSITY && user_param.machine == SERVER) {
		printf("\n************************************\n");
		printf("* Waiting for client to connect... *\n");
		printf("************************************\n");
	}

	/* Initialize the connection and print the local data. */
	if (establish_connection(&user_comm)) {
		log_ebt( " Unable to init the socket connection\n");
		return FAILURE;
	}

	exchange_versions(&user_comm, &user_param);
	check_version_compatibility(&user_param);
	check_sys_data(&user_comm, &user_param);

	/* See if MTU and link type are valid and supported. */
	if (check_mtu(ctx.context, &user_param, &user_comm)) {
		log_ebt( " Couldn't get context for the device\n");
		return FAILURE;
	}

	ALLOCATE(my_dest, struct pingpong_dest, user_param.num_of_qps);
	memset(my_dest, 0, sizeof(struct pingpong_dest)*user_param.num_of_qps);
	ALLOCATE(rem_dest, struct pingpong_dest, user_param.num_of_qps);
	memset(rem_dest, 0, sizeof(struct pingpong_dest)*user_param.num_of_qps);

	/* Allocating arrays needed for the test. */
	alloc_ctx(&ctx, &user_param);

	/* Create RDMA CM resources and connect through CM. */
	if (user_param.work_rdma_cm == ON) {
		rc = create_rdma_cm_connection(&ctx, &user_param, &user_comm,
			my_dest, rem_dest);
		if (rc) {
			log_ebt(
				"Failed to create RDMA CM connection with resources.\n");
			return FAILURE;
		}
	} else {
		/* create all the basic IB resources. */
		if (ctx_init(&ctx, &user_param)) {
			log_ebt( " Couldn't create IB resources\n");
			return FAILURE;
		}
	}

	/* Set up the Connection. */
	if (set_up_connection(&ctx, &user_param, my_dest)) {
		log_ebt( " Unable to set up socket connection\n");
		return FAILURE;
	}

	/* Print basic test information. */
	ctx_print_test_info(&user_param);

	for (i = 0; i < user_param.num_of_qps; i++) {

		/* shaking hands and gather the other side info. */
		if (ctx_hand_shake(&user_comm, &my_dest[i], &rem_dest[i])) {
			log_ebt( "Failed to exchange data between server and clients\n");
			return FAILURE;
		}
	}

	if (user_param.work_rdma_cm == OFF) {
		if (ctx_check_gid_compatibility(&my_dest[0], &rem_dest[0])) {
			log_ebt( "\n Found Incompatibility issue with GID types.\n");
			log_ebt( " Please Try to use a different IP version.\n\n");
			return FAILURE;
		}
	}

	if (user_param.work_rdma_cm == OFF) {
		if (ctx_connect(&ctx, rem_dest, &user_param, my_dest)) {
			log_ebt( " Unable to Connect the HCA's through the link\n");
			return FAILURE;
		}
	}

	if (user_param.connection_type == DC)
	{
		/* Set up connection one more time to send qpn properly for DC */
		if (set_up_connection(&ctx,&user_param,my_dest)) {
			log_ebt(" Unable to set up socket connection\n");
			return FAILURE;
		}
	}

	/* Print this machine QP information */
	for (i=0; i < user_param.num_of_qps; i++) {
		ctx_print_pingpong_data(&my_dest[i],&user_comm);
	}

	user_comm.rdma_params->side = REMOTE;

	for (i=0; i < user_param.num_of_qps; i++) {

		if (ctx_hand_shake(&user_comm,&my_dest[i],&rem_dest[i])) {
			log_ebt(" Failed to exchange data between server and clients\n");
			return FAILURE;
		}

		ctx_print_pingpong_data(&rem_dest[i],&user_comm);
	}

	/* An additional handshake is required after moving qp to RTR. */
	if (ctx_hand_shake(&user_comm, &my_dest[0], &rem_dest[0])) {
		log_ebt( "Failed to exchange data between server and clients\n");
		return FAILURE;
	}

	/* For half duplex tests, server just waits for client to exit */
	if (user_param.machine == SERVER && !user_param.duplex) {
		if (user_param.output == FULL_VERBOSITY) {
			printf(RESULT_LINE);
			printf((user_param.report_fmt == MBS ? RESULT_FMT : RESULT_FMT_G));
			printf((user_param.cpu_util_data.enable ? RESULT_EXT_CPU_UTIL : RESULT_EXT));
		}

		if (ctx_hand_shake(&user_comm, &my_dest[0], &rem_dest[0])) {
			log_ebt( " Failed to exchange data between server and clients\n");
			return FAILURE;
		}

		xchg_bw_reports(&user_comm, &my_bw_rep, &rem_bw_rep, atof(user_param.rem_version));
		print_full_bw_report(&user_param, &rem_bw_rep, NULL);

		if (user_param.output == FULL_VERBOSITY) {
			printf(RESULT_LINE);
		}

		if (ctx_close_connection(&user_comm, &my_dest[0], &rem_dest[0])) {
			log_ebt( "Failed to close connection between server and client\n");
			return FAILURE;
		}

		if (user_param.work_rdma_cm == ON) {
			if (destroy_ctx(&ctx, &user_param)) {
				log_ebt( "Failed to destroy resources.\n");
				return FAILURE;
			}

			user_comm.rdma_params->work_rdma_cm = OFF;
			return destroy_ctx(user_comm.rdma_ctx, user_comm.rdma_params);
		}

		return destroy_ctx(&ctx, &user_param);
	}

	if (user_param.use_event) {
		if (ibv_req_notify_cq(ctx.send_cq, 0)) {
			log_ebt( "Couldn't request CQ notification\n");
			return FAILURE;
		}
	}
	if (user_param.output == FULL_VERBOSITY) {
		printf(RESULT_LINE);
		printf((user_param.report_fmt == MBS ? RESULT_FMT : RESULT_FMT_G));
		printf((user_param.cpu_util_data.enable ? RESULT_EXT_CPU_UTIL : RESULT_EXT));
	}

	ctx_set_send_wqes(&ctx, &user_param, rem_dest);

	if (user_param.test_method == RUN_REGULAR || user_param.test_method == RUN_ALL) {

		if (user_param.perform_warm_up) {
			if (perform_warm_up(&ctx, &user_param)) {
				log_ebt( "Problems with warm up\n");
				return FAILURE;
			}
		}

		if (user_param.duplex) {
			if (ctx_hand_shake(&user_comm, &my_dest[0], &rem_dest[0])) {
				log_ebt( "Failed to sync between server and client between different msg sizes\n");
				return FAILURE;
			}
		}

		if (run_iter_bw(&ctx, &user_param)) {
			log_ebt( " Error occurred in run_iter function\n");
			return FAILURE;
		}

		print_report_bw(&user_param, &my_bw_rep);

		if (user_param.duplex) {
			xchg_bw_reports(&user_comm, &my_bw_rep, &rem_bw_rep, atof(user_param.rem_version));
			print_full_bw_report(&user_param, &my_bw_rep, &rem_bw_rep);
		}

		if (user_param.report_both && user_param.duplex) {
			printf(RESULT_LINE);
			printf("\n Local results:\n");
			printf(RESULT_LINE);
			printf((user_param.report_fmt == MBS ? RESULT_FMT : RESULT_FMT_G));
			printf((user_param.cpu_util_data.enable ? RESULT_EXT_CPU_UTIL : RESULT_EXT));
			print_full_bw_report(&user_param, &my_bw_rep, NULL);
			printf(RESULT_LINE);

			printf("\n Remote results:\n");
			printf(RESULT_LINE);
			printf((user_param.report_fmt == MBS ? RESULT_FMT : RESULT_FMT_G));
			printf((user_param.cpu_util_data.enable ? RESULT_EXT_CPU_UTIL : RESULT_EXT));
			print_full_bw_report(&user_param, &rem_bw_rep, NULL);
		}
	} else if (user_param.test_method == RUN_INFINITELY) {

		if (run_iter_bw_infinitely(&ctx, &user_param)) {
			log_ebt( " Error occurred while running infinitely! aborting ...\n");
			return FAILURE;
		}
	}

	if (user_param.output == FULL_VERBOSITY) {
		printf(RESULT_LINE);
	}
	/* For half duplex tests, server just waits for client to exit */
	if (user_param.machine == CLIENT && !user_param.duplex) {

		if (ctx_hand_shake(&user_comm, &my_dest[0], &rem_dest[0])) {
			log_ebt( " Failed to exchange data between server and clients\n");
			return FAILURE;
		}

		xchg_bw_reports(&user_comm, &my_bw_rep, &rem_bw_rep, atof(user_param.rem_version));
	}

	if (ctx_close_connection(&user_comm, &my_dest[0], &rem_dest[0])) {
		log_ebt( "Failed to close connection between server and client\n");
		return FAILURE;
	}

	if (!user_param.is_bw_limit_passed && (user_param.is_limit_bw == ON)) {
		log_ebt( "Error: BW result is below bw limit\n");
		return FAILURE;
	}

	if (!user_param.is_msgrate_limit_passed && (user_param.is_limit_bw == ON)) {
		log_ebt( "Error: Msg rate  is below msg_rate limit\n");
		return FAILURE;
	}

	if (user_param.work_rdma_cm == ON) {
		if (destroy_ctx(&ctx, &user_param)) {
			log_ebt( "Failed to destroy resources.\n");
			return FAILURE;
		}

		user_comm.rdma_params->work_rdma_cm = OFF;
		return destroy_ctx(user_comm.rdma_ctx, user_comm.rdma_params);
	}

	return destroy_ctx(&ctx, &user_param);
}
