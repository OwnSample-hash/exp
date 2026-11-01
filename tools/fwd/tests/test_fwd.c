/*
 * test_fwd.c - self contained tests; they need no root and only use loopback.
 * Route / TUN / sysctl-write paths need privileges and are exercised here
 * only as far as their error reporting (no system state is modified).
 */
#include "fwd.h"
#include "fwd_sys.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <linux/if_tun.h>
#include <net/if.h>

static int g_fail, g_ok;

#define CHECK(cond)                                                           \
    do {                                                                      \
        if (cond) {                                                           \
            g_ok++;                                                           \
        } else {                                                              \
            g_fail++;                                                         \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                     \
    } while (0)

#define CHECK_STR(got, want)                                                  \
    do {                                                                      \
        if (strcmp((got), (want)) == 0) {                                     \
            g_ok++;                                                           \
        } else {                                                              \
            g_fail++;                                                         \
            fprintf(stderr, "FAIL %s:%d: got '%s' want '%s'\n", __FILE__,     \
                    __LINE__, (got), (want));                                 \
        }                                                                     \
    } while (0)

/* ---------------------------------------------------------------- */
/* helpers                                                          */
/* ---------------------------------------------------------------- */
static int make_sa(int family, const char *ip, unsigned port,
                   struct sockaddr_storage *ss, socklen_t *sl)
{
    memset(ss, 0, sizeof *ss);
    if (family == AF_INET) {
        struct sockaddr_in *s = (struct sockaddr_in *)ss;
        s->sin_family = AF_INET;
        s->sin_port = htons((uint16_t)port);
        *sl = sizeof *s;
        return inet_pton(AF_INET, ip, &s->sin_addr) == 1 ? 0 : -1;
    } else {
        struct sockaddr_in6 *s = (struct sockaddr_in6 *)ss;
        s->sin6_family = AF_INET6;
        s->sin6_port = htons((uint16_t)port);
        *sl = sizeof *s;
        return inet_pton(AF_INET6, ip, &s->sin6_addr) == 1 ? 0 : -1;
    }
}

/* Bind a socket to port 0 to learn a free port, then close it again. */
static unsigned free_port(int family, int type, const char *ip)
{
    struct sockaddr_storage ss;
    socklen_t sl;
    int fd = socket(family, type, 0);
    unsigned port = 0;

    if (fd < 0 || make_sa(family, ip, 0, &ss, &sl) < 0)
        return 0;
    if (bind(fd, (struct sockaddr *)&ss, sl) == 0) {
        sl = sizeof ss;
        getsockname(fd, (struct sockaddr *)&ss, &sl);
        port = ntohs(family == AF_INET ? ((struct sockaddr_in *)&ss)->sin_port
                                       : ((struct sockaddr_in6 *)&ss)->sin6_port);
    }
    close(fd);
    return port;
}

static int bound_socket(int family, int type, const char *ip, unsigned port,
                        int nonblock)
{
    struct sockaddr_storage ss;
    socklen_t sl;
    int fd = socket(family, type, 0);
    if (fd < 0 || make_sa(family, ip, port, &ss, &sl) < 0)
        return -1;
    if (bind(fd, (struct sockaddr *)&ss, sl) < 0 ||
        (type == SOCK_STREAM && listen(fd, 16) < 0)) {
        close(fd);
        return -1;
    }
    if (nonblock)
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    return fd;
}

/* encode human strings -> command line -> execute -> response */
static int run(fwd_ctx *ctx, int num, int argc, const char *const *argv,
               char *resp, size_t rl)
{
    char line[FWD_MAX_CMD + 1];
    int n = fwd_cmd_encode_strs(num, argv, argc, line, sizeof line);
    if (n < 0) {
        snprintf(resp, rl, "ENCODE:%d", n);
        return n;
    }
    return fwd_execute(ctx, line, resp, rl);
}

#define RUN(ctx, num, resp, ...)                                              \
    run((ctx), (num),                                                         \
        (int)(sizeof((const char *[]){__VA_ARGS__}) / sizeof(const char *)),  \
        (const char *[]){__VA_ARGS__}, (resp), sizeof(resp))

static int status_of(const char *resp)
{
    fwd_response r;
    if (fwd_response_parse(resp, &r) != FWD_OK)
        return -1;
    return r.status;
}

/* ---------------------------------------------------------------- */
/* addresses and codec                                              */
/* ---------------------------------------------------------------- */
static int addr_same(const fwd_addr *a, const fwd_addr *b)
{
    return a->family == b->family && memcmp(a->bytes, b->bytes, 16) == 0;
}

static void test_addr(void)
{
    fwd_addr a, b;
    char buf[64];
    uint32_t v;

    CHECK(fwd_addr_from_string(&a, "192.168.1.1") == FWD_OK);
    CHECK(fwd_addr_encode(&a, buf, sizeof buf) > 0);
    CHECK_STR(buf, "3232235777");
    CHECK(fwd_addr_decode(&b, buf) == FWD_OK && addr_same(&a, &b));
    CHECK(fwd_addr_to_u32(&a, &v) == FWD_OK && v == 0xC0A80101u);

    CHECK(fwd_addr_from_string(&a, "::1") == FWD_OK);
    CHECK(fwd_addr_encode(&a, buf, sizeof buf) == 34);
    CHECK_STR(buf, "0x00000000000000000000000000000001");
    CHECK(fwd_addr_decode(&b, buf) == FWD_OK && addr_same(&a, &b));

    CHECK(fwd_addr_from_string(&a, "2001:db8::dead:beef") == FWD_OK);
    fwd_addr_encode(&a, buf, sizeof buf);
    CHECK_STR(buf, "0x20010db80000000000000000deadbeef");
    CHECK(fwd_addr_decode(&b, buf) == FWD_OK && addr_same(&a, &b));
    fwd_addr_to_string(&b, buf, sizeof buf);
    CHECK_STR(buf, "2001:db8::dead:beef");

    CHECK(fwd_addr_decode(&b, "0xC0A80101") == FWD_OK && b.family == FWD_V4);
    CHECK(fwd_addr_decode(&b, "4294967296") == FWD_E_RANGE);
    CHECK(fwd_addr_decode(&b, "1.2.3.4") == FWD_E_PARSE);
    CHECK(fwd_addr_decode(&b, "0x123456789") == FWD_E_PARSE);
    CHECK(fwd_addr_decode(&b, "") == FWD_E_PARSE);
}

static void test_codec(void)
{
    char out[FWD_MAX_CMD + 1];
    fwd_cmd c, d;
    int n;

    {
        const char *argv[] = {"tcp", "127.0.0.1", "9000", "127.0.0.1", "9001"};
        n = fwd_cmd_encode_strs(FWD_CMD_FWD_ADD, argv, 5, out, sizeof out);
        CHECK(n > 0);
        CHECK_STR(out, "1:1:2130706433:9000:2130706433:9001");
        CHECK(fwd_cmd_decode(out, &c) == FWD_OK && c.num == FWD_CMD_FWD_ADD);
        CHECK(c.u.fwd.proto == FWD_PROTO_TCP && c.u.fwd.lport == 9000 &&
              c.u.fwd.rport == 9001 && c.u.fwd.laddr.family == FWD_V4);
        /* struct -> string gives the same encoding */
        CHECK(fwd_cmd_encode(&c, out, sizeof out) > 0);
        CHECK_STR(out, "1:1:2130706433:9000:2130706433:9001");
    }
    {
        const char *argv[] = {"udp", "::1", "53", "2001:db8::1", "5353"};
        n = fwd_cmd_encode_strs(FWD_CMD_FWD_ADD, argv, 5, out, sizeof out);
        CHECK(n > 0);
        CHECK_STR(out, "1:2:0x00000000000000000000000000000001:53:"
                       "0x20010db8000000000000000000000001:5353");
        CHECK(fwd_cmd_decode(out, &c) == FWD_OK &&
              c.u.fwd.laddr.family == FWD_V6);
    }
    {
        const char *argv[] = {"3", "tcp", "10.0.0.1", "80", "10.0.0.2", "8080"};
        n = fwd_cmd_encode_strs(FWD_CMD_FWD_MOD, argv, 6, out, sizeof out);
        CHECK(n > 0 && fwd_cmd_decode(out, &c) == FWD_OK && c.id == 3);
    }
    {
        const char *argv[] = {"192.0.2.0", "24", "192.0.2.1", "", "100", "0"};
        n = fwd_cmd_encode_strs(FWD_CMD_ROUTE_ADD, argv, 6, out, sizeof out);
        CHECK(n > 0);
        CHECK(fwd_cmd_decode(out, &c) == FWD_OK);
        CHECK(c.u.route.prefix == 24 && c.u.route.metric == 100 &&
              c.u.route.ifname[0] == '\0');
        CHECK(fwd_cmd_encode(&c, d.u.sysctl.key, 0) == FWD_E_BUF);
        CHECK(fwd_cmd_encode(&c, out, sizeof out) == n);
    }
    {
        const char *argv[] = {"tap", "tap0", "10.0.0.1", "4000", "10.0.0.2",
                              "4000", "172.16.0.1", "24", "1400"};
        n = fwd_cmd_encode_strs(FWD_CMD_TUN_ADD, argv, 9, out, sizeof out);
        CHECK(n > 0 && fwd_cmd_decode(out, &c) == FWD_OK);
        CHECK(c.u.tun.type == FWD_TUN_TAP && c.u.tun.mtu == 1400 &&
              strcmp(c.u.tun.name, "tap0") == 0 && c.u.tun.prefix == 24);
        CHECK(fwd_cmd_encode(&c, out, sizeof out) == n);
    }
    {
        const char *argv[] = {"net.ipv4.ip_forward", "1"};
        n = fwd_cmd_encode_strs(FWD_CMD_SYSCTL_SET, argv, 2, out, sizeof out);
        CHECK_STR(out, "10:net.ipv4.ip_forward:1");
    }
    CHECK(fwd_cmd_decode("12", &c) == FWD_OK && c.id == 0);
    CHECK(fwd_cmd_decode("12:40", &c) == FWD_OK && c.id == 40);

    /* rejects */
    CHECK(fwd_cmd_decode("", &c) == FWD_E_PARSE);
    CHECK(fwd_cmd_decode("abc", &c) == FWD_E_PARSE);
    CHECK(fwd_cmd_decode("99", &c) == FWD_E_PARSE);
    CHECK(fwd_cmd_decode("1:1:2130706433:9000", &c) == FWD_E_PARSE);
    CHECK(fwd_cmd_decode("1:1:2130706433:9000:2130706433:9001:5", &c) ==
          FWD_E_PARSE);
    CHECK(fwd_cmd_decode("1:1:2130706433:70000:2130706433:9001", &c) ==
          FWD_E_RANGE);
    CHECK(fwd_cmd_decode("1:3:2130706433:9000:2130706433:9001", &c) ==
          FWD_E_RANGE);
    CHECK(fwd_cmd_decode("1:1:127.0.0.1:9000:2130706433:9001", &c) ==
          FWD_E_PARSE);
    CHECK(fwd_cmd_decode("7:0:200:0::0:0", &c) != FWD_OK);
    {
        const char *bad[] = {"tcp", "not-an-ip", "1", "127.0.0.1", "2"};
        CHECK(fwd_cmd_encode_strs(FWD_CMD_FWD_ADD, bad, 5, out, sizeof out) <
              0);
        CHECK(fwd_cmd_encode_strs(FWD_CMD_FWD_ADD, bad, 2, out, sizeof out) ==
              FWD_E_PARSE);
    }
    CHECK(fwd_cmd_by_name("fwd-add") == FWD_CMD_FWD_ADD);
    CHECK(fwd_cmd_by_name("nonsense") == -1);
}

/* ---------------------------------------------------------------- */
/* TCP forwarding through the full command path                     */
/* ---------------------------------------------------------------- */
#define DATA_LEN 300000

static void test_tcp(fwd_ctx *ctx, int family, const char *ip)
{
    struct sockaddr_storage ss;
    socklen_t sl;
    char resp[FWD_MAX_RESP + 1], fports[16], eports[16];
    unsigned fport = free_port(family, SOCK_STREAM, ip), eport;
    int lst, echo = -1, cli, i, cmd_ok = 1;
    unsigned char *data = malloc(DATA_LEN), *got = malloc(DATA_LEN);
    unsigned char pend[65536];
    size_t pend_len = 0, pend_off = 0, sent = 0, recvd = 0;
    int shut = 0, eof = 0;

    if (!fport) {
        fprintf(stderr, "skip tcp %s (no such address)\n", ip);
        goto out;
    }
    lst = bound_socket(family, SOCK_STREAM, ip, 0, 1);
    sl = sizeof ss;
    getsockname(lst, (struct sockaddr *)&ss, &sl);
    eport = ntohs(family == AF_INET ? ((struct sockaddr_in *)&ss)->sin_port
                                    : ((struct sockaddr_in6 *)&ss)->sin6_port);
    snprintf(fports, sizeof fports, "%u", fport);
    snprintf(eports, sizeof eports, "%u", eport);

    CHECK(RUN(ctx, FWD_CMD_FWD_ADD, resp, "tcp", ip, fports, ip, eports) > 0);
    {
        fwd_response r;
        CHECK(fwd_response_parse(resp, &r) == FWD_OK && r.status == 0);
        CHECK(atoi(r.payload) > 0);
    }

    for (i = 0; i < DATA_LEN; i++)
        data[i] = (unsigned char)(i * 31 + i / 251);

    make_sa(family, ip, fport, &ss, &sl);
    cli = socket(family, SOCK_STREAM, 0);
    CHECK(connect(cli, (struct sockaddr *)&ss, sl) == 0);
    fcntl(cli, F_SETFL, fcntl(cli, F_GETFL) | O_NONBLOCK);

    /* pump everything in this one thread */
    for (i = 0; i < 20000 && !eof; i++) {
        struct pollfd p;
        fwd_run(ctx, 1);

        if (echo < 0)
            echo = accept(lst, NULL, NULL);
        if (echo >= 0) {
            if (pend_len == pend_off) {
                ssize_t n = recv(echo, pend, sizeof pend, MSG_DONTWAIT);
                if (n > 0) {
                    pend_len = (size_t)n;
                    pend_off = 0;
                } else if (n == 0) {
                    close(echo); /* client side finished */
                    echo = -2;
                }
            }
            if (echo >= 0 && pend_off < pend_len) {
                ssize_t n = send(echo, pend + pend_off, pend_len - pend_off,
                                 MSG_DONTWAIT | MSG_NOSIGNAL);
                if (n > 0)
                    pend_off += (size_t)n;
            }
        }
        if (sent < DATA_LEN) {
            ssize_t n = send(cli, data + sent, DATA_LEN - sent,
                             MSG_DONTWAIT | MSG_NOSIGNAL);
            if (n > 0)
                sent += (size_t)n;
        } else if (!shut) {
            shutdown(cli, SHUT_WR);
            shut = 1;
        }
        p.fd = cli;
        p.events = POLLIN;
        if (poll(&p, 1, 0) > 0) {
            ssize_t n = recv(cli, got + recvd, DATA_LEN - recvd, MSG_DONTWAIT);
            if (n > 0)
                recvd += (size_t)n;
            else if (n == 0)
                eof = 1;
        }
    }
    CHECK(sent == DATA_LEN);
    CHECK(recvd == DATA_LEN && memcmp(data, got, DATA_LEN) == 0);
    CHECK(eof); /* half close travelled both ways */
    close(cli);
    if (echo >= 0)
        close(echo);

    /* listing shows our forward */
    CHECK(RUN(ctx, FWD_CMD_LIST_FWD, resp, "0") > 0);
    {
        char want[80];
        char la[48];
        fwd_addr a;
        fwd_addr_from_string(&a, ip);
        fwd_addr_encode(&a, la, sizeof la);
        snprintf(want, sizeof want, ":%s:%u:", la, fport);
        CHECK(strstr(resp, want) != NULL);
        CHECK(strncmp(resp, "12:0:", 5) == 0);
    }
    close(lst);
out:
    (void)cmd_ok;
    free(data);
    free(got);
}

/* ---------------------------------------------------------------- */
static void test_udp(fwd_ctx *ctx)
{
    char resp[FWD_MAX_RESP + 1], fports[16], eports[16];
    unsigned fport = free_port(AF_INET, SOCK_DGRAM, "127.0.0.1");
    unsigned eport = free_port(AF_INET, SOCK_DGRAM, "127.0.0.1");
    int echo = bound_socket(AF_INET, SOCK_DGRAM, "127.0.0.1", eport, 1);
    int cli = socket(AF_INET, SOCK_DGRAM, 0), i, got = 0;
    struct sockaddr_storage ss;
    socklen_t sl;
    char buf[64];

    snprintf(fports, sizeof fports, "%u", fport);
    snprintf(eports, sizeof eports, "%u", eport);
    CHECK(echo >= 0 && fport && eport);
    CHECK(RUN(ctx, FWD_CMD_FWD_ADD, resp, "udp", "127.0.0.1", fports,
              "127.0.0.1", eports) > 0 &&
          status_of(resp) == 0);

    make_sa(AF_INET, "127.0.0.1", fport, &ss, &sl);
    sendto(cli, "hello udp", 9, 0, (struct sockaddr *)&ss, sl);
    for (i = 0; i < 500 && !got; i++) {
        struct sockaddr_storage from;
        socklen_t fl = sizeof from;
        struct pollfd p = {cli, POLLIN, 0};
        ssize_t n;
        fwd_run(ctx, 2);
        n = recvfrom(echo, buf, sizeof buf, MSG_DONTWAIT,
                     (struct sockaddr *)&from, &fl);
        if (n > 0)
            sendto(echo, buf, (size_t)n, 0, (struct sockaddr *)&from, fl);
        if (poll(&p, 1, 0) > 0) {
            n = recv(cli, buf, sizeof buf, 0);
            if (n == 9 && memcmp(buf, "hello udp", 9) == 0)
                got = 1;
        }
    }
    CHECK(got);
    close(cli);
    close(echo);
}

/* ---------------------------------------------------------------- */
static void test_forward_lifecycle(fwd_ctx *ctx)
{
    char resp[FWD_MAX_RESP + 1], p1[16], p2[16], p3[16];
    unsigned a = free_port(AF_INET, SOCK_STREAM, "127.0.0.1");
    unsigned b = free_port(AF_INET, SOCK_STREAM, "127.0.0.1");
    unsigned c = free_port(AF_INET, SOCK_STREAM, "127.0.0.1");
    fwd_response r;
    char id[16];
    int before, after;

    snprintf(p1, sizeof p1, "%u", a);
    snprintf(p2, sizeof p2, "%u", b);
    snprintf(p3, sizeof p3, "%u", c);

    before = 0;
    {
        char l[FWD_MAX_RESP + 1];
        RUN(ctx, FWD_CMD_LIST_FWD, l, "0");
        fwd_response_parse(l, &r);
        before = atoi(r.payload);
    }
    RUN(ctx, FWD_CMD_FWD_ADD, resp, "tcp", "127.0.0.1", p1, "127.0.0.1", "9");
    fwd_response_parse(resp, &r);
    CHECK(r.status == 0);
    snprintf(id, sizeof id, "%d", atoi(r.payload));

    /* duplicate listen endpoint */
    RUN(ctx, FWD_CMD_FWD_ADD, resp, "tcp", "127.0.0.1", p1, "127.0.0.1", "9");
    fwd_response_parse(resp, &r);
    CHECK(r.status == -FWD_E_EXIST);
    CHECK(strstr(r.message, "already") != NULL);

    /* port taken by somebody else -> system error with errno */
    {
        int blocker = bound_socket(AF_INET, SOCK_STREAM, "127.0.0.1", b, 0);
        RUN(ctx, FWD_CMD_FWD_ADD, resp, "tcp", "127.0.0.1", p2, "127.0.0.1",
            "9");
        fwd_response_parse(resp, &r);
        CHECK(r.status == -FWD_E_SYS && r.sys_errno == EADDRINUSE);
        close(blocker);
    }

    /* modify: move the listener to another port */
    RUN(ctx, FWD_CMD_FWD_MOD, resp, id, "tcp", "127.0.0.1", p3, "127.0.0.1",
        "10");
    CHECK(status_of(resp) == 0);
    {
        char l[FWD_MAX_RESP + 1], want[40];
        RUN(ctx, FWD_CMD_LIST_FWD, l, "0");
        snprintf(want, sizeof want, ":2130706433:%u:2130706433:10:", c);
        CHECK(strstr(l, want) != NULL);
    }
    /* the old port is free again */
    {
        int fd = bound_socket(AF_INET, SOCK_STREAM, "127.0.0.1", a, 0);
        CHECK(fd >= 0);
        if (fd >= 0)
            close(fd);
    }

    RUN(ctx, FWD_CMD_FWD_DEL, resp, id);
    CHECK(status_of(resp) == 0);
    RUN(ctx, FWD_CMD_FWD_DEL, resp, id);
    fwd_response_parse(resp, &r);
    CHECK(r.status == -FWD_E_NOENT);
    RUN(ctx, FWD_CMD_FWD_MOD, resp, id, "tcp", "127.0.0.1", p3, "127.0.0.1",
        "10");
    CHECK(status_of(resp) == -FWD_E_NOENT);
    {
        char l[FWD_MAX_RESP + 1];
        RUN(ctx, FWD_CMD_LIST_FWD, l, "0");
        fwd_response_parse(l, &r);
        after = atoi(r.payload);
        CHECK(after == before);
    }
}

/* ---------------------------------------------------------------- */
/* error responses / commands that must not touch the system         */
/* ---------------------------------------------------------------- */
static void test_errors(fwd_ctx *ctx)
{
    char resp[FWD_MAX_RESP + 1];
    fwd_response r;

    CHECK(fwd_execute(ctx, "0", resp, sizeof resp) > 0);
    CHECK_STR(resp, "0:0");

    fwd_execute(ctx, "99", resp, sizeof resp);
    CHECK(fwd_response_parse(resp, &r) == FWD_OK && r.status == -FWD_E_PARSE);
    fwd_execute(ctx, "garbage", resp, sizeof resp);
    CHECK_STR(resp, "-1:2:0:malformed command");
    fwd_execute(ctx, "2:abc", resp, sizeof resp);
    CHECK(fwd_response_parse(resp, &r) == FWD_OK && r.cmd == 2 &&
          r.status == -FWD_E_PARSE);
    fwd_execute(ctx, "1:1:0:0:0:0", resp, sizeof resp); /* zero ports */
    CHECK(status_of(resp) == -FWD_E_INVAL);

    /* only net.ipv4/net.ipv6 sysctls are reachable */
    fwd_execute(ctx, "10:kernel.core_pattern:x", resp, sizeof resp);
    fwd_response_parse(resp, &r);
    CHECK(r.status == -FWD_E_INVAL);
    fwd_execute(ctx, "11:net.ipv4..ip_forward", resp, sizeof resp);
    CHECK(status_of(resp) == -FWD_E_INVAL);

    /* reading is fine */
    fwd_execute(ctx, "11:net.ipv4.ip_forward", resp, sizeof resp);
    fwd_response_parse(resp, &r);
    CHECK(r.status == 0 && (!strcmp(r.payload, "0") || !strcmp(r.payload, "1")));

    /* deleting a route that cannot exist must fail cleanly (no changes) */
    fwd_execute(ctx, "8:3221225984:24:0::0:0", resp, sizeof resp);
    CHECK(status_of(resp) > 0);

    /* route listing is read only */
    fwd_execute(ctx, "14", resp, sizeof resp);
    fwd_response_parse(resp, &r);
    CHECK(r.cmd == 14);
    if (r.status == 0)
        printf("  (kernel reports %d routes)\n", atoi(r.payload));

    fwd_execute(ctx, "13", resp, sizeof resp);
    CHECK(strncmp(resp, "13:0:0:0", 8) == 0);

    /* truncated response buffer */
    CHECK(fwd_execute(ctx, "0", resp, 8) == FWD_E_BUF);
}

#if FWD_ENABLE_TUNTAP
/* ---------------------------------------------------------------- */
/* tunnels on a fake /dev/net/tun (no root needed)                   */
/* ---------------------------------------------------------------- */
/* Each opened "device" is one end of an AF_UNIX datagram socketpair; the
 * test reads/writes packets on the other end. Interface ioctls succeed. */
static fwd_sys_ops g_real_ops;
static int g_fake_dev[8], g_fake_peer[8], g_nfake;

static int fake_open(const char *path, int flags)
{
    int sv[2];

    if (strcmp(path, "/dev/net/tun") != 0)
        return g_real_ops.open(path, flags);
    if (g_nfake == 8 || socketpair(AF_UNIX, SOCK_DGRAM, 0, sv) < 0)
        return -1;
    fcntl(sv[1], F_SETFL, fcntl(sv[1], F_GETFL) | O_NONBLOCK);
    g_fake_dev[g_nfake] = sv[0];
    g_fake_peer[g_nfake++] = sv[1];
    return sv[0];
}

static int fake_peer_of(int dev)
{
    int i;
    for (i = 0; i < g_nfake; i++)
        if (g_fake_dev[i] == dev)
            return g_fake_peer[i];
    return -1;
}

static int fake_ioctl(int fd, unsigned long req, void *arg)
{
    struct ifreq *ifr = arg;

    switch (req) {
    case TUNSETIFF:
        if (!ifr->ifr_name[0])
            snprintf(ifr->ifr_name, IFNAMSIZ, "fake%d", fd);
        return 0;
    case SIOCSIFMTU:
    case SIOCSIFFLAGS:
        return 0;
    case SIOCGIFFLAGS:
        ifr->ifr_flags = 0;
        return 0;
    }
    return g_real_ops.ioctl(fd, req, arg);
}

static void pump(fwd_ctx *ctx)
{
    int i;
    for (i = 0; i < 5; i++)
        fwd_run(ctx, 5);
}

/* recv with a short wait; returns length or -1, data NUL terminated */
static int recv_wait(int fd, char *buf, size_t len)
{
    struct pollfd p = {fd, POLLIN, 0};
    ssize_t n;

    if (poll(&p, 1, 200) <= 0)
        return -1;
    n = recv(fd, buf, len - 1, 0);
    if (n >= 0)
        buf[n] = '\0';
    return (int)n;
}

static unsigned sock_port(int fd)
{
    struct sockaddr_in sa;
    socklen_t sl = sizeof sa;
    getsockname(fd, (struct sockaddr *)&sa, &sl);
    return ntohs(sa.sin_port);
}

static void send_to(int fd, unsigned port, const char *msg)
{
    struct sockaddr_storage ss;
    socklen_t sl;
    make_sa(AF_INET, "127.0.0.1", port, &ss, &sl);
    sendto(fd, msg, strlen(msg), 0, (struct sockaddr *)&ss, sl);
}

/* A 24 byte IPv4 packet 10.0.0.<src> -> <dst> with a 4 byte payload. */
static void ip4(unsigned char *p, unsigned src, uint32_t dst, const char *pl)
{
    memset(p, 0, 24);
    p[0] = 0x45;
    p[3] = 24;
    p[8] = 64;
    p[9] = 17;
    p[12] = 10;
    p[15] = (unsigned char)src;
    p[16] = (unsigned char)(dst >> 24);
    p[17] = (unsigned char)(dst >> 16);
    p[18] = (unsigned char)(dst >> 8);
    p[19] = (unsigned char)dst;
    memcpy(p + 20, pl, 4);
}
#define TEN(x) (0x0a000000u | (x)) /* 10.0.0.x */

static void send_raw(int fd, unsigned port, const void *buf, size_t n)
{
    struct sockaddr_storage ss;
    socklen_t sl;
    make_sa(AF_INET, "127.0.0.1", port, &ss, &sl);
    sendto(fd, buf, n, 0, (struct sockaddr *)&ss, sl);
}

/* Plain UDP transport (FWD_TT_UDP); QUIC is test_tun_quic(). */
static void test_tun_listen(void)
{
    fwd_sys_ops fake;
    fwd_ctx *ctx;
    fwd_tunnel_spec sp;
    fwd_tunnel_info info;
    char buf[256], resp[FWD_MAX_RESP + 1], out[FWD_MAX_DECODED];
    unsigned lp = free_port(AF_INET, SOCK_DGRAM, "127.0.0.1");
    unsigned cp, cp2, lp2;
    int c1 = bound_socket(AF_INET, SOCK_DGRAM, "127.0.0.1", 0, 1);
    int c2 = bound_socket(AF_INET, SOCK_DGRAM, "127.0.0.1", 0, 1);
    int dev, dev2;
    uint32_t id, id2;
    fwd_cmd c;

    fwd_sys_ops_default(&g_real_ops);
    fake = g_real_ops;
    fake.open = fake_open;
    fake.ioctl = fake_ioctl;
    fwd_sys_set_ops(&fake);
    ctx = fwd_create(NULL);
    cp = sock_port(c1);
    cp2 = sock_port(c2);

    /* codec: mode is an optional trailing argument */
    {
        const char *a9[] = {"tun", "", "127.0.0.1", "4000", "0", "0",
                            "0", "0", "0"};
        const char *a10[] = {"tun", "", "127.0.0.1", "4000", "0", "0",
                             "0", "0", "0", "listen"};
        const char *bad[] = {"tun", "", "127.0.0.1", "4000", "0", "0",
                             "0", "0", "0", "2"};
        const char *a11[] = {"tun", "", "127.0.0.1", "4000", "0", "0",
                             "0", "0", "0", "connect", "udp"};
        const char *a12[] = {"tun", "", "127.0.0.1", "4000", "0", "0",
                             "0", "0", "0", "0", "auto",
                             "AB:cd:ef:01:23:45:67:89:ab:cd:ef:01:23:45:67:89:"
                             "ab:cd:ef:01:23:45:67:89:ab:cd:ef:01:23:45:67:89"};
        const char *badpin[] = {"tun", "", "127.0.0.1", "4000", "0", "0",
                                "0", "0", "0", "0", "quic", "abcd"};
        const char *badtt[] = {"tun", "", "127.0.0.1", "4000", "0", "0",
                               "0", "0", "0", "0", "tcp"};
        char line[FWD_MAX_CMD + 1];
        CHECK(fwd_cmd_encode_strs(FWD_CMD_TUN_ADD, a9, 9, line,
                                  sizeof line) > 0);
        CHECK(fwd_cmd_decode(line, &c) == FWD_OK &&
              c.u.tun.mode == FWD_TUN_CONNECT);
        CHECK(fwd_cmd_encode_strs(FWD_CMD_TUN_ADD, a10, 10, line,
                                  sizeof line) > 0);
        CHECK_STR(line, "4:1::2130706433:4000:0:0:0:0:0:1");
        CHECK(fwd_cmd_decode(line, &c) == FWD_OK &&
              c.u.tun.mode == FWD_TUN_LISTEN);
        CHECK(fwd_cmd_encode(&c, line, sizeof line) > 0);
        CHECK_STR(line, "4:1::2130706433:4000:0:0:0:0:0:1");
        CHECK(fwd_cmd_encode_strs(FWD_CMD_TUN_ADD, bad, 10, line,
                                  sizeof line) == FWD_E_RANGE);
        /* transport and pin follow the mode; defaults are left out */
        CHECK(fwd_cmd_encode_strs(FWD_CMD_TUN_ADD, a11, 11, line,
                                  sizeof line) > 0);
        CHECK_STR(line, "4:1::2130706433:4000:0:0:0:0:0:0:2");
        CHECK(fwd_cmd_decode(line, &c) == FWD_OK &&
              c.u.tun.transport == FWD_TT_UDP && c.u.tun.pin[0] == 0);
        CHECK(fwd_cmd_encode_strs(FWD_CMD_TUN_ADD, a12, 12, line,
                                  sizeof line) > 0);
        CHECK_STR(line, "4:1::2130706433:4000:0:0:0:0:0:0:0:"
                        "abcdef0123456789abcdef0123456789"
                        "abcdef0123456789abcdef0123456789");
        CHECK(fwd_cmd_decode(line, &c) == FWD_OK &&
              c.u.tun.transport == FWD_TT_AUTO &&
              strlen(c.u.tun.pin) == 64);
        c.u.tun.pin[0] = '\0'; /* no pin, auto, connect: all left out */
        CHECK(fwd_cmd_encode(&c, line, sizeof line) > 0);
        CHECK_STR(line, "4:1::2130706433:4000:0:0:0:0:0");
        snprintf(c.u.tun.pin, sizeof c.u.tun.pin, "%s", a12[11]);
        CHECK(fwd_cmd_encode(&c, line, sizeof line) > 0 &&
              strlen(strrchr(line, ':') + 1) == 64);
        CHECK(fwd_cmd_encode_strs(FWD_CMD_TUN_ADD, badpin, 12, line,
                                  sizeof line) == FWD_E_PARSE);
        CHECK(fwd_cmd_encode_strs(FWD_CMD_TUN_ADD, badtt, 11, line,
                                  sizeof line) == FWD_E_PARSE);
        CHECK(fwd_cmd_decode("4:1::2130706433:4000:0:0:0:0:0:0:3", &c) ==
              FWD_E_RANGE);
        CHECK(fwd_cmd_decode("4:1::2130706433:4000:0:0:0:0:0:0:1:AB:CD",
                             &c) == FWD_E_PARSE);
    }

    /* validation */
    memset(&sp, 0, sizeof sp);
    sp.type = FWD_TUN_TUN;
    sp.transport = FWD_TT_UDP;
    fwd_addr_from_string(&sp.laddr, "127.0.0.1");
    sp.lport = (uint16_t)lp;
    CHECK(fwd_tunnel_add(ctx, &sp, &id) == FWD_E_INVAL); /* connect, no peer */
    sp.mode = FWD_TUN_LISTEN;
    sp.transport = 3;
    CHECK(fwd_tunnel_add(ctx, &sp, &id) == FWD_E_INVAL);
    sp.transport = FWD_TT_UDP;
    snprintf(sp.pin, sizeof sp.pin, "%064d", 0);
    CHECK(fwd_tunnel_add(ctx, &sp, &id) == FWD_E_INVAL); /* pin needs QUIC */
#if !FWD_ENABLE_QUIC
    sp.transport = FWD_TT_AUTO; /* auto is plain UDP then, a pin is not */
    CHECK(fwd_tunnel_add(ctx, &sp, &id) == FWD_E_NOSYS);
    sp.pin[0] = '\0';
    sp.transport = FWD_TT_QUIC;
    CHECK(fwd_tunnel_add(ctx, &sp, &id) == FWD_E_NOSYS);
    CHECK(fwd_tunnel_tls(ctx, NULL, NULL, NULL) == FWD_E_NOSYS);
    sp.transport = FWD_TT_UDP;
#endif
    sp.pin[0] = '\0';
    sp.mode = FWD_TUN_CONNECT;
    sp.mode = 7;
    CHECK(fwd_tunnel_add(ctx, &sp, &id) == FWD_E_INVAL);
    sp.mode = FWD_TUN_LISTEN;
    CHECK(fwd_tunnel_add(ctx, &sp, &id) == FWD_OK); /* raddr unset = any */
    dev = fake_peer_of(g_fake_dev[g_nfake - 1]);

    /* no peer yet: packets from the interface are dropped */
    CHECK(fwd_tunnel_get(ctx, id, &info) == FWD_OK && info.peer.family == 0);
    send(dev, "early", 5, 0);
    pump(ctx);
    CHECK(fwd_tunnel_get(ctx, id, &info) == FWD_OK && info.tx_packets == 0);
    CHECK(recv_wait(c1, buf, sizeof buf) < 0);

    /* the first datagram makes its sender the peer */
    send_to(c1, lp, "hello");
    pump(ctx);
    CHECK(recv_wait(dev, buf, sizeof buf) == 5 && strcmp(buf, "hello") == 0);
    CHECK(fwd_tunnel_get(ctx, id, &info) == FWD_OK && info.rx_packets == 1 &&
          info.peer.family == FWD_V4 && info.peer_port == cp);
    send(dev, "reply", 5, 0);
    pump(ctx);
    CHECK(recv_wait(c1, buf, sizeof buf) == 5 && strcmp(buf, "reply") == 0);

    /* a second sender is a second peer. Device packets go to the peer that
     * sent from their destination, unknown / multicast ones to both */
    {
        unsigned char p1[24], p2[24], pk[24];
        ip4(p1, 1, TEN(9), "c1c1"); /* c1 is 10.0.0.1 inside */
        ip4(p2, 2, TEN(9), "c2c2"); /* c2 is 10.0.0.2 */
        send_raw(c1, lp, p1, 24);
        send_raw(c2, lp, p2, 24);
        pump(ctx);
        CHECK(recv_wait(dev, buf, sizeof buf) == 24);
        CHECK(recv_wait(dev, buf, sizeof buf) == 24);
        CHECK(fwd_tunnel_get(ctx, id, &info) == FWD_OK && info.peers == 2 &&
              info.peer_port == cp2); /* heard from last */
        ip4(pk, 9, TEN(2), "to-2");
        send(dev, pk, 24, 0);
        pump(ctx);
        CHECK(recv_wait(c2, buf, sizeof buf) == 24 &&
              memcmp(buf + 20, "to-2", 4) == 0);
        CHECK(recv_wait(c1, buf, sizeof buf) < 0);
        ip4(pk, 9, TEN(1), "to-1");
        send(dev, pk, 24, 0);
        pump(ctx);
        CHECK(recv_wait(c1, buf, sizeof buf) == 24);
        CHECK(recv_wait(c2, buf, sizeof buf) < 0);
        ip4(pk, 9, TEN(77), "unkn"); /* nobody owns 10.0.0.77 */
        send(dev, pk, 24, 0);
        pump(ctx);
        CHECK(recv_wait(c1, buf, sizeof buf) == 24);
        CHECK(recv_wait(c2, buf, sizeof buf) == 24);
        ip4(pk, 9, 0xe0000001u, "mcst"); /* 224.0.0.1 */
        send(dev, pk, 24, 0);
        pump(ctx);
        CHECK(recv_wait(c1, buf, sizeof buf) == 24);
        CHECK(recv_wait(c2, buf, sizeof buf) == 24);
        CHECK(fwd_tunnel_get(ctx, id, &info) == FWD_OK &&
              info.tx_packets == 5); /* "reply" before */
        /* c2 now sends from 10.0.0.1 too: it owns that address */
        ip4(p2, 1, TEN(9), "c2-1");
        send_raw(c2, lp, p2, 24);
        pump(ctx);
        CHECK(recv_wait(dev, buf, sizeof buf) == 24);
        ip4(pk, 9, TEN(1), "to-1");
        send(dev, pk, 24, 0);
        pump(ctx);
        CHECK(recv_wait(c2, buf, sizeof buf) == 24);
        CHECK(recv_wait(c1, buf, sizeof buf) < 0);
        /* both listed */
        snprintf(buf, sizeof buf, "15:%u", id);
        CHECK(fwd_execute(ctx, buf, resp, sizeof resp) > 0);
        CHECK(fwd_response_decode(resp, out, sizeof out) > 0);
        CHECK(strncmp(out, "PEER ", 5) == 0);
        snprintf(buf, sizeof buf, "127.0.0.1:%u  udp  4   2   0\n", cp2);
        CHECK(strstr(out, buf) != NULL);
        snprintf(buf, sizeof buf, "127.0.0.1:%u  udp  4   2   0\n", cp);
        CHECK(strstr(out, buf) != NULL);
        CHECK(fwd_execute(ctx, "15:999", resp, sizeof resp) > 0 &&
              status_of(resp) == -FWD_E_NOENT);
    }

    /* filter on the source port: c2 is no longer acceptable and dropped */
    fwd_addr_from_string(&sp.raddr, "127.0.0.1");
    sp.rport = (uint16_t)cp;
    CHECK(fwd_tunnel_mod(ctx, id, &sp) == FWD_OK);
    CHECK(fwd_tunnel_get(ctx, id, &info) == FWD_OK && info.peers == 1 &&
          info.peer_port == cp);
    send_to(c2, lp, "denied");
    pump(ctx);
    CHECK(recv_wait(dev, buf, sizeof buf) < 0);
    send_to(c1, lp, "allowed");
    pump(ctx);
    CHECK(recv_wait(dev, buf, sizeof buf) == 7 && strcmp(buf, "allowed") == 0);
    /* filter on another address */
    fwd_addr_from_string(&sp.raddr, "127.0.0.2");
    sp.rport = 0;
    CHECK(fwd_tunnel_mod(ctx, id, &sp) == FWD_OK);
    send_to(c1, lp, "nope");
    pump(ctx);
    CHECK(recv_wait(dev, buf, sizeof buf) < 0);
    CHECK(fwd_tunnel_get(ctx, id, &info) == FWD_OK && info.peers == 0 &&
          info.peer.family == 0);

    /* listen -> connect on the same socket, towards c1 */
    sp.mode = FWD_TUN_CONNECT;
    fwd_addr_from_string(&sp.raddr, "127.0.0.1");
    sp.rport = (uint16_t)cp;
    CHECK(fwd_tunnel_mod(ctx, id, &sp) == FWD_OK);
    send(dev, "conn", 4, 0);
    pump(ctx);
    CHECK(recv_wait(c1, buf, sizeof buf) == 4 && strcmp(buf, "conn") == 0);
    CHECK(fwd_tunnel_get(ctx, id, &info) == FWD_OK &&
          info.peer_port == cp);
    /* and back to listen: the connected association is dissolved */
    sp.mode = FWD_TUN_LISTEN;
    sp.rport = 0;
    memset(&sp.raddr, 0, sizeof sp.raddr);
    CHECK(fwd_tunnel_mod(ctx, id, &sp) == FWD_OK);
    send_to(c2, lp, "again");
    pump(ctx);
    CHECK(recv_wait(dev, buf, sizeof buf) == 5 && strcmp(buf, "again") == 0);

    /* over the command protocol, listed and decoded */
    lp2 = free_port(AF_INET, SOCK_DGRAM, "127.0.0.1");
    snprintf(buf, sizeof buf, "%u", lp2);
    CHECK(status_of((RUN(ctx, FWD_CMD_TUN_ADD, resp, "tun", "lst0",
                         "127.0.0.1", buf, "0", "0", "0", "0", "0",
                         "listen", "udp"),
                     resp)) == 0);
    CHECK(sscanf(resp, "4:0:%u", &id2) == 1);
    dev2 = fake_peer_of(g_fake_dev[g_nfake - 1]);
    send_to(c1, lp2, "x");
    pump(ctx);
    CHECK(recv_wait(dev2, buf, sizeof buf) == 1);
    CHECK(fwd_execute(ctx, "13", resp, sizeof resp) > 0);
    CHECK(fwd_response_decode(resp, out, sizeof out) > 0);
    /* lst0 learned c1, the first tunnel c2; the filter shows as "*:*" */
    snprintf(buf, sizeof buf, "lst0    127.0.0.1:%u  *:*", lp2);
    CHECK(strstr(out, buf) != NULL);
    snprintf(buf, sizeof buf, "listen  127.0.0.1:%u  udp        udp  1\n",
             cp);
    CHECK(strstr(out, buf) != NULL);
    snprintf(buf, sizeof buf, "listen  127.0.0.1:%u  udp        udp  1\n",
             cp2);
    CHECK(strstr(out, buf) != NULL);

    /* a full table: the peer silent for longest makes room */
    {
        int many[FWD_TUN_MAX_PEERS + 1], i, ok = 1;
        for (i = 0; i <= FWD_TUN_MAX_PEERS; i++) {
            many[i] = bound_socket(AF_INET, SOCK_DGRAM, "127.0.0.1", 0, 1);
            send_to(many[i], lp2, "m");
            fwd_run(ctx, 1);
        }
        pump(ctx);
        while (recv(dev2, buf, sizeof buf, MSG_DONTWAIT) >= 0)
            ;
        CHECK(fwd_tunnel_get(ctx, id2, &info) == FWD_OK &&
              info.peers == FWD_TUN_MAX_PEERS);
        send(dev2, "all", 3, 0); /* not IP: to everybody */
        pump(ctx);
        /* c1 was there before; it and many[0] made room */
        CHECK(recv_wait(c1, buf, sizeof buf) < 0);
        CHECK(recv_wait(many[0], buf, sizeof buf) < 0);
        for (i = 1; i <= FWD_TUN_MAX_PEERS; i++) {
            ok &= recv_wait(many[i], buf, sizeof buf) == 3;
            close(many[i]);
        }
        close(many[0]);
        CHECK(ok);
    }

    fwd_destroy(ctx);
    fwd_sys_set_ops(NULL);
    while (g_nfake)
        close(g_fake_peer[--g_nfake]);
    close(c1);
    close(c2);
}

#if FWD_ENABLE_QUIC
/* ---------------------------------------------------------------- */
/* tunnels over QUIC (both ends in this one context)                 */
/* ---------------------------------------------------------------- */
static int via_of(fwd_ctx *ctx, uint32_t id)
{
    fwd_tunnel_info info;
    return fwd_tunnel_get(ctx, id, &info) == FWD_OK ? info.via : -1;
}

static int wait_via(fwd_ctx *ctx, uint32_t id, int via, int ms)
{
    int i;
    for (i = 0; i < ms / 10; i++) {
        if (via_of(ctx, id) == via)
            return 1;
        fwd_run(ctx, 10);
    }
    return via_of(ctx, id) == via;
}

/* run the context until fd has a datagram; length or -1 */
static int pump_recv(fwd_ctx *ctx, int fd, char *buf, size_t len, int ms)
{
    int i;
    for (i = 0; i < ms / 10; i++) {
        ssize_t n = recv(fd, buf, len - 1, MSG_DONTWAIT);
        if (n >= 0) {
            buf[n] = '\0';
            return (int)n;
        }
        fwd_run(ctx, 10);
    }
    return -1;
}

/* drain fd, 1 if one of the datagrams was msg */
static int got_among(int fd, const char *msg)
{
    char buf[2048];
    ssize_t n;
    int found = 0;
    while ((n = recv(fd, buf, sizeof buf - 1, MSG_DONTWAIT)) >= 0) {
        buf[n] = '\0';
        found |= (size_t)n == strlen(msg) && memcmp(buf, msg, n) == 0;
    }
    return found;
}

static uint32_t add_tun(fwd_ctx *ctx, int mode, int transport, unsigned lport,
                        unsigned rport, const char *pin, int *dev)
{
    fwd_tunnel_spec sp;
    uint32_t id = 0;

    memset(&sp, 0, sizeof sp);
    sp.type = FWD_TUN_TUN;
    sp.mode = (uint8_t)mode;
    sp.transport = (uint8_t)transport;
    fwd_addr_from_string(&sp.laddr, "127.0.0.1");
    sp.lport = (uint16_t)lport;
    if (rport) {
        fwd_addr_from_string(&sp.raddr, "127.0.0.1");
        sp.rport = (uint16_t)rport;
    }
    if (pin)
        snprintf(sp.pin, sizeof sp.pin, "%s", pin);
    CHECK(fwd_tunnel_add(ctx, &sp, &id) == FWD_OK);
    *dev = g_fake_peer[g_nfake - 1]; /* fd numbers of deleted ones recur */
    return id;
}

static void set_pin(fwd_ctx *ctx, uint32_t id, const char *pin)
{
    fwd_tunnel_info info;
    CHECK(fwd_tunnel_get(ctx, id, &info) == FWD_OK);
    snprintf(info.spec.pin, sizeof info.spec.pin, "%s", pin);
    CHECK(fwd_tunnel_mod(ctx, id, &info.spec) == FWD_OK);
}

static void test_tun_quic(void)
{
    char dir[] = "/tmp/fwdtest.XXXXXX", cert[64], key[64];
    char fp[FWD_FINGERPRINT_LEN], zero[65], buf[2048], big[1400];
    char resp[FWD_MAX_RESP + 1], out[FWD_MAX_DECODED];
    unsigned la = free_port(AF_INET, SOCK_DGRAM, "127.0.0.1");
    unsigned lb = free_port(AF_INET, SOCK_DGRAM, "127.0.0.1");
    unsigned lc, ld, le;
    int u = bound_socket(AF_INET, SOCK_DGRAM, "127.0.0.1", 0, 1);
    int u2 = bound_socket(AF_INET, SOCK_DGRAM, "127.0.0.1", 0, 1);
    int da, db, dc, dd, de;
    uint32_t a, b, c, d, e;
    fwd_tunnel_info info;
    fwd_sys_ops fake;
    fwd_ctx *ctx;

    if (!mkdtemp(dir)) {
        CHECK(!"mkdtemp");
        return;
    }
    snprintf(cert, sizeof cert, "%s/cert.pem", dir);
    snprintf(key, sizeof key, "%s/key.pem", dir);
    CHECK(fwd_tls_make_selfsigned(cert, key, NULL, "peer.test", 1) == FWD_OK);
    CHECK(fwd_tls_fingerprint(cert, fp, sizeof fp) == FWD_OK);
    memset(zero, '0', 64);
    zero[64] = '\0';

    fwd_sys_ops_default(&g_real_ops);
    fake = g_real_ops;
    fake.open = fake_open;
    fake.ioctl = fake_ioctl;
    fwd_sys_set_ops(&fake);
    ctx = fwd_create(NULL);

    /* default transport, ephemeral certificate: A listens, B connects */
    a = add_tun(ctx, FWD_TUN_LISTEN, FWD_TT_AUTO, la, 0, NULL, &da);
    b = add_tun(ctx, FWD_TUN_CONNECT, FWD_TT_AUTO, lb, la, NULL, &db);
    CHECK(via_of(ctx, b) == FWD_VIA_NONE); /* handshake running */
    CHECK(wait_via(ctx, b, FWD_VIA_QUIC, 2000));
    CHECK(wait_via(ctx, a, FWD_VIA_QUIC, 2000));
    send(db, "b->a", 4, 0);
    CHECK(pump_recv(ctx, da, buf, sizeof buf, 1000) == 4 &&
          strcmp(buf, "b->a") == 0);
    send(da, "a->b", 4, 0);
    CHECK(pump_recv(ctx, db, buf, sizeof buf, 1000) == 4 &&
          strcmp(buf, "a->b") == 0);
    memset(big, 'x', sizeof big);
    big[0] = 0x45; /* looks like IPv4, and like a QUIC short header */
    send(db, big, sizeof big, 0);
    CHECK(pump_recv(ctx, da, buf, sizeof buf, 1000) == (int)sizeof big &&
          memcmp(buf, big, sizeof big) == 0);
    CHECK(fwd_tunnel_get(ctx, a, &info) == FWD_OK && info.peer_port == lb &&
          info.peer.family == FWD_V4 && info.tx_packets == 1 &&
          info.rx_packets == 2);
    /* plain datagrams are not taken while QUIC is up */
    send_to(u, la, "raw");
    CHECK(pump_recv(ctx, da, buf, sizeof buf, 200) < 0);
    CHECK(fwd_execute(ctx, "13", resp, sizeof resp) > 0 &&
          fwd_response_decode(resp, out, sizeof out) > 0);
    snprintf(buf, sizeof buf, "listen   127.0.0.1:%u  auto       quic  1\n",
             lb);
    CHECK(strstr(out, buf) != NULL);
    snprintf(buf, sizeof buf, "connect  127.0.0.1:%u  auto       quic  1\n",
             la);
    CHECK(strstr(out, buf) != NULL);

    /* a second client of A at the same time; A routes by inner address */
    {
        unsigned lh = free_port(AF_INET, SOCK_DGRAM, "127.0.0.1");
        unsigned char pk[24];
        int dh, i;
        uint32_t h = add_tun(ctx, FWD_TUN_CONNECT, FWD_TT_AUTO, lh, la, NULL,
                             &dh);
        CHECK(wait_via(ctx, h, FWD_VIA_QUIC, 2000));
        ip4(pk, 2, TEN(9), "from");
        send(db, pk, 24, 0); /* B is 10.0.0.2 */
        CHECK(pump_recv(ctx, da, buf, sizeof buf, 1000) == 24);
        ip4(pk, 3, TEN(9), "from");
        send(dh, pk, 24, 0); /* H is 10.0.0.3 */
        CHECK(pump_recv(ctx, da, buf, sizeof buf, 1000) == 24);
        CHECK(fwd_tunnel_get(ctx, a, &info) == FWD_OK && info.peers == 2 &&
              info.via == FWD_VIA_QUIC && info.peer_port == lh);
        ip4(pk, 9, TEN(3), "to-H");
        send(da, pk, 24, 0);
        CHECK(pump_recv(ctx, dh, buf, sizeof buf, 1000) == 24 &&
              memcmp(buf + 20, "to-H", 4) == 0);
        CHECK(pump_recv(ctx, db, buf, sizeof buf, 100) < 0);
        ip4(pk, 9, TEN(2), "to-B");
        send(da, pk, 24, 0);
        CHECK(pump_recv(ctx, db, buf, sizeof buf, 1000) == 24 &&
              memcmp(buf + 20, "to-B", 4) == 0);
        CHECK(pump_recv(ctx, dh, buf, sizeof buf, 100) < 0);
        ip4(pk, 9, TEN(50), "both");
        send(da, pk, 24, 0);
        CHECK(pump_recv(ctx, db, buf, sizeof buf, 1000) == 24);
        CHECK(pump_recv(ctx, dh, buf, sizeof buf, 1000) == 24);
        /* H goes away: A drops it once the connection is closed */
        CHECK(fwd_tunnel_del(ctx, h) == FWD_OK);
        for (i = 0; i < 100; i++) {
            if (fwd_tunnel_get(ctx, a, &info) == FWD_OK && info.peers == 1)
                break;
            fwd_run(ctx, 10);
        }
        CHECK(info.peers == 1 && info.peer_port == lb);
        send(da, "only-b", 6, 0); /* one peer: everything goes to it */
        CHECK(pump_recv(ctx, db, buf, sizeof buf, 1000) == 6);
    }

    /* a configured certificate, pinned by the connecting side ... */
    CHECK(fwd_tunnel_tls(ctx, key, key, NULL) == FWD_E_TLS);
    CHECK(fwd_tunnel_tls(ctx, cert, NULL, NULL) == FWD_E_INVAL);
    CHECK(fwd_tunnel_tls(ctx, cert, key, NULL) == FWD_OK);
    set_pin(ctx, b, fp);
    CHECK(wait_via(ctx, b, FWD_VIA_QUIC, 2000));
    CHECK(wait_via(ctx, a, FWD_VIA_QUIC, 2000));
    /* ... and by the listening side, which then wants a client cert */
    set_pin(ctx, a, fp);
    CHECK(wait_via(ctx, a, FWD_VIA_QUIC, 4000)); /* B reconnected */
    CHECK(wait_via(ctx, b, FWD_VIA_QUIC, 2000)); /* and was accepted */
    send(db, "pinned", 6, 0);
    CHECK(pump_recv(ctx, da, buf, sizeof buf, 1000) == 6);
    /* the wrong pin on either side: rejected, and no plain UDP fallback
     * (A would take plain datagrams now) */
    set_pin(ctx, b, zero);
    CHECK(wait_via(ctx, b, FWD_VIA_REJECTED, 2000));
    send(db, "leak", 4, 0);
    CHECK(pump_recv(ctx, da, buf, sizeof buf, 300) < 0);
    set_pin(ctx, b, "");
    set_pin(ctx, a, zero);
    CHECK(wait_via(ctx, b, FWD_VIA_REJECTED, 2000));
    send(db, "leak", 4, 0);
    CHECK(pump_recv(ctx, da, buf, sizeof buf, 300) < 0);
    set_pin(ctx, a, "");
    set_pin(ctx, b, fp); /* a change retries at once, not after backoff */
    CHECK(wait_via(ctx, b, FWD_VIA_QUIC, 2000));

    /* two connect mode ends: each is server and client */
    {
        unsigned lf = free_port(AF_INET, SOCK_DGRAM, "127.0.0.1");
        unsigned lg = free_port(AF_INET, SOCK_DGRAM, "127.0.0.1");
        int df, dg;
        uint32_t f = add_tun(ctx, FWD_TUN_CONNECT, FWD_TT_AUTO, lf, lg, fp,
                             &df);
        uint32_t g = add_tun(ctx, FWD_TUN_CONNECT, FWD_TT_QUIC, lg, lf, NULL,
                             &dg);
        CHECK(wait_via(ctx, f, FWD_VIA_QUIC, 2000));
        CHECK(wait_via(ctx, g, FWD_VIA_QUIC, 2000));
        send(df, "f->g", 4, 0);
        CHECK(pump_recv(ctx, dg, buf, sizeof buf, 1000) == 4);
        send(dg, "g->f", 4, 0);
        CHECK(pump_recv(ctx, df, buf, sizeof buf, 1000) == 4);
        CHECK(fwd_tunnel_del(ctx, f) == FWD_OK);
        CHECK(fwd_tunnel_del(ctx, g) == FWD_OK);
    }

    /* a listening auto tunnel takes plain UDP from an old style peer */
    le = free_port(AF_INET, SOCK_DGRAM, "127.0.0.1");
    e = add_tun(ctx, FWD_TUN_LISTEN, FWD_TT_AUTO, le, 0, NULL, &de);
    send_to(u, le, "plain"); /* on TUN only what looks like IP counts */
    CHECK(pump_recv(ctx, de, buf, sizeof buf, 200) < 0);
    {
        struct sockaddr_in sa;
        unsigned char ip[28] = {0x45, 0, 0, 28, 0, 0, 0, 0, 64, 17};
        memset(&sa, 0, sizeof sa);
        sa.sin_family = AF_INET;
        sa.sin_port = htons((uint16_t)le);
        sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        sendto(u, ip, sizeof ip, 0, (struct sockaddr *)&sa, sizeof sa);
    }
    CHECK(pump_recv(ctx, de, buf, sizeof buf, 1000) == 28);
    CHECK(via_of(ctx, e) == FWD_VIA_UDP);
    send(de, "plain-back", 10, 0);
    fwd_run(ctx, 10);
    CHECK(recv_wait(u, buf, sizeof buf) == 10);

    /* peers without QUIC: auto falls back after 3 s, quic only drops */
    lc = free_port(AF_INET, SOCK_DGRAM, "127.0.0.1");
    ld = free_port(AF_INET, SOCK_DGRAM, "127.0.0.1");
    c = add_tun(ctx, FWD_TUN_CONNECT, FWD_TT_AUTO, lc, sock_port(u), NULL,
                &dc);
    d = add_tun(ctx, FWD_TUN_CONNECT, FWD_TT_QUIC, ld, sock_port(u2), NULL,
                &dd);
    send(dc, "early", 5, 0); /* dropped while QUIC is being tried */
    CHECK(wait_via(ctx, c, FWD_VIA_UDP, 5000));
    CHECK(via_of(ctx, d) == FWD_VIA_NONE);
    CHECK(!got_among(u, "early"));
    send(dc, "fallback", 8, 0);
    send(dd, "nope", 4, 0);
    pump(ctx);
    CHECK(got_among(u, "fallback"));
    CHECK(!got_among(u2, "nope"));
    send_to(u, lc, "back");
    CHECK(pump_recv(ctx, dc, buf, sizeof buf, 1000) == 4);

    fwd_destroy(ctx);
    fwd_sys_set_ops(NULL);
    while (g_nfake)
        close(g_fake_peer[--g_nfake]);
    close(u);
    close(u2);
    unlink(cert);
    unlink(key);
    rmdir(dir);
}
#endif /* FWD_ENABLE_QUIC */

#endif /* FWD_ENABLE_TUNTAP */

/* ---------------------------------------------------------------- */
/* readable decoding of responses                                    */
/* ---------------------------------------------------------------- */
static void decodes_to(const char *resp, const char *want, int line)
{
    char out[FWD_MAX_DECODED];
    int n = fwd_response_decode(resp, out, sizeof out);

    if (n >= 0 && strcmp(out, want) == 0 && (size_t)n == strlen(want)) {
        g_ok++;
        return;
    }
    g_fail++;
    fprintf(stderr, "FAIL %s:%d: decode '%s' -> %d\n'%s'\nwant\n'%s'\n",
            __FILE__, line, resp, n, n >= 0 ? out : "", want);
}
#define DECODES_TO(resp, want) decodes_to((resp), (want), __LINE__)

static void test_decode(void)
{
    char out[FWD_MAX_DECODED], resp[FWD_MAX_RESP + 1];
    fwd_ctx *ctx;

    DECODES_TO("0:0", "ok\n");
    DECODES_TO("2:0", "ok\n");
    DECODES_TO("1:0:7", "ok, id 7\n");
    DECODES_TO("4:0:2:tun3", "ok, id 2, interface tun3\n");
    DECODES_TO("4:0:2", "ok, id 2\n");
    DECODES_TO("11:0:1", "1\n");
    DECODES_TO("2:5:0:no forward with id 9",
               "fwd-del: error 5 (no such entry): no forward with id 9\n");
    DECODES_TO("-1:2:0:malformed command",
               "error 2 (malformed command): malformed command\n");
    DECODES_TO("7:4:1:netlink  Operation not permitted",
               "route-add: error 4 (system call failed): netlink  "
               "Operation not permitted [errno 1]\n");
    DECODES_TO("-1:10:0:authentication failed",
               "error 10 (authentication failed): authentication failed\n");

    /* tables: addresses in human form, ports and prefixes joined, IPv6 in
     * brackets, the unset / zero values as "-" */
    DECODES_TO("12:0:0:0", "no entries\n");
    DECODES_TO("12:0:4:0", "no entries at this offset (4 in total)\n");
    DECODES_TO("12:0:2:2:1:1:2130706433:8080:3232235786:80:3:"
               "2:2:0x00000000000000000000000000000001:53:"
               "0x20010db8000000000000000000000005:5353:0",
               "ID  PROTO  LISTEN          REMOTE              ACTIVE\n"
               "1   tcp    127.0.0.1:8080  192.168.1.10:80     3\n"
               "2   udp    [::1]:53        [2001:db8::5]:5353  0\n");
    DECODES_TO("12:0:5:1:4:1:0:1:0:2:0",
               "ID  PROTO  LISTEN     REMOTE     ACTIVE\n"
               "4   tcp    0.0.0.0:1  0.0.0.0:2  0\n"
               "(1 of 5 entries, page with the offset argument)\n");
    DECODES_TO("13:0:3:3:1:1:tun0:3221225985:4000:3221225986:4000:"
               "172818433:24:1400:10:12:0:3221225986:4000:2:2:1:"
               "2:2::0:4001:3221225986:4001:0:0:0:0:18446744073709551615:0:"
               "3221225986:4001:0:1:1:"
               "3:1:tun1:0x00000000000000000000000000000001:5000:0:0:0:0:0:"
               "0:0:1:0:0:1:0:0",
               "ID  TYPE  IFNAME  LOCAL           PEER            "
               "ADDRESS       MTU   TX  RX                    MODE     "
               "CURRENT         TRANSPORT  VIA   PEERS\n"
               "1   tun   tun0    192.0.2.1:4000  192.0.2.2:4000  "
               "10.77.0.1/24  1400  10  12                    connect  "
               "192.0.2.2:4000  udp        udp   1\n"
               "2   tap   -       0.0.0.0:4001    192.0.2.2:4001  "
               "-             -     0   18446744073709551615  connect  "
               "192.0.2.2:4001  auto       quic  1\n"
               "3   tun   tun1    [::1]:5000      *:*             "
               "-             -     0   0                     listen   "
               "-               quic       -     0\n");
    DECODES_TO("15:0:2:2:3221225986:4000:1:5:7:0:"
               "0x20010db8000000000000000000000005:4001:2:0:1:130",
               "PEER                VIA   TX  RX  IDLE\n"
               "192.0.2.2:4000      quic  5   7   0\n"
               "[2001:db8::5]:4001  udp   0   1   130\n");
    DECODES_TO("14:0:2:2:0:0:3232235777:eth0:100:254:"
               "0x20010db8000000000000000000000000:32:0::0:254",
               "DESTINATION    GATEWAY      DEV   METRIC  TABLE\n"
               "0.0.0.0/0      192.168.1.1  eth0  100     254\n"
               "2001:db8::/32  -            -     0       254\n");

    /* not well formed for the command */
    CHECK(fwd_response_decode("garbage", out, sizeof out) == FWD_E_PARSE);
    CHECK(fwd_response_decode("0:0:junk", out, sizeof out) == FWD_E_PARSE);
    CHECK(fwd_response_decode("1:0:abc", out, sizeof out) == FWD_E_PARSE);
    CHECK(fwd_response_decode("11:0:a b", out, sizeof out) == FWD_E_PARSE);
    CHECK(fwd_response_decode("12:0:1:1:1:1", out, sizeof out) ==
          FWD_E_PARSE);
    /* an unknown protocol number is shown as a number, not rejected */
    CHECK(fwd_response_decode("12:0:1:1:1:9:0:1:0:2:0", out, sizeof out) > 0 &&
          strstr(out, "1   9 ") != NULL);
    CHECK(fwd_response_decode("12:0:1:1:1:1:0:99999:0:2:0", out,
                              sizeof out) == FWD_E_PARSE);
    CHECK(fwd_response_decode("14:0:1:1:0:200:0::0:254", out, sizeof out) ==
          FWD_E_PARSE);
    CHECK(fwd_response_decode("0:0", out, 3) == FWD_E_BUF);
    CHECK(fwd_response_decode(NULL, out, sizeof out) == FWD_E_INVAL);

    /* what the executor really produces decodes */
    ctx = fwd_create(NULL);
    CHECK(fwd_execute(ctx, "12", resp, sizeof resp) > 0);
    DECODES_TO(resp, "no entries\n");
    CHECK(fwd_execute(ctx, "2:42", resp, sizeof resp) > 0);
    CHECK(fwd_response_decode(resp, out, sizeof out) > 0 &&
          strncmp(out, "fwd-del: error 5 (no such entry)", 32) == 0);
    fwd_destroy(ctx);
}

/* ---------------------------------------------------------------- */
/* UDP control channel, with an authenticating second instance       */
/* ---------------------------------------------------------------- */
static void test_ctl(void)
{
    fwd_ctx *open_ctx = fwd_create(NULL), *tok_ctx;
    fwd_config cfg;
    fwd_addr lo;
    unsigned p1 = free_port(AF_INET, SOCK_DGRAM, "127.0.0.1");
    unsigned p2 = free_port(AF_INET, SOCK_DGRAM, "127.0.0.1");
    pid_t pid;
    int status = -1, i;

    fwd_config_default(&cfg);
    snprintf(cfg.ctl_token, sizeof cfg.ctl_token, "s3cret");
    tok_ctx = fwd_create(&cfg);
    fwd_addr_from_string(&lo, "127.0.0.1");
    CHECK(fwd_ctl_listen(open_ctx, &lo, (uint16_t)p1) == FWD_OK);
    CHECK(fwd_ctl_listen(tok_ctx, &lo, (uint16_t)p2) == FWD_OK);
    CHECK(fwd_ctl_listen(open_ctx, &lo, (uint16_t)p1) == FWD_E_EXIST);

    pid = fork();
    if (pid == 0) {
        char resp[FWD_MAX_RESP + 1];
        fwd_response r;
        int bad = 0;
        fwd_addr a;
        fwd_addr_from_string(&a, "127.0.0.1");

#define EXPECT(cond)                                                          \
    do {                                                                      \
        if (!(cond)) {                                                        \
            fprintf(stderr, "child FAIL line %d: %s (resp '%s')\n", __LINE__, \
                    #cond, resp);                                             \
            bad = 1;                                                          \
        }                                                                     \
    } while (0)

        EXPECT(fwd_client_request(&a, (uint16_t)p1, "0", resp, sizeof resp,
                                  2000) > 0);
        EXPECT(strcmp(resp, "0:0") == 0);
        fwd_client_request(&a, (uint16_t)p1, "77:1", resp, sizeof resp, 2000);
        EXPECT(fwd_response_parse(resp, &r) == FWD_OK && r.status != 0);
        fwd_client_request(&a, (uint16_t)p1, "12", resp, sizeof resp, 2000);
        EXPECT(strncmp(resp, "12:0:", 5) == 0);

        /* token protected instance */
        fwd_client_request(&a, (uint16_t)p2, "0", resp, sizeof resp, 2000);
        EXPECT(fwd_response_parse(resp, &r) == FWD_OK &&
               r.status == -FWD_E_AUTH);
        fwd_client_request(&a, (uint16_t)p2, "wrong@0", resp, sizeof resp,
                           2000);
        EXPECT(fwd_response_parse(resp, &r) == FWD_OK &&
               r.status == -FWD_E_AUTH);
        fwd_client_request(&a, (uint16_t)p2, "s3cret@0", resp, sizeof resp,
                           2000);
        EXPECT(strcmp(resp, "0:0") == 0);
        _exit(bad);
    }
    for (i = 0; i < 3000; i++) {
        fwd_run(open_ctx, 1);
        fwd_run(tok_ctx, 1);
        if (waitpid(pid, &status, WNOHANG) == pid)
            break;
    }
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    fwd_destroy(open_ctx);
    fwd_destroy(tok_ctx);
}

/* ---------------------------------------------------------------- */
/* QUIC control channel: self-signed certificate, the trust options  */
/* ---------------------------------------------------------------- */
#if FWD_ENABLE_QUIC
static void test_quic(void)
{
    char dir[] = "/tmp/fwdtest.XXXXXX", cert[64], key[64], fp[FWD_FINGERPRINT_LEN];
    fwd_ctx *ctx;
    fwd_config cfg;
    fwd_addr lo;
    unsigned qp = free_port(AF_INET, SOCK_DGRAM, "127.0.0.1");
    unsigned up = free_port(AF_INET, SOCK_DGRAM, "127.0.0.1");
    pid_t pid;
    int status = -1, i;

    if (!mkdtemp(dir)) {
        CHECK(!"mkdtemp");
        return;
    }
    snprintf(cert, sizeof cert, "%s/cert.pem", dir);
    snprintf(key, sizeof key, "%s/key.pem", dir);
    fwd_addr_from_string(&lo, "127.0.0.1");
    CHECK(fwd_tls_make_selfsigned(cert, key, &lo, "fwd.test", 1) == FWD_OK);
    CHECK(fwd_tls_fingerprint(cert, fp, sizeof fp) == FWD_OK);
    CHECK(strlen(fp) == 95 && fp[2] == ':');
    CHECK(fwd_tls_fingerprint(cert, fp, 10) == FWD_E_BUF);
    CHECK(fwd_tls_fingerprint(key, fp, sizeof fp) == FWD_E_TLS);

    fwd_config_default(&cfg);
    snprintf(cfg.ctl_token, sizeof cfg.ctl_token, "s3cret");
    ctx = fwd_create(&cfg);
    CHECK(fwd_ctl_listen_quic(ctx, &lo, (uint16_t)qp, key, key) == FWD_E_TLS);
    CHECK(fwd_ctl_listen_quic(ctx, &lo, (uint16_t)qp, cert, key) == FWD_OK);
    CHECK(fwd_ctl_listen_quic(ctx, &lo, (uint16_t)qp, cert, key) ==
          FWD_E_EXIST);
    CHECK(fwd_ctl_listen(ctx, &lo, (uint16_t)up) == FWD_OK);

    pid = fork();
    if (pid == 0) {
        char resp[FWD_MAX_RESP + 1], err[256];
        char badfp[FWD_FINGERPRINT_LEN];
        fwd_tls_trust t;
        fwd_response r;
        int bad = 0, n;

        resp[0] = '\0';
#define QREQ(trust, cmd)                                                      \
    fwd_client_request_quic(&lo, (uint16_t)qp, (trust), (cmd), resp,          \
                            sizeof resp, 5000, err, sizeof err)

        /* pin only: self-signed cert accepted as is */
        memset(&t, 0, sizeof t);
        t.pin_sha256 = fp;
        n = QREQ(&t, "s3cret@0");
        EXPECT(n > 0 && strcmp(resp, "0:0") == 0);
        /* pin also accepted without colons and in lower case */
        {
            char plain[65];
            int j, k = 0;
            for (j = 0; fp[j]; j++)
                if (fp[j] != ':')
                    plain[k++] = (char)(fp[j] | 0x20);
            plain[k] = '\0';
            t.pin_sha256 = plain;
            EXPECT(QREQ(&t, "s3cret@0") > 0 && strcmp(resp, "0:0") == 0);
        }
        /* wrong pin */
        snprintf(badfp, sizeof badfp, "%s", fp);
        badfp[0] = badfp[0] == 'A' ? 'B' : 'A';
        t.pin_sha256 = badfp;
        EXPECT(QREQ(&t, "s3cret@0") == FWD_E_TLS);
        t.pin_sha256 = "12:34";
        EXPECT(QREQ(&t, "s3cret@0") == FWD_E_INVAL);

        /* the certificate file as trust anchor, IP and name checks */
        memset(&t, 0, sizeof t);
        t.ca_file = cert;
        EXPECT(QREQ(&t, "s3cret@12") > 0 && strncmp(resp, "12:0:", 5) == 0);
        t.server_name = "fwd.test";
        EXPECT(QREQ(&t, "s3cret@0") > 0 && strcmp(resp, "0:0") == 0);
        t.server_name = "other.test";
        EXPECT(QREQ(&t, "s3cret@0") == FWD_E_TLS);
        /* CA + pin: both must pass */
        t.server_name = NULL;
        t.pin_sha256 = badfp;
        EXPECT(QREQ(&t, "s3cret@0") == FWD_E_TLS);
        t.pin_sha256 = fp;
        EXPECT(QREQ(&t, "s3cret@0") > 0 && strcmp(resp, "0:0") == 0);

        /* system trust store does not know a self-signed certificate */
        EXPECT(QREQ(NULL, "s3cret@0") == FWD_E_TLS);

        /* token still applies */
        memset(&t, 0, sizeof t);
        t.pin_sha256 = fp;
        QREQ(&t, "wrong@0");
        EXPECT(fwd_response_parse(resp, &r) == FWD_OK &&
               r.status == -FWD_E_AUTH);
        /* a command over the limit is answered, not dropped */
        {
            char big[FWD_MAX_CMD + 40];
            memset(big, '1', sizeof big - 1);
            big[sizeof big - 1] = '\0';
            QREQ(&t, big);
            EXPECT(fwd_response_parse(resp, &r) == FWD_OK &&
                   r.status == -FWD_E_PARSE);
        }

        /* UDP keeps working next to QUIC */
        n = fwd_client_request(&lo, (uint16_t)up, "s3cret@0", resp,
                               sizeof resp, 2000);
        EXPECT(n > 0 && strcmp(resp, "0:0") == 0);
#undef QREQ
        _exit(bad);
    }
    for (i = 0; i < 6000; i++) {
        fwd_run(ctx, 5);
        if (waitpid(pid, &status, WNOHANG) == pid)
            break;
    }
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    fwd_destroy(ctx);
    unlink(cert);
    unlink(key);
    rmdir(dir);
}
#endif

/* ---------------------------------------------------------------- */
int main(void)
{
    fwd_ctx *ctx;
    int v6 = 0;
    int fd;

    signal(SIGPIPE, SIG_IGN);
    ctx = fwd_create(NULL);
    if (!ctx)
        return 2;

    test_addr();
    test_codec();
    test_decode();
    test_forward_lifecycle(ctx);
    test_errors(ctx);
    test_tcp(ctx, AF_INET, "127.0.0.1");
    fd = bound_socket(AF_INET6, SOCK_STREAM, "::1", 0, 0);
    if (fd >= 0) {
        close(fd);
        v6 = 1;
    }
    if (v6)
        test_tcp(ctx, AF_INET6, "::1");
    else
        fprintf(stderr, "skip IPv6 test: no ::1\n");
    test_udp(ctx);
    fwd_destroy(ctx);
#if FWD_ENABLE_TUNTAP
    test_tun_listen();
#if FWD_ENABLE_QUIC
    test_tun_quic();
#endif
#endif
    test_ctl();
#if FWD_ENABLE_QUIC
    test_quic();
#else
    {
        fwd_ctx *c = fwd_create(NULL);
        fwd_addr lo;
        fwd_addr_from_string(&lo, "127.0.0.1");
        CHECK(fwd_ctl_listen_quic(c, &lo, 1, "c", "k") == FWD_E_NOSYS);
        fwd_destroy(c);
        fprintf(stderr, "skip QUIC test: built without FWD_ENABLE_QUIC\n");
    }
#endif

    printf("%d checks passed, %d failed\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
