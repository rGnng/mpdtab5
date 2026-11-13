#include "mpd_wire.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>

void mpd_wire_init(mpd_wire_t *w) { memset(w, 0, sizeof(*w)); w->fd = -1; }
void mpd_wire_close(mpd_wire_t *w) {
    if(w->fd >= 0) close(w->fd);
    w->fd = -1; w->pos = w->used = 0;
}
static bool fail(mpd_wire_t *w, const char *message) {
    snprintf(w->error, sizeof(w->error), "%s", message);
    mpd_wire_close(w); return false;
}
static int byte(mpd_wire_t *w) {
    if(w->pos == w->used) {
        int n;
        do { n = recv(w->fd, w->rx, sizeof(w->rx), 0); } while(n < 0 && errno == EINTR);
        if(n <= 0) { fail(w, n == 0 ? "MPD disconnected" : "MPD receive timeout or error"); return -1; }
        w->pos = 0; w->used = (unsigned)n;
    }
    return w->rx[w->pos++];
}
static bool line(mpd_wire_t *w, char *out, size_t cap) {
    size_t n = 0; int c;
    while((c = byte(w)) >= 0) {
        if(c == '\n') { out[n] = 0; return true; }
        if(c == 0 || n + 1 >= cap) return fail(w, "MPD response line too long or invalid");
        if(c != '\r') out[n++] = (char)c;
    }
    return false;
}
bool mpd_wire_attach(mpd_wire_t *w, int fd, unsigned timeout_ms) {
    mpd_wire_close(w); w->fd = fd; w->error[0] = 0;
    struct timeval timeout = { .tv_sec = timeout_ms / 1000, .tv_usec = (timeout_ms % 1000) * 1000 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    char greeting[128];
    if(!line(w, greeting, sizeof(greeting))) return false;
    return !strncmp(greeting, "OK MPD ", 7) || fail(w, "Server is not MPD");
}
bool mpd_wire_connect(mpd_wire_t *w, const char *host, uint16_t port, unsigned timeout_ms) {
    mpd_wire_close(w);
    char service[8]; snprintf(service, sizeof(service), "%u", port);
    struct addrinfo hints = { .ai_socktype = SOCK_STREAM, .ai_family = AF_UNSPEC }, *addresses = NULL;
    if(getaddrinfo(host, service, &hints, &addresses)) return fail(w, "MPD hostname lookup failed");
    int fd = -1;
    for(struct addrinfo *a = addresses; a; a = a->ai_next) {
        fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if(fd < 0) continue;
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        int result = connect(fd, a->ai_addr, a->ai_addrlen);
        if(result < 0 && errno == EINPROGRESS) {
            fd_set writes; FD_ZERO(&writes); FD_SET(fd, &writes);
            struct timeval timeout = { .tv_sec = timeout_ms/1000, .tv_usec = (timeout_ms%1000)*1000 };
            int error = 0; socklen_t len = sizeof(error);
            result = select(fd+1, NULL, &writes, NULL, &timeout);
            if(result > 0 && getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len) == 0 && error == 0) result = 0;
            else result = -1;
        }
        fcntl(fd, F_SETFL, flags);
        if(result == 0) break;
        close(fd); fd = -1;
    }
    freeaddrinfo(addresses);
    if(fd < 0) return fail(w, "MPD connection failed");
    return mpd_wire_attach(w, fd, timeout_ms);
}
static bool send_all(mpd_wire_t *w, const char *p, size_t len) {
    while(len) {
#ifdef MSG_NOSIGNAL
        int n = send(w->fd, p, len, MSG_NOSIGNAL);
#else
        int n = send(w->fd, p, len, 0);
#endif
        if(n < 0 && errno == EINTR) continue;
        if(n <= 0) return fail(w, "MPD send timeout or error");
        p += n; len -= n;
    }
    return true;
}
bool mpd_wire_command(mpd_wire_t *w, const char *command, mpd_field_cb field,
                      mpd_binary_cb binary, void *ctx) {
    w->error[0] = 0;
    if(w->fd < 0) return fail(w, "MPD is disconnected");
    if(!send_all(w, command, strlen(command)) || !send_all(w, "\n", 1)) return false;
    char record[4096];
    while(line(w, record, sizeof(record))) {
        if(!strcmp(record, "OK")) return true;
        if(!strncmp(record, "ACK ", 4)) {
            snprintf(w->error, sizeof(w->error), "%.191s", record); return false;
        }
        if(!strcmp(record, "list_OK")) continue;
        char *sep = strstr(record, ": ");
        if(!sep) return fail(w, "Malformed MPD response");
        *sep = 0; const char *value = sep + 2;
        if(!strcmp(record, "binary")) {
            char *end; unsigned long size = strtoul(value, &end, 10);
            if(!value[0] || *end || value[0] == '-' || size > 4*1024*1024)
                return fail(w, "Invalid MPD binary length");
            unsigned char chunk[1024];
            while(size) {
                size_t n = size < sizeof(chunk) ? size : sizeof(chunk);
                for(size_t i=0; i<n; i++) { int c=byte(w); if(c<0) return false; chunk[i]=(unsigned char)c; }
                if(binary && !binary(chunk, n, ctx)) return fail(w, "MPD binary consumer failed");
                size -= n;
            }
            if(byte(w) != '\n') return fail(w, "Malformed MPD binary terminator");
        }
        else if(field && !field(record, value, ctx)) return fail(w, "MPD response exceeds controller capacity");
    }
    return false;
}
bool mpd_quote(char *out, size_t cap, const char *value) {
    if(cap < 3) return false;
    size_t n=0; out[n++]='"';
    for(const unsigned char *p=(const unsigned char *)value; *p; p++) {
        if(*p < 32 || *p == 127) return false;
        if(*p == '"' || *p == '\\') { if(n+3>=cap) return false; out[n++]='\\'; }
        if(n+2>=cap) return false;
        out[n++]=(char)*p;
    }
    out[n++]='"'; out[n]=0; return true;
}
uint64_t mpd_id(const char *value) {
    uint64_t id=1469598103934665603ULL;
    for(const unsigned char *p=(const unsigned char *)value; *p; p++) { id^=*p; id*=1099511628211ULL; }
    return id ? id : 1;
}
