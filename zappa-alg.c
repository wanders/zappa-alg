/*
 * Copyright: Copyright (c) 2013, Anders Waldenborg <anders@0x63.nu>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name(s) of the above copyright
 * holders shall not be used in advertising or otherwise to promote the
 * sale, use or other dealings in this Software without prior written
 * authorization.
 */


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <stdarg.h>
#include <assert.h>

#include "insane-macros.h"

enum clientstate {
	FREE, LISTENING, ACCEPTED,
};

struct client {
	enum clientstate state;

	int port;
	struct in_addr addr;

	int mc_send_fd;
	int listen_fd;
	int accept_fd;
	int connect_fd;
};

struct fddata {
	void (*handler) (int, int, struct client *);
	struct client *client;
};


#define MAX_CLIENTS 10
#define MAX_FDS (MAX_CLIENTS * 4 + 1)

static struct client clients[MAX_CLIENTS];

static struct in_addr ip_outside;
static struct in_addr ip_inside;
static struct in_addr ip_mcaddr;

static void
debug (const char *fmt, ...)
{
	va_list va;
	va_start (va, fmt);
	vfprintf (stderr, fmt, va);
	va_end (va);
}


static struct client *
find_client (struct in_addr addr)
{
	foreach_array (client, clients) {
		if (client->state != FREE && client->addr.s_addr == addr.s_addr)
			return client;
	}
	return NULL;
}

static struct client *
new_client (void)
{
	foreach_array (client, clients) {
		if (client->state == FREE)
			return client;
	}
	return NULL;
}


static void
addfd (struct pollfd *fds, struct fddata *fddata, int *fdcnt, int fd, int events, void (*handler) (int, int, struct client *), struct client *client)
{
	fds[*fdcnt].fd = fd;
	fds[*fdcnt].events = events;
	fddata[*fdcnt].handler = handler;
	fddata[*fdcnt].client = client;
	(*fdcnt)++;
}

static void
hexdump (const char *b, int len)
{
	const unsigned char *buf = (const unsigned char *)b;

	for (int i = 0; i < len; i += 16) {
		int j;
		for (j = i; j < i + 16 && j < len; j++) {
			debug ("%02x ", buf[j]);
			if (j == i + 7)
				debug (" ");
		}


		if (j < 7)
			debug (" ");
		for (; j < i + 16; j++)
			debug ("   ");

		debug (" ");

		for (j = i; j < i + 16 && j < len; j++) {
			debug ("%c", buf[j] >= 32 && buf[j] < 127 ? buf[j] : '.');
			if (j == i + 7)
				debug (" ");
		}

		debug ("\n");
	}
}

static void
handle_mc (int sock, int revents, struct client *_client)
{
	char buf[4096];
	struct sockaddr_in addr;
	socklen_t l = sizeof (addr);
	ssize_t r;

	assert (_client == NULL);

	if (!(revents & POLLIN))
		return;

	errexit (r = recvfrom (sock, buf, sizeof (buf), 0, (struct sockaddr *)&addr, &l));

	debug ("Recieved %d bytes of data from %s:%d\n", r, inet_ntoa (addr.sin_addr), addr.sin_port);
	hexdump (buf, r);


	struct client *client = find_client (addr.sin_addr);
	if (!client) {
		client = new_client ();

		client->addr = addr.sin_addr;
		client->port = addr.sin_port;
		client->state = LISTENING;

		errexit (client->mc_send_fd = socket (AF_INET, SOCK_DGRAM, 0));

		errexit (bind (client->mc_send_fd, INET_SOCKADDR (ip_outside, addr.sin_port), INET_SOCKADDR_L));


		errexit (client->listen_fd = socket (AF_INET, SOCK_STREAM, 0));

		errexit (bind (client->listen_fd, INET_SOCKADDR (ip_outside, addr.sin_port), INET_SOCKADDR_L));

		errexit (listen (client->listen_fd, 1));

	}
	if (client->port != addr.sin_port) {
		debug ("Client %s switched port (from %d to %d)\n", inet_ntoa (client->addr), client->port, addr.sin_port);
		client->port = addr.sin_port;
	}

	errexit (sendto (client->mc_send_fd, buf, r, 0, INET_SOCKADDR (ip_mcaddr, 5555), INET_SOCKADDR_L));
}


static void
handle_tcpredir_listenfd (int sock, int revents, struct client *client)
{
	struct sockaddr_in addr;
	socklen_t l = sizeof (addr);

	assert (sock == client->listen_fd);

	if (!(revents & POLLIN))
		return;


	errexit (client->accept_fd = accept (sock, (struct sockaddr *)&addr, &l));

	debug ("Accepted TCP from %s:%d\n", inet_ntoa (addr.sin_addr), addr.sin_port);

	errexit (client->connect_fd = socket (AF_INET, SOCK_STREAM, 0));

	debug ("Connecting TCP to %s:%d\n", inet_ntoa (client->addr), client->port);

	errexit (connect (client->connect_fd, INET_SOCKADDR (client->addr, client->port), INET_SOCKADDR_L));

	client->state = ACCEPTED;

}



static void
handle_tcpredir_fd (int sock, int revents, struct client *client)
{
	char buf[4096];
	ssize_t r;

	assert (client != NULL);

	if (!(revents & POLLIN))
		return;

	if (sock == client->connect_fd) {
		errexit (r = recv (client->connect_fd, buf, sizeof (buf), 0));
		if (r == 0)
			goto disconnect;
		debug ("TCP Recieved outbound %d bytes\n", r);
		hexdump (buf, r);
		errexit (send (client->accept_fd, buf, r, 0));
	} else if (sock == client->accept_fd) {
		errexit (r = recv (client->accept_fd, buf, sizeof (buf), 0));
		if (r == 0)
			goto disconnect;
		debug ("TCP Recieved inbound %d bytes\n", r);
		hexdump (buf, r);
		errexit (send (client->connect_fd, buf, r, 0));
	} else {
		assert (0);
	}

	return;

disconnect:
	close (client->connect_fd);
	close (client->accept_fd);
	client->state = LISTENING;
}


int main(int argc, char **argv) {
	int sock;

	if (argc != 3) {
		fprintf (stderr, "Usage: zappa-alg inside-ip outside-ip\n");
		exit (1);
	}

	errexit (inet_aton (argv[1], &ip_inside));
	errexit (inet_aton (argv[2], &ip_outside));
	errexit (inet_aton ("239.16.16.195", &ip_mcaddr));




	errexit (sock = socket (AF_INET, SOCK_DGRAM, 0));
	errexit (setsockopt (sock, SOL_SOCKET, SO_REUSEADDR, CONSTP(int, 1), sizeof (int)));

	errexit (bind (sock, INET_SOCKADDR ({INADDR_ANY}, 5555), INET_SOCKADDR_L));

	struct ip_mreqn mr = {.imr_ifindex = 0,
	                      .imr_multiaddr = ip_mcaddr,
	                      .imr_address = ip_inside};
	errexit (setsockopt (sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mr, sizeof (mr)));


	for (;;) {
		struct pollfd fds[MAX_FDS];
		struct fddata fddata[MAX_FDS];
		int fdcnt = 0;

		addfd (fds, fddata, &fdcnt, sock, POLLIN, handle_mc, NULL);

		for (unsigned int i = 0; i < N_ENTRIES (clients); i++) {
			if (clients[i].state == ACCEPTED) {
				addfd (fds, fddata, &fdcnt, clients[i].accept_fd, POLLIN, handle_tcpredir_fd, &clients[i]);
				addfd (fds, fddata, &fdcnt, clients[i].connect_fd, POLLIN, handle_tcpredir_fd, &clients[i]);
			}
			if (clients[i].state == LISTENING) {
				addfd (fds, fddata, &fdcnt, clients[i].listen_fd, POLLIN, handle_tcpredir_listenfd, &clients[i]);
			}
		}

		errexit (poll (fds, fdcnt, 0));

		for (int i = 0; i < fdcnt; i++) {
			if (fds[i].revents) {
				debug ("Event on %d\n", i);
				fddata[i].handler (fds[i].fd, fds[i].revents, fddata[i].client);
			}
		}


	}
	return 0;
}
