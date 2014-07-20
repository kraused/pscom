
#include <stdio.h>
#include <stdlib.h>

#include "pscom.h"

#define	LOOP	10
#define SKIP	2
#define WINSIZE	64
#define SIZE	(1*1024*1024)

static inline double wtime()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);	

	return tv.tv_sec +  tv.tv_usec*1e-6;
}

int main(int argc, char **argv)
{
	double t0, t1;
	int client;
	int i, j;
	int ret;
	int loop, skip, winsize, size;
	const char *server;
	char *buf;
	pscom_socket_t *sock;
	pscom_connection_t *con;
	pscom_request_t *req[WINSIZE+1];


	loop    = LOOP;
	skip    = SKIP;
	winsize = WINSIZE;
	size    = SIZE;

	buf = malloc(size);
	for (i = 0; i < size; ++i)
		buf[i] = 0;

	client = 0;

	for (i = 1; i < argc; ++argc) {
		if (('-' == argv[i][0]) && ('c' == argv[i][1])) {
			client = 1;
			server = argv[i+1];
			break;
		}
	}

	ret = pscom_init(PSCOM_VERSION);
	if (PSCOM_SUCCESS != ret) {
		fprintf(stderr, "pscom_init failed: %s\n", pscom_err_str(ret));
		return 1;
	}

	sock = pscom_open_socket(0, 0);

	for (j = 0; j < winsize; ++j)
		req[j] = pscom_request_create(4, 0);
	req[winsize] = 0;

	if (client) {
		con = pscom_open_connection(sock);
		if (!con) {
			fprintf(stderr, "pscom_open_connection failed: %s\n",
			        pscom_err_str(ret));
			return 1;
		}

		ret = pscom_connect_socket_str(con, server);
		if (PSCOM_SUCCESS != ret) {
			fprintf(stderr, "pscom_connect_socket_str failed: %s\n", 
			        pscom_err_str(ret));
			return 1;
		}

		for (i = 0; i < loop + skip; ++i) {
			if (skip == i) {
				t0 = wtime();
			}

			for (j = 0; j < winsize; ++j) {
				pscom_req_prepare(req[j], con, buf, size, NULL, 4);
				pscom_post_send(req[j]);
			}

			pscom_wait_all(req);

			pscom_req_prepare(req[0], con, buf, 4, NULL, 4);
			pscom_post_recv(req[0]);
			pscom_wait(req[0]);
		}

		t1 = wtime();
		printf(" %g\n", (size * 1.0 * loop * winsize)/((t1 - t0) * 1024 * 1024));
	} else {
		pscom_listen(sock, 7100);

		do {
			pscom_wait_any();
			con = pscom_get_next_connection(sock, NULL);
		} while (!con);

		for (i = 0; i < loop + skip; ++i) {
			for (j = 0; j < winsize; ++j) {
				pscom_req_prepare(req[j], con, buf, size, NULL, 4);
				pscom_post_recv(req[j]);
			}

			pscom_wait_all(req);

			pscom_req_prepare(req[0], con, buf, 4, NULL, 4);
			pscom_post_send(req[0]);
			pscom_wait(req[0]);
		}
	}

	for (j = 0; j < winsize; ++j)
		pscom_request_free(req[j]);

	return 0;
}

