
#include <stdio.h>
#include <stdlib.h>

#include "pscom.h"

#define	LOOP	20
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
	char *buf1;
	char *buf2;
	pscom_socket_t *sock;
	pscom_connection_t *con;
	pscom_request_t *req1[WINSIZE+1];
	pscom_request_t *req2[WINSIZE+1];


	loop    = LOOP;
	skip    = SKIP;
	winsize = WINSIZE;
	size    = SIZE;

	buf1 = malloc(size);
	buf2 = malloc(size);
	for (i = 0; i < size; ++i)
		buf1[i] = 0;
	for (i = 0; i < size; ++i)
		buf2[i] = 0;

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
		req1[j] = pscom_request_create(4, 0);
	req1[winsize] = 0;
	for (j = 0; j < winsize; ++j)
		req2[j] = pscom_request_create(4, 0);
	req2[winsize] = 0;

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
				pscom_req_prepare(req1[j], con, buf1, size, NULL, 4);
				pscom_post_recv(req1[j]);
			}
			
			for (j = 0; j < winsize; ++j) {
				pscom_req_prepare(req2[j], con, buf2, size, NULL, 4);
				pscom_post_send(req2[j]);
			}

			pscom_wait_all(req1);
			pscom_wait_all(req2);
		}

		t1 = wtime();
		printf(" %g\n", (2 * size * 1.0 * loop * winsize)/((t1 - t0) * 1024 * 1024));

		pscom_close_socket(sock);
		pscom_close_connection(con);
	} else {
		pscom_listen(sock, 7100);

		do {
			pscom_wait_any();
			con = pscom_get_next_connection(sock, NULL);
		} while (!con);

		for (i = 0; i < loop + skip; ++i) {
			for (j = 0; j < winsize; ++j) {
				pscom_req_prepare(req1[j], con, buf1, size, NULL, 4);
				pscom_post_recv(req1[j]);
			}

			for (j = 0; j < winsize; ++j) {
				pscom_req_prepare(req2[j], con, buf2, size, NULL, 4);
				pscom_post_send(req2[j]);
			}

			pscom_wait_all(req1);
			pscom_wait_all(req2);
		}

		pscom_close_socket(sock);
		pscom_close_connection(con);
	}

	for (j = 0; j < winsize; ++j)
		pscom_request_free(req1[j]);
	for (j = 0; j < winsize; ++j)
		pscom_request_free(req2[j]);

	return 0;
}

