#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* One owner per connection. Callbacks consume records while they are streamed;
 * no complete database or binary response is buffered by the protocol layer. */
typedef struct {
    int fd;
    unsigned pos, used;
    unsigned char rx[4096];
    char error[192];
} mpd_wire_t;
typedef bool (*mpd_field_cb)(const char *key, const char *value, void *ctx);
typedef bool (*mpd_binary_cb)(const void *data, size_t size, void *ctx);
void mpd_wire_init(mpd_wire_t *w);
void mpd_wire_close(mpd_wire_t *w);
bool mpd_wire_connect(mpd_wire_t *w, const char *host, uint16_t port, unsigned timeout_ms);
/* Attaches a connected socket and validates the greeting (also useful in tests). */
bool mpd_wire_attach(mpd_wire_t *w, int fd, unsigned timeout_ms);
/* Arguments must be escaped using mpd_quote. ACK leaves the stream usable;
 * I/O, framing, callback failures close it so partial replies cannot leak. */
bool mpd_wire_command(mpd_wire_t *w, const char *command, mpd_field_cb field,
                      mpd_binary_cb binary, void *ctx);
/* Split request/reply for a dedicated idle subscription connection. */
bool mpd_wire_send(mpd_wire_t *w, const char *command);
bool mpd_wire_response(mpd_wire_t *w, mpd_field_cb field, mpd_binary_cb binary, void *ctx);
int mpd_wire_ready(mpd_wire_t *w); /* 1 readable, 0 pending, -1 disconnected */
bool mpd_quote(char *out, size_t cap, const char *value);
uint64_t mpd_id(const char *value);
uint32_t mpd_timestamp(const char *value); /* ISO 8601 UTC/offset, 0 unknown */
