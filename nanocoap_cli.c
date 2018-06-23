/*
 * Copyright (c) 2015-2017 Ken Bannister. All rights reserved.
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     examples
 * @{
 *
 * @file
 * @brief       nanocoap CLI support
 *
 * @author      Ken Bannister <kb2ma@runbox.com>
 *
 * @}
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "net/nanocoap.h"
#include "net/sock/udp.h"
#include "od.h"
#include "fmt.h"
#include "xtimer.h"

#define ENABLE_DEBUG (0)
#include "debug.h"

/* From Koen Zandberg's PR #9086 */
ssize_t nanocoap_request(coap_pkt_t *pkt, sock_udp_ep_t *local, sock_udp_ep_t *remote, size_t len)
{
    ssize_t res;
    size_t pdu_len = (pkt->payload - (uint8_t *)pkt->hdr) + pkt->payload_len;
    uint8_t *buf = (uint8_t*)pkt->hdr;
    sock_udp_t sock;

    if (!remote->port) {
        remote->port = COAP_PORT;
    }

    res = sock_udp_create(&sock, local, remote, 0);
    if (res < 0) {
        return res;
    }

    /* TODO: timeout random between between ACK_TIMEOUT and (ACK_TIMEOUT *
     * ACK_RANDOM_FACTOR) */
    uint32_t timeout = COAP_ACK_TIMEOUT * (1000000U);
    int tries = COAP_MAX_RETRANSMIT + 1;
    while (tries--) {

        res = sock_udp_send(&sock, buf, pdu_len, NULL);
        if (res <= 0) {
            DEBUG("nanocoap: error sending coap request\n");
            goto out;
        }

        res = sock_udp_recv(&sock, buf, len, timeout, NULL);
        if (res <= 0) {
            if (res == -ETIMEDOUT) {
                DEBUG("nanocoap: timeout\n");

                timeout *= 2;
                continue;
            }
            DEBUG("nanocoap: error receiving coap request\n");
            break;
        }
        else {
            if (coap_parse(pkt, (uint8_t *)buf, res) < 0) {
                DEBUG("nanocoap: error parsing packet\n");
                res = -EBADMSG;
            }
            break;
        }
    }
    if (!tries) {
        DEBUG("nanocoap: maximum retries reached.\n");
        res = -ETIMEDOUT;
        goto out;
    }

out:
    sock_udp_close(&sock);

    return res;
}

static ssize_t _send(coap_pkt_t *pkt, size_t len, char *addr_str, char *port_str)
{
    ipv6_addr_t addr;
    sock_udp_ep_t remote;

    remote.family = AF_INET6;

    /* parse for interface */
    int iface = ipv6_addr_split_iface(addr_str);
    if (iface == -1) {
        if (gnrc_netif_numof() == 1) {
            /* assign the single interface found in gnrc_netif_numof() */
            remote.netif = (uint16_t)gnrc_netif_iter(NULL)->pid;
        }
        else {
            remote.netif = SOCK_ADDR_ANY_NETIF;
        }
    }
    else {
        if (gnrc_netif_get_by_pid(iface) == NULL) {
            puts("gcoap_cli: interface not valid");
            return 0;
        }
        remote.netif = iface;
    }

    /* parse destination address */
    if (ipv6_addr_from_str(&addr, addr_str) == NULL) {
        puts("gcoap_cli: unable to parse destination address");
        return 0;
    }
    if ((remote.netif == SOCK_ADDR_ANY_NETIF) && ipv6_addr_is_link_local(&addr)) {
        puts("gcoap_cli: must specify interface for link local target");
        return 0;
    }
    memcpy(&remote.addr.ipv6[0], &addr.u8[0], sizeof(addr.u8));

    /* parse port */
    remote.port = atoi(port_str);
    if (remote.port == 0) {
        puts("gcoap_cli: unable to parse destination port");
        return 0;
    }

    return nanocoap_request(pkt, NULL, &remote, len);
}

int nanocoap_cli_cmd(int argc, char **argv)
{
    /* Ordered like the RFC method code numbers, but off by 1. GET is code 0. */
    char *method_codes[] = {"get", "post", "put"};
    unsigned buflen = 128;
    uint8_t buf[buflen];
    coap_pkt_t pkt;
    size_t len;
    uint8_t token[2] = {0xDA, 0xEC};

    if (argc == 1) {
        /* show help for commands */
        goto end;
    }

    int code_pos = -1;
    for (size_t i = 0; i < sizeof(method_codes) / sizeof(char*); i++) {
        if (strcmp(argv[1], method_codes[i]) == 0) {
            code_pos = i;
        }
    }
    if (code_pos == -1) {
        goto end;
    }

    pkt.hdr = (coap_hdr_t *)buf;

    /* parse options */
    if (argc == 5 || argc == 6) {
        uint32_t elapsed = xtimer_now_usec();
        ssize_t hdrlen = coap_build_hdr(pkt.hdr, COAP_TYPE_CON, &token[0], 2,
                                        code_pos+1, 1);
        coap_pkt_init(&pkt, &buf[0], buflen, hdrlen);
        coap_opt_add_string(&pkt, COAP_OPT_URI_PATH, argv[4], '/');
        if (argc == 6) {
            coap_opt_add_uint(&pkt, COAP_OPT_CONTENT_FORMAT, COAP_FORMAT_TEXT);
            len = coap_opt_finish(&pkt, COAP_OPT_FINISH_PAYLOAD);

            pkt.payload_len = strlen(argv[5]);
            memcpy(pkt.payload, argv[5], pkt.payload_len);
            len += pkt.payload_len;
        }
        else {
            len = coap_opt_finish(&pkt, COAP_OPT_FINISH_NONE);
        }
        elapsed = xtimer_now_usec() - elapsed;

        printf("nanocoap_cli: sending msg ID %u, %u bytes, built in %" PRIu32 " usec\n",
                coap_get_id(&pkt), (unsigned) len, elapsed);

        ssize_t res = _send(&pkt, buflen, argv[2], argv[3]);
        if (res < 0) {
            printf("nanocoap_cli: msg send failed: %d\n", res);
        }
        else {
            char *class_str = (coap_get_code_class(&pkt) == COAP_CLASS_SUCCESS)
                                    ? "Success" : "Error";
            printf("nanocoap: response %s, code %1u.%02u", class_str,
                   coap_get_code_class(&pkt), coap_get_code_detail(&pkt));
            if (pkt.payload_len) {
                unsigned format = coap_get_content_type(&pkt);
                if (format == COAP_FORMAT_TEXT
                        || format == COAP_FORMAT_LINK
                        || coap_get_code_class(&pkt) == COAP_CLASS_CLIENT_FAILURE
                        || coap_get_code_class(&pkt) == COAP_CLASS_SERVER_FAILURE) {
                    /* Expecting diagnostic payload in failure cases */
                    printf(", %u bytes\n%.*s\n", pkt.payload_len, pkt.payload_len,
                                                                  (char *)pkt.payload);
                }
                else {
                    printf(", %u bytes\n", pkt.payload_len);
                    od_hex_dump(pkt.payload, pkt.payload_len, OD_WIDTH_DEFAULT);
                }
            }
            else {
                printf(", empty payload\n");
            }
        }
        return 0;
    }

    end:
    printf("usage: %s <get|post|put> <addr>[%%iface] <port> <path> [data]\n",
           argv[0]);
    return 1;
}
