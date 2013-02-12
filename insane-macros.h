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

#ifndef _INSANE_MACROS_H__
#define _INSANE_MACROS_H__


/*
 * Standard stuff
 */

#define STRINGIFY_NOEXPAND(x) #x
#define STRINGIFY(x) STRINGIFY_NOEXPAND(x)

#define FILE_LINE __FILE__ ":" STRINGIFY(__LINE__)

#define N_ENTRIES(x) (sizeof (x) / sizeof ((x)[0]))


/**
 * errexit macro
 *
 * Exit program if expression is less than zero. Prints the expression
 * and errno and exits.
 *
 * Example:
 *	errexit (sock = socket (AF_INET, SOCK_DGRAM, 0));
 *
 */

#define errexit(x) _errexit (x, FILE_LINE ": " #x)

static void
_errexit (int r, const char *msg)
{
	if (r >= 0)
		return;

	perror (msg);
	exit (1);
}



/**
 * Macro for creating sockaddr_in structures.
 *
 *
 *
 * Examples:
 * 	bind (sock, INET_SOCKADDR (INADDR_ANY, 5555), INET_SOCKADDR_L);
 *
 */

#define INET_SOCKADDR(addr, port) (struct sockaddr *) \
    &(struct sockaddr_in){                            \
        .sin_family = AF_INET,                        \
        .sin_port = (port),                           \
        .sin_addr = addr                              \
    }
#define INET_SOCKADDR_L sizeof (struct sockaddr_in)






/**
 * Iterate over array.
 *
 * sometype_t myarray[10];
 * foreach_array (e, myarray) {
 *     dosomething(e);
 * }
 */
#define foreach_array_index(entry, array, idx) for (unsigned int idx = 0; idx < N_ENTRIES (array); idx++) for (typeof(array[0]) *entry = &array[idx]; entry; entry = NULL)
#define foreach_array(entry, array) foreach_array_index(entry, array, __array_idx ## __COUNTER__)

/**
 * Pointer to a integer.
 *
 * For use with setsockopt and similar functions.
 *
 * "foo(&1)" is not allowed. Writing "int i=1; foo(&i);" is
 * boring. This macro allows writing: "foo(CONSTP(int, 1))"
 *
 * setsockopt (sock, SOL_SOCKET, SO_REUSEADDR, CONSTP(int, 1), sizeof (int))
 */
#define CONSTP(type, value) (&((struct {type _;}){(value)}._))

#endif
