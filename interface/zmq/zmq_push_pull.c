/*
 * Copyright (c) 2020 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <netinet/in.h>

#include "zmq_push_pull.h"

void *zmqpull_sockctxt;
void *zmqpull_sockcet;
void *zmqpush_sockctxt;
void *zmqpush_sockcet;

int zmq_pull_create(void)
{
	/* Socket to talk to Client */
	zmqpull_sockctxt = zmq_ctx_new();
	zmqpull_sockcet = zmq_socket(zmqpull_sockctxt, ZMQ_PULL);

#ifdef CP_BUILD
	int rc = zmq_connect(zmqpull_sockcet, zmq_pull_ifconnect);
	assert(rc == 0);

	printf("ZMQ Server connect to:\t%s\t\t; device:\t%s\n",
			zmq_pull_ifconnect, ZMQ_DEV_ID);
#else
	int rc = zmq_connect(zmqpull_sockcet, zmq_pull_ifconnect);
	assert(rc == 0);

	printf("ZMQ Server connect to:\t%s\t\t; device:\t%s\n",
	zmq_pull_ifconnect, ZMQ_DEV_ID);
#endif	/* CP_BUILD */

	return rc;
}

int
zmq_push_create(void)
{
	/* Socket to talk to Server */
	zmqpush_sockctxt = zmq_ctx_new();
	zmqpush_sockcet = zmq_socket(zmqpush_sockctxt, ZMQ_PUSH);

#ifdef CP_BUILD
	int rc = zmq_connect(zmqpush_sockcet, zmq_push_ifconnect);
	assert(rc == 0);

	printf("ZMQ Client connect to:\t%s\t\t; device:\t%s\n",
			zmq_push_ifconnect, ZMQ_DEV_ID);
#else
	int rc = zmq_connect(zmqpush_sockcet, zmq_push_ifconnect);
	assert(rc == 0);

	printf("ZMQ Client connect to:\t%s\t\t; device:\t%s\n",
	                zmq_push_ifconnect, ZMQ_DEV_ID);
#endif  /* CP_BUILD */

	return rc;
}

void
zmq_push_pull_destroy(void)
{
	zmq_close(zmqpull_sockcet);
	zmq_close(zmqpush_sockcet);
	zmq_ctx_destroy(zmqpull_sockctxt);
	zmq_ctx_destroy(zmqpush_sockctxt);
}

int
zmq_mbuf_push(void *mbuf, uint32_t zmqbufsz)
{
#ifdef SIMU_CP
	return zmq_send(zmqpush_sockcet, mbuf, zmqbufsz, 0);
#else
	return zmq_send(zmqpush_sockcet, mbuf, zmqbufsz, ZMQ_DONTWAIT);
#endif
}

int
zmq_mbuf_pull(void *buf, uint32_t zmqbufsz)
{
#ifdef SIMU_CP
	return zmq_recv(zmqpull_sockcet, buf, zmqbufsz, 0);
#else
	return zmq_recv(zmqpull_sockcet, buf, zmqbufsz, ZMQ_DONTWAIT);
#endif
}

