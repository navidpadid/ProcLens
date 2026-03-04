// SPDX-License-Identifier: MIT
// Simple multi-threaded test program for E2E testing
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define NUM_THREADS 4

void *worker_thread(void *arg)
{
	long thread_id = (long)arg;
	long i;

	printf("Thread %ld started\n", thread_id);

	/* Do some work to show CPU usage and keep thread alive */
	for (i = 0; i < 1000000; i++) {
		/* Light CPU work */
		long tmp = i * i;
		(void)tmp;

		/* Sleep occasionally to avoid 100% CPU */
		if (i % 10000 == 0)
			usleep(100000); /* Sleep 100ms */
	}

	printf("Thread %ld finished\n", thread_id);
	return NULL;
}

int main(void)
{
	pthread_t threads[NUM_THREADS];
	struct sockaddr_in tcp_addr, udp_addr;
	struct sockaddr_un unix_addr;
	int tcp_sock = -1, udp_sock = -1, unix_sock = -1;
	long i;
	int rc;

	printf("Multi-threaded test application with sockets\n");
	printf("Main PID: %d\n", getpid());

	/* Create a TCP listening socket (IPv4) */
	tcp_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (tcp_sock >= 0) {
		memset(&tcp_addr, 0, sizeof(tcp_addr));
		tcp_addr.sin_family = AF_INET;
		tcp_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		tcp_addr.sin_port = htons(12345);

		/* Allow reuse to avoid "Address already in use" errors */
		int opt = 1;

		setsockopt(tcp_sock, SOL_SOCKET, SO_REUSEADDR, &opt,
			   sizeof(opt));

		if (bind(tcp_sock, (struct sockaddr *)&tcp_addr,
			 sizeof(tcp_addr)) == 0) {
			listen(tcp_sock, 5);
			printf("TCP socket listening on 127.0.0.1:12345 "
			       "(fd=%d)\n",
			       tcp_sock);
		} else {
			perror("TCP bind failed");
			close(tcp_sock);
			tcp_sock = -1;
		}
	}

	/* Create a UDP socket (IPv4) */
	udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (udp_sock >= 0) {
		memset(&udp_addr, 0, sizeof(udp_addr));
		udp_addr.sin_family = AF_INET;
		udp_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		udp_addr.sin_port = htons(12346);

		if (bind(udp_sock, (struct sockaddr *)&udp_addr,
			 sizeof(udp_addr)) == 0) {
			printf("UDP socket bound to 127.0.0.1:12346 (fd=%d)\n",
			       udp_sock);
		} else {
			perror("UDP bind failed");
			close(udp_sock);
			udp_sock = -1;
		}
	}

	/* Create a Unix domain socket */
	unix_sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (unix_sock >= 0) {
		memset(&unix_addr, 0, sizeof(unix_addr));
		unix_addr.sun_family = AF_UNIX;
		snprintf(unix_addr.sun_path, sizeof(unix_addr.sun_path),
			 "/tmp/test_multithread_%d.sock", getpid());

		/* Remove any existing socket file */
		unlink(unix_addr.sun_path);

		if (bind(unix_sock, (struct sockaddr *)&unix_addr,
			 sizeof(unix_addr)) == 0) {
			listen(unix_sock, 5);
			printf("Unix socket listening at %s (fd=%d)\n",
			       unix_addr.sun_path, unix_sock);
		} else {
			perror("Unix socket bind failed");
			close(unix_sock);
			unix_sock = -1;
		}
	}

	printf("\nCreating %d threads...\n\n", NUM_THREADS);

	/* Create worker threads */
	for (i = 0; i < NUM_THREADS; i++) {
		rc = pthread_create(&threads[i], NULL, worker_thread,
				    (void *)i);
		if (rc) {
			fprintf(stderr, "Error creating thread %ld: %d\n", i,
				rc);
			exit(1);
		}
	}

	/* Wait for threads to complete */
	for (i = 0; i < NUM_THREADS; i++)
		pthread_join(threads[i], NULL);

	printf("\nAll threads completed\n");
	printf("Total threads (%s + workers): %d\n", __func__, NUM_THREADS + 1);

	/* Clean up sockets */
	if (tcp_sock >= 0) {
		close(tcp_sock);
		printf("Closed TCP socket\n");
	}
	if (udp_sock >= 0) {
		close(udp_sock);
		printf("Closed UDP socket\n");
	}
	if (unix_sock >= 0) {
		close(unix_sock);
		unlink(unix_addr.sun_path);
		printf("Closed Unix socket\n");
	}

	return 0;
}
