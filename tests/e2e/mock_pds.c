/*
 * The mock PDS behind `make e2e`. Each connection is one request and is
 * answered with Connection: close, so the harness needs no HTTP library and
 * the test can read a route's last request body after a job completes.
 */

#include "mock_pds.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#define E2E_PDS_ROUTES 16
#define E2E_PDS_NSID_MAX 80
#define E2E_PDS_BODY_MAX 2048
#define E2E_PDS_REQUEST_MAX 4096

typedef struct {
    char nsid[E2E_PDS_NSID_MAX];
    int status;
    char body[E2E_PDS_BODY_MAX];
    char request[1024];
    char path[256];
    int hits;
} e2e_route;

struct e2e_pds {
    int listener;
    int port;
    pthread_t thread;
    pthread_mutex_t lock;
    volatile bool stop;
    e2e_route routes[E2E_PDS_ROUTES];
    int route_count;
};

static e2e_route *
find_route_locked(e2e_pds *pds, const char *nsid)
{
    for (int i = 0; i < pds->route_count; i++) {
        if (strcmp(pds->routes[i].nsid, nsid) == 0) {
            return &pds->routes[i];
        }
    }
    return NULL;
}

static void
send_all(int fd, const char *data, size_t len)
{
    while (len > 0) {
        const ssize_t sent = send(fd, data, len, 0);

        if (sent <= 0) {
            return;
        }
        data += sent;
        len -= (size_t) sent;
    }
}

/* Read until the header terminator, then until Content-Length bytes of body
 * have arrived. Returns the body length, or -1 when the connection is not a
 * complete request. */
static ssize_t
read_request(int fd, char *buf, size_t cap, size_t *header_len)
{
    size_t len = 0;
    char *end;

    while (len + 1 < cap) {
        const ssize_t n = recv(fd, buf + len, cap - len - 1, 0);

        if (n <= 0) {
            return -1;
        }
        len += (size_t) n;
        buf[len] = '\0';
        end = strstr(buf, "\r\n\r\n");
        if (end) {
            break;
        }
    }
    if (!end) {
        return -1;
    }
    *header_len = (size_t) (end + 4 - buf);

    size_t content_length = 0;
    const char *p = buf;
    while (p < end) {
        const char *line_end = strstr(p, "\r\n");
        const size_t line_len = line_end ? (size_t) (line_end - p) : strlen(p);

        if (line_len > 15 && strncasecmp(p, "content-length:", 15) == 0) {
            content_length = (size_t) strtoul(p + 15, NULL, 10);
            break;
        }
        if (!line_end) {
            break;
        }
        p = line_end + 2;
    }

    size_t have = len - *header_len;
    while (have < content_length) {
        const ssize_t n = recv(fd, buf + len, cap - len - 1, 0);

        if (n <= 0) {
            return -1;
        }
        len += (size_t) n;
        have += (size_t) n;
        buf[len] = '\0';
    }
    return (ssize_t) have;
}

static void
handle_connection(e2e_pds *pds, int fd)
{
    static const char not_found[] =
        "{\"error\":\"NotFoundError\",\"message\":\"unknown NSID\"}";
    char buf[E2E_PDS_REQUEST_MAX];
    size_t header_len = 0;
    char method[8] = "";
    char path[256] = "";
    char request_path[256] = "";
    const char *body = "";
    const char *status_text = "OK";
    const char *answer = not_found;
    int status = 404;

    const ssize_t body_len = read_request(fd, buf, sizeof buf, &header_len);
    if (body_len < 0 || sscanf(buf, "%7s %255s", method, path) != 2) {
        return;
    }
    body = buf + header_len;
    /* Keep the whole target before the query is split off: a GET's cursor is in
     * the query string, and the test reads it back to prove what was asked. */
    snprintf(request_path, sizeof request_path, "%s", path);
    /* resolveHandle and similar are GETs with a query string; the route is
     * keyed by the NSID alone. */
    char *query = strchr(path, '?');
    if (query) {
        *query = '\0';
    }

    if (strncmp(path, "/xrpc/", 6) == 0) {
        const char *nsid = path + 6;

        pthread_mutex_lock(&pds->lock);
        e2e_route *route = find_route_locked(pds, nsid);
        if (route) {
            route->hits++;
            snprintf(route->request, sizeof route->request, "%s", body);
            snprintf(route->path, sizeof route->path, "%s", request_path);
            status = route->status;
            /* Copy the answer before unlocking: e2e_pds_route may replace it
             * while the response is being written. */
            char reply[E2E_PDS_BODY_MAX];
            snprintf(reply, sizeof reply, "%s", route->body);
            pthread_mutex_unlock(&pds->lock);
            if (status == 200) {
                status_text = "OK";
            } else if (status == 500) {
                status_text = "Internal Server Error";
            } else {
                status_text = "Error";
            }
            char header[256];
            const int header_size = snprintf(
                header, sizeof header,
                "HTTP/1.1 %d %s\r\nContent-Type: application/json\r\n"
                "Content-Length: %zu\r\nConnection: close\r\n\r\n",
                status, status_text, strlen(reply));
            send_all(fd, header, (size_t) header_size);
            send_all(fd, reply, strlen(reply));
            return;
        }
        pthread_mutex_unlock(&pds->lock);
    }

    char header[256];
    const int header_size = snprintf(
        header, sizeof header,
        "HTTP/1.1 %d %s\r\nContent-Type: application/json\r\n"
        "Content-Length: %zu\r\nConnection: close\r\n\r\n",
        status, status_text, strlen(answer));
    send_all(fd, header, (size_t) header_size);
    send_all(fd, answer, strlen(answer));
}

static void *
server_main(void *arg)
{
    e2e_pds *pds = (e2e_pds *) arg;

    while (!pds->stop) {
        struct pollfd pfd = {.fd = pds->listener, .events = POLLIN};

        if (poll(&pfd, 1, 100) <= 0) {
            continue;
        }
        const int fd = accept(pds->listener, NULL, NULL);
        if (fd < 0) {
            continue;
        }
        handle_connection(pds, fd);
        close(fd);
    }
    return NULL;
}

bool
e2e_pds_start(e2e_pds **out, int *out_port)
{
    e2e_pds *pds;
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof addr;
    int one = 1;

    if (!out || !out_port) {
        return false;
    }
    pds = (e2e_pds *) calloc(1, sizeof(*pds));
    if (!pds) {
        return false;
    }
    pthread_mutex_init(&pds->lock, NULL);

    pds->listener = socket(AF_INET, SOCK_STREAM, 0);
    if (pds->listener < 0) {
        free(pds);
        return false;
    }
    setsockopt(pds->listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(pds->listener, (struct sockaddr *) &addr, sizeof addr) != 0 ||
        listen(pds->listener, 8) != 0 ||
        getsockname(pds->listener, (struct sockaddr *) &addr, &addr_len) != 0) {
        close(pds->listener);
        pthread_mutex_destroy(&pds->lock);
        free(pds);
        return false;
    }
    pds->port = (int) ntohs(addr.sin_port);
    if (pthread_create(&pds->thread, NULL, server_main, pds) != 0) {
        close(pds->listener);
        pthread_mutex_destroy(&pds->lock);
        free(pds);
        return false;
    }

    *out = pds;
    *out_port = pds->port;
    return true;
}

void
e2e_pds_route(e2e_pds *pds, const char *nsid, int status, const char *body)
{
    e2e_route *route;

    if (!pds || !nsid || !body) {
        return;
    }
    pthread_mutex_lock(&pds->lock);
    route = find_route_locked(pds, nsid);
    if (!route && pds->route_count < E2E_PDS_ROUTES) {
        route = &pds->routes[pds->route_count++];
        snprintf(route->nsid, sizeof route->nsid, "%s", nsid);
    }
    if (route) {
        route->status = status;
        snprintf(route->body, sizeof route->body, "%s", body);
    }
    pthread_mutex_unlock(&pds->lock);
}

int
e2e_pds_hits(e2e_pds *pds, const char *nsid)
{
    int hits = 0;

    pthread_mutex_lock(&pds->lock);
    e2e_route *route = find_route_locked(pds, nsid);
    if (route) {
        hits = route->hits;
    }
    pthread_mutex_unlock(&pds->lock);
    return hits;
}

bool
e2e_pds_request_contains(e2e_pds *pds, const char *nsid, const char *needle)
{
    bool found = false;

    pthread_mutex_lock(&pds->lock);
    e2e_route *route = find_route_locked(pds, nsid);
    if (route && needle) {
        found = strstr(route->request, needle) != NULL;
    }
    pthread_mutex_unlock(&pds->lock);
    return found;
}

bool
e2e_pds_request_path_contains(e2e_pds *pds, const char *nsid, const char *needle)
{
    bool found = false;

    pthread_mutex_lock(&pds->lock);
    e2e_route *route = find_route_locked(pds, nsid);
    if (route && needle) {
        found = strstr(route->path, needle) != NULL;
    }
    pthread_mutex_unlock(&pds->lock);
    return found;
}

void
e2e_pds_free(e2e_pds *pds)
{
    if (!pds) {
        return;
    }
    pds->stop = true;
    pthread_join(pds->thread, NULL);
    close(pds->listener);
    pthread_mutex_destroy(&pds->lock);
    free(pds);
}
