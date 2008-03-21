/* Copyright (C) 2008 Board of Trustees, Leland Stanford Jr. University.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "vlog-socket.h"
#include <errno.h>
#include <sys/un.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "fatal-signal.h"
#include "util.h"
#include "vlog.h"

#ifndef SCM_CREDENTIALS
#include <time.h>
#endif

static int make_unix_socket(bool nonblock, bool passcred,
                            const char *bind_path, const char *connect_path);

/* Server for Vlog control connection. */
struct vlog_server {
    char *path;
    int fd;
};

/* Start listening for connections from clients and processing their
 * requests.  'path' may be:
 *
 *      - NULL, in which case the default socket path is used.  (Only one
 *        Vlog_server_socket per process can use the default path.)
 *
 *      - A name that does not start with '/', in which case it is appended to
 *        the default socket path.
 *
 *      - An absolute path (starting with '/') that gives the exact name of
 *        the Unix domain socket to listen on.
 *
 * Returns 0 if successful, otherwise a positive errno value.  If successful,
 * sets '*serverp' to the new vlog_server, otherwise to NULL. */
int
vlog_server_listen(const char *path, struct vlog_server **serverp)
{
    struct vlog_server *server = xmalloc(sizeof *server);

    if (path && path[0] == '/') {
        server->path = xstrdup(path);
    } else {
        server->path = xasprintf("/tmp/vlogs.%ld%s",
                                 (long int) getpid(), path ? path : "");
    }

    server->fd = make_unix_socket(true, true, server->path, NULL);
    if (server->fd < 0) {
        int fd = server->fd;
        free(server->path);
        free(server);
        fprintf(stderr, "Could not initialize vlog configuration socket: %s\n",
                strerror(-server->fd));
        *serverp = NULL;
        return fd;
    }
    *serverp = server;
    return 0;
}

/* Destroys 'server' and stops listening for connections. */
void
vlog_server_close(struct vlog_server *server)
{
    if (server) {
        close(server->fd);
        unlink(server->path);
        fatal_signal_remove_file_to_unlink(server->path);
        free(server->path);
        free(server);
    }
}

/* Returns the fd used by 'server'.  The caller can poll this fd (POLLIN) to
 * determine when to call vlog_server_poll(). */
int
vlog_server_get_fd(const struct vlog_server *server)
{
    return server->fd;
}

static int
recv_with_creds(const struct vlog_server *server,
                char *cmd_buf, size_t cmd_buf_size,
                struct sockaddr_un *un, socklen_t *un_len)
{
#ifdef SCM_CREDENTIALS
    /* Read a message and control messages from 'fd'.  */
    char cred_buf[CMSG_SPACE(sizeof(struct ucred))];
    ssize_t n;
    struct iovec iov;
    struct msghdr msg;
    struct ucred* cred;
    struct cmsghdr* cmsg;

    iov.iov_base = cmd_buf;
    iov.iov_len = cmd_buf_size - 1;

    memset(&msg, 0, sizeof msg);
    msg.msg_name = un;
    msg.msg_namelen = sizeof *un;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cred_buf;
    msg.msg_controllen = sizeof cred_buf;

    n = recvmsg(server->fd, &msg, 0);
    *un_len = msg.msg_namelen;
    if (n < 0) {
        return errno;
    }
    cmd_buf[n] = '\0';

    /* Ensure that the message has credentials ensuring that it was sent
     * from the same user who started us, or by root. */
    cred = NULL;
    for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL;
         cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if (cmsg->cmsg_level == SOL_SOCKET
            && cmsg->cmsg_type == SCM_CREDENTIALS) {
            cred = (struct ucred *) CMSG_DATA(cmsg);
        } else if (cmsg->cmsg_level == SOL_SOCKET
                   && cmsg->cmsg_type == SCM_RIGHTS) {
            /* Anyone can send us fds.  If we don't close them, then that's
             * a DoS: the sender can overflow our fd table. */
            int* fds = (int *) CMSG_DATA(cmsg);
            size_t n_fds = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof *fds;
            size_t i;
            for (i = 0; i < n_fds; i++) {
                close(fds[i]);
            }
        }
    }
    if (!cred) {
        fprintf(stderr, "vlog: config message lacks credentials\n");
        return -1;
    } else if (cred->uid && cred->uid != getuid()) {
        fprintf(stderr, "vlog: config message uid=%ld is not 0 or %ld\n",
                (long int) cred->uid, (long int) getuid());
        return -1;
    }

    return 0;
#else /* !SCM_CREDENTIALS */
    socklen_t len;
    ssize_t n;
    struct stat s;
    time_t recent;

    /* Receive a message. */
    len = sizeof *un;
    n = recvfrom(server->fd, cmd_buf, cmd_buf_size - 1, 0,
                 (struct sockaddr *) un, &len);
    *un_len = len;
    if (n < 0) {
        return errno;
    }
    cmd_buf[n] = '\0';

    len -= offsetof(struct sockaddr_un, sun_path);
    un->sun_path[len] = '\0';
    if (stat(un->sun_path, &s) < 0) {
        fprintf(stderr, "vlog: config message from inaccessible socket: %s\n",
                strerror(errno));
        return -1;
    }
    if (!S_ISSOCK(s.st_mode)) {
        fprintf(stderr, "vlog: config message not from a socket\n");
        return -1;
    }
    recent = time(0) - 30;
    if (s.st_atime < recent || s.st_ctime < recent || s.st_mtime < recent) {
        fprintf(stderr, "vlog: config socket too old\n");
        return -1;
    }
    if (s.st_uid && s.st_uid != getuid()) {
        fprintf(stderr, "vlog: config message uid=%ld is not 0 or %ld\n",
                (long int) s.st_uid, (long int) getuid());
        return -1;
    }
    return 0;
#endif /* !SCM_CREDENTIALS */
}

/* Processes incoming requests for 'server'. */
void
vlog_server_poll(struct vlog_server *server)
{
    for (;;) {
        char cmd_buf[512];
        struct sockaddr_un un;
        socklen_t un_len;
        char *reply;
        int error;

        error = recv_with_creds(server, cmd_buf, sizeof cmd_buf, &un, &un_len);
        if (error > 0) {
            if (error != EAGAIN && error != EWOULDBLOCK) {
                fprintf(stderr, "vlog: reading configuration socket: %s",
                        strerror(errno));
            }
            return;
        } else if (error < 0) {
            continue;
        }

        /* Process message and send reply. */
        if (!strncmp(cmd_buf, "set ", 4)) {
            char *msg = vlog_set_levels_from_string(cmd_buf + 4);
            reply = msg ? msg : xstrdup("ack");
        } else if (!strcmp(cmd_buf, "list")) {
            reply = vlog_get_levels();
        } else {
            reply = xstrdup("nak");
        }
        sendto(server->fd, reply, strlen(reply), 0,
               (struct sockaddr*) &un, un_len);
        free(reply);
    }
}

/* Client for Vlog control connection. */

struct vlog_client {
    char *connect_path;
    char *bind_path;
    int fd;
};

/* Connects to a Vlog server socket.  If 'path' does not start with '/', then
 * it start with a PID as a string.  If a non-null, non-absolute name was
 * passed to Vlog_server_socket::listen(), then it must follow the PID in
 * 'path'.  If 'path' starts with '/', then it must be an absolute path that
 * gives the exact name of the Unix domain socket to connect to.
 *
 * Returns 0 if successful, otherwise a positive errno value.  If successful,
 * sets '*clientp' to the new vlog_client, otherwise to NULL. */
int
vlog_client_connect(const char *path, struct vlog_client **clientp)
{
    struct vlog_client *client;
    int fd;

    client = xmalloc(sizeof *client);
    client->connect_path = (path[0] == '/'
                            ? xstrdup(path)
                            : xasprintf("/tmp/vlogs.%s", path));

    client->bind_path = xasprintf("/tmp/vlog.%ld", (long int) getpid());
    fd = make_unix_socket(false, false,
                          client->bind_path, client->connect_path);

    if (fd >= 0) {
        client->fd = fd;
        *clientp = client;
        return 0;
    } else {
        free(client->connect_path);
        free(client->bind_path);
        free(client);
        *clientp = NULL;
        return errno;
    }
}

/* Destroys 'client'. */
void
vlog_client_close(struct vlog_client *client)
{
    if (client) {
        unlink(client->bind_path);
        fatal_signal_remove_file_to_unlink(client->bind_path);
        free(client->bind_path);
        free(client->connect_path);
        close(client->fd);
        free(client);
    }
}

/* Sends 'request' to the server socket that 'client' is connected to.  Returns
 * 0 if successful, otherwise a positive errno value. */
int
vlog_client_send(struct vlog_client *client, const char *request)
{
#ifdef SCM_CREDENTIALS
    struct ucred cred;
    struct iovec iov;
    char buf[CMSG_SPACE(sizeof cred)];
    struct msghdr msg;
    struct cmsghdr* cmsg;
    ssize_t nbytes;

    cred.pid = getpid();
    cred.uid = getuid();
    cred.gid = getgid();

    iov.iov_base = (void*) request;
    iov.iov_len = strlen(request);

    memset(&msg, 0, sizeof msg);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = buf;
    msg.msg_controllen = sizeof buf;

    cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_CREDENTIALS;
    cmsg->cmsg_len = CMSG_LEN(sizeof cred);
    memcpy(CMSG_DATA(cmsg), &cred, sizeof cred);
    msg.msg_controllen = cmsg->cmsg_len;

    nbytes = sendmsg(client->fd, &msg, 0);
#else /* !SCM_CREDENTIALS */
    ssize_t nbytes = send(client->fd, request, strlen(request), 0);
#endif /* !SCM_CREDENTIALS */
    if (nbytes > 0) {
        return nbytes == strlen(request) ? 0 : ENOBUFS;
    } else {
        return errno;
    }
}

/* Attempts to receive a response from the server socket that 'client' is
 * connected to.  Returns 0 if successful, otherwise a positive errno value.
 * If successful, sets '*reply' to the reply, which the caller must free,
 * otherwise to NULL. */
int
vlog_client_recv(struct vlog_client *client, char **reply)
{
    struct pollfd pfd;
    int nfds;
    char buffer[65536];
    ssize_t nbytes;

    *reply = NULL;

    pfd.fd = client->fd;
    pfd.events = POLLIN;
    nfds = poll(&pfd, 1, 1000);
    if (nfds == 0) {
        return ETIMEDOUT;
    } else if (nfds < 0) {
        return errno;
    }

    nbytes = read(client->fd, buffer, sizeof buffer - 1);
    if (nbytes < 0) {
        return errno;
    } else {
        buffer[nbytes] = '\0';
        *reply = xstrdup(buffer);
        return 0;
    }
}

/* Sends 'request' to the server socket and waits for a reply.  Returns 0 if
 * successful, otherwise to a positive errno value.  If successful, sets
 * '*reply' to the reply, which the caller must free, otherwise to NULL. */
int
vlog_client_transact(struct vlog_client *client,
                     const char *request, char **reply)
{
    int i;

    /* Retry up to 3 times. */
    for (i = 0; i < 3; ++i) {
        int error = vlog_client_send(client, request);
        if (error) {
            *reply = NULL;
            return error;
        }
        error = vlog_client_recv(client, reply);
        if (error != ETIMEDOUT) {
            return error;
        }
    }
    *reply = NULL;
    return ETIMEDOUT;
}

/* Returns the path of the server socket to which 'client' is connected.  The
 * caller must not modify or free the returned string. */
const char *
vlog_client_target(const struct vlog_client *client)
{
    return client->connect_path;
}

/* Helper functions. */

/* Stores in '*un' a sockaddr_un that refers to file 'name'.  Stores in
 * '*un_len' the size of the sockaddr_un. */
static void
make_sockaddr_un(const char *name, struct sockaddr_un* un, socklen_t *un_len)
{
    un->sun_family = AF_UNIX;
    strncpy(un->sun_path, name, sizeof un->sun_path);
    un->sun_path[sizeof un->sun_path - 1] = '\0';
    *un_len = (offsetof(struct sockaddr_un, sun_path)
                + strlen (un->sun_path) + 1);
}

/* Creates a Unix domain datagram socket that is bound to '*bind_path' (if
 * 'bind_path' is non-null) and connected to '*connect_path' (if 'connect_path'
 * is non-null).  If 'nonblock' is true, the socket is made non-blocking.  If
 * 'passcred' is true, the socket is configured to receive SCM_CREDENTIALS
 * control messages.
 *
 * Returns the socket's fd if successful, otherwise a negative errno value. */
static int
make_unix_socket(bool nonblock, bool passcred UNUSED,
                 const char *bind_path, const char *connect_path)
{
    int error;
    int fd;

    fd = socket(PF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) {
        return -errno;
    }

    if (nonblock) {
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags == -1) {
            goto error;
        }
        if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
            goto error;
        }
    }

    if (bind_path) {
        struct sockaddr_un un;
        socklen_t un_len;
        make_sockaddr_un(bind_path, &un, &un_len);
        if (unlink(un.sun_path) && errno != ENOENT) {
            fprintf(stderr, "unlinking \"%s\": %s\n",
                    un.sun_path, strerror(errno));
        }
        fatal_signal_add_file_to_unlink(bind_path);
        if (bind(fd, (struct sockaddr*) &un, un_len)
            || fchmod(fd, S_IRWXU)) {
            goto error;
        }
    }

    if (connect_path) {
        struct sockaddr_un un;
        socklen_t un_len;
        make_sockaddr_un(connect_path, &un, &un_len);
        if (connect(fd, (struct sockaddr*) &un, un_len)) {
            goto error;
        }
    }

#ifdef SCM_CREDENTIALS
    if (passcred) {
        int enable = 1;
        if (setsockopt(fd, SOL_SOCKET, SO_PASSCRED, &enable, sizeof(enable))) {
            goto error;
        }
    }
#endif

    return fd;

error:
    if (bind_path) {
        fatal_signal_remove_file_to_unlink(bind_path);
    }
    error = errno;
    close(fd);
    return -error;
}