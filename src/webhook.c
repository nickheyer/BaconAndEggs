#include "webhook.h"
#include "config.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"
#include "lwip/dns.h"
#include "lwip/ip_addr.h"

// Per-request state, heap allocated so concurrent webhooks don't stomp
typedef struct {
    char host[HOST_LEN];
    char path[URL_LEN];
    uint16_t port;
    char body[256];
    char request[512];
} webhook_req_t;

static bool parse_url(const char *url, webhook_req_t *req) {
    req->port = 80;
    memset(req->host, 0, sizeof(req->host));
    memset(req->path, 0, sizeof(req->path));

    const char *p = url;
    if (strncmp(p, "https://", 8) == 0) {
        printf("Webhook: HTTPS not supported (no TLS on Pico W)\n");
        return false;
    } else if (strncmp(p, "http://", 7) == 0) {
        p += 7;
    }

    const char *slash = strchr(p, '/');
    const char *colon = strchr(p, ':');

    if (colon && (!slash || colon < slash)) {
        int host_len = colon - p;
        if (host_len >= HOST_LEN) host_len = HOST_LEN - 1;
        memcpy(req->host, p, host_len);
        req->port = (uint16_t)atoi(colon + 1);
    } else if (slash) {
        int host_len = slash - p;
        if (host_len >= HOST_LEN) host_len = HOST_LEN - 1;
        memcpy(req->host, p, host_len);
    } else {
        strncpy(req->host, p, HOST_LEN - 1);
    }

    if (slash) {
        strncpy(req->path, slash, URL_LEN - 1);
    } else {
        strcpy(req->path, "/");
    }

    return strlen(req->host) > 0;
}

static void webhook_err_cb(void *arg, err_t err) {
    (void)err;
    webhook_req_t *req = (webhook_req_t *)arg;
    if (req) {
        printf("Webhook: connection error\n");
        free(req);
    }
}

static err_t webhook_sent_cb(void *arg, struct tcp_pcb *tpcb, u16_t len) {
    (void)len;
    webhook_req_t *req = (webhook_req_t *)arg;
    tcp_arg(tpcb, NULL);
    tcp_close(tpcb);
    free(req);
    return ERR_OK;
}

static err_t webhook_connected_cb(void *arg, struct tcp_pcb *tpcb, err_t err) {
    webhook_req_t *req = (webhook_req_t *)arg;
    if (err != ERR_OK || !req) {
        if (req) free(req);
        tcp_abort(tpcb);
        return ERR_ABRT;
    }

    int rlen = snprintf(req->request, sizeof(req->request),
        "POST %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        req->path, req->host,
        (int)strlen(req->body), req->body);

    tcp_sent(tpcb, webhook_sent_cb);
    tcp_write(tpcb, req->request, rlen, TCP_WRITE_FLAG_COPY);
    tcp_output(tpcb);

    return ERR_OK;
}

static void webhook_do_connect(webhook_req_t *req, const ip_addr_t *ipaddr) {
    struct tcp_pcb *pcb = tcp_new();
    if (!pcb) {
        printf("Webhook: failed to create PCB\n");
        free(req);
        return;
    }

    tcp_arg(pcb, req);
    tcp_err(pcb, webhook_err_cb);
    err_t err = tcp_connect(pcb, ipaddr, req->port, webhook_connected_cb);
    if (err != ERR_OK) {
        printf("Webhook: connect failed (%d)\n", err);
        tcp_close(pcb);
        free(req);
    }
}

static void webhook_dns_cb(const char *name, const ip_addr_t *ipaddr, void *arg) {
    (void)name;
    webhook_req_t *req = (webhook_req_t *)arg;

    if (!ipaddr) {
        printf("Webhook: DNS failed for %s\n", req->host);
        free(req);
        return;
    }

    webhook_do_connect(req, ipaddr);
}

static void webhook_send(webhook_req_t *req) {
    ip_addr_t addr;
    if (ipaddr_aton(req->host, &addr)) {
        webhook_do_connect(req, &addr);
        return;
    }

    err_t err = dns_gethostbyname(req->host, &addr, webhook_dns_cb, req);
    if (err == ERR_OK) {
        webhook_do_connect(req, &addr);
    } else if (err != ERR_INPROGRESS) {
        printf("Webhook: DNS error for %s\n", req->host);
        free(req);
    }
}

void webhook_notify(const char *event, const char *server_name, const char *detail) {
    if (!config.webhook.enabled || strlen(config.webhook.url) == 0)
        return;

    webhook_req_t *req = calloc(1, sizeof(webhook_req_t));
    if (!req) {
        printf("Webhook: out of memory\n");
        return;
    }

    if (!parse_url(config.webhook.url, req)) {
        free(req);
        return;
    }

    uint32_t timestamp = to_ms_since_boot(get_absolute_time()) / 1000;
    snprintf(req->body, sizeof(req->body),
             "{\"event\":\"%s\",\"server\":\"%s\",\"detail\":\"%s\",\"timestamp\":%u}",
             event, server_name, detail, timestamp);

    printf("Webhook: %s -> %s\n", event, config.webhook.url);
    webhook_send(req);
}
