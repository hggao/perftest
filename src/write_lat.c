/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies Ltd.  All rights reserved.
 * Copyright (c) 2005 Hewlett Packard, Inc (Grant Grundler)
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
#include <unistd.h>
#if !defined(__FreeBSD__)
#include <malloc.h>
#endif

#include "get_clock.h"
#include "perftest_logging.h"
#include "perftest_parameters.h"
#include "perftest_resources.h"
#include "perftest_communication.h"

/******************************************************************************
 *
 ******************************************************************************/
int main(int argc, char *argv[])
{
	int				ret_parser, i = 0, rc;
	struct report_options		report;
	struct pingpong_context		ctx;
	struct pingpong_dest		*my_dest  = NULL;
	struct pingpong_dest		*rem_dest = NULL;
	struct ibv_device		*ib_dev;
	struct perftest_parameters	user_param;
	struct perftest_comm		user_comm;

	/* init default values to user's parameters */
	memset(&ctx,0,sizeof(struct pingpong_context));
	memset(&user_param, 0, sizeof(struct perftest_parameters));
	memset(&user_comm,0,sizeof(struct perftest_comm));

	user_param.verb    = WRITE;
	user_param.tst     = LAT;
	user_param.r_flag  = &report;
	strncpy(user_param.version, VERSION, sizeof(user_param.version));

	/* Configure the parameters values according to user arguments or defalut values. */
	ret_parser = parser(&user_param,argv,argc);
	if (ret_parser) {
		if (ret_parser != VERSION_EXIT && ret_parser != HELP_EXIT)
			log_ebt(" Parser function exited with Error\n");
		return FAILURE;
	}

	/* In case of ib_write_lat, PCI relaxed ordering should be disabled since we're polling for data change
	 * of last packet so in case of relaxed odering we might get the last packet in wrong order thus the test
	 * would be incorrect
	 */
	user_param.disable_pcir = 1;

	if (user_param.use_xrc || user_param.connection_type == DC) {
		user_param.num_of_qps *= 2;
	}

	/* Finding the IB device selected (or defalut if no selected). */
	ib_dev = ctx_find_dev(&user_param.ib_devname);
	if (!ib_dev) {
		log_ebt(" Unable to find the Infiniband/RoCE device\n");
		return FAILURE;
	}

	/* Getting the relevant context from the device */
	ctx.context = ctx_open_device(ib_dev, &user_param);
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
	if (check_link(ctx.context,&user_param)) {
		log_ebt( " Couldn't get context for the device\n");
		return FAILURE;
	}

	/* copy the relevant user parameters to the comm struct + creating rdma_cm resources. */
	if (create_comm_struct(&user_comm,&user_param)) {
		log_ebt(" Unable to create RDMA_CM resources\n");
		return FAILURE;
	}

	if (user_param.output == FULL_VERBOSITY && user_param.machine == SERVER) {
		printf("\n************************************\n");
		printf("* Waiting for client to connect... *\n");
		printf("************************************\n");
	}

	/* Initialize the connection and print the local data. */
	if (establish_connection(&user_comm)) {
		log_ebt(" Unable to init the socket connection\n");
		return FAILURE;
	}

	exchange_versions(&user_comm, &user_param);
	check_version_compatibility(&user_param);
	check_sys_data(&user_comm, &user_param);

	/* See if MTU and link type are valid and supported. */
	if (check_mtu(ctx.context,&user_param, &user_comm)) {
		log_ebt( " Couldn't get context for the device\n");
		return FAILURE;
	}

	ALLOCATE(my_dest , struct pingpong_dest , user_param.num_of_qps);
	memset(my_dest, 0, sizeof(struct pingpong_dest)*user_param.num_of_qps);
	ALLOCATE(rem_dest , struct pingpong_dest , user_param.num_of_qps);
	memset(rem_dest, 0, sizeof(struct pingpong_dest)*user_param.num_of_qps);

	/* Allocating arrays needed for the test. */
	alloc_ctx(&ctx,&user_param);

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
		/* create all the basic IB resources (data buffer, PD, MR, CQ and events channel) */
		if (ctx_init(&ctx,&user_param)) {
			log_ebt( " Couldn't create IB resources\n");
			return FAILURE;
		}
	}

	/* Set up the Connection. */
	if (set_up_connection(&ctx,&user_param,my_dest)) {
		log_ebt(" Unable to set up socket connection\n");
		return FAILURE;
	}

	/* Print basic test information. */
	ctx_print_test_info(&user_param);

	/* shaking hands and gather the other side info. */
	if (ctx_hand_shake(&user_comm,my_dest,rem_dest)) {
		log_ebt("Failed to exchange data between server and clients\n");
		return FAILURE;
	}

	for (i=0; i < user_param.num_of_qps; i++) {

		/* shaking hands and gather the other side info. */
		if (ctx_hand_shake(&user_comm,&my_dest[i],&rem_dest[i])) {
			log_ebt("Failed to exchange data between server and clients\n");
			return FAILURE;
		}

	};

	if (user_param.work_rdma_cm == OFF) {
		if (ctx_check_gid_compatibility(&my_dest[0], &rem_dest[0])) {
			log_ebt("\n Found Incompatibility issue with GID types.\n");
			log_ebt(" Please Try to use a different IP version.\n\n");
			return FAILURE;
		}
	}

	if (user_param.work_rdma_cm == OFF) {
		if (ctx_connect(&ctx,rem_dest,&user_param,my_dest)) {
			log_ebt(" Unable to Connect the HCA's through the link\n");
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
	for (i=0; i < user_param.num_of_qps; i++)
		ctx_print_pingpong_data(&my_dest[i],&user_comm);

	user_comm.rdma_params->side = REMOTE;

	for (i=0; i < user_param.num_of_qps; i++) {

		if (ctx_hand_shake(&user_comm,&my_dest[i],&rem_dest[i])) {
			log_ebt(" Failed to exchange data between server and clients\n");
			return FAILURE;
		}

		ctx_print_pingpong_data(&rem_dest[i],&user_comm);
	}

	/* An additional handshake is required after moving qp to RTR. */
	if (ctx_hand_shake(&user_comm,my_dest,rem_dest)) {
		log_ebt("Failed to exchange data between server and clients\n");
		return FAILURE;
	}

	ctx_set_send_wqes(&ctx,&user_param,rem_dest);

	if (user_param.output == FULL_VERBOSITY) {
		printf(RESULT_LINE);
		printf("%s",(user_param.test_type == ITERATIONS) ? RESULT_FMT_LAT : RESULT_FMT_LAT_DUR);
		printf((user_param.cpu_util_data.enable ? RESULT_EXT_CPU_UTIL : RESULT_EXT));
	}

	if (user_param.test_method == RUN_ALL) {

		for (i = 1; i < 24 ; ++i) {
			user_param.size = (uint64_t)1 << i;
			if(run_iter_lat_write(&ctx,&user_param)) {
				log_ebt("Test exited with Error\n");
				return FAILURE;
			}

			user_param.test_type == ITERATIONS ? print_report_lat(&user_param) : print_report_lat_duration(&user_param);
		}

	} else {

		if(run_iter_lat_write(&ctx,&user_param)) {
			log_ebt("Test exited with Error\n");
			return FAILURE;
		}
		user_param.test_type == ITERATIONS ? print_report_lat(&user_param) : print_report_lat_duration(&user_param);
	}

	if (user_param.output == FULL_VERBOSITY) {
		printf(RESULT_LINE);
	}
	return 0;
}
