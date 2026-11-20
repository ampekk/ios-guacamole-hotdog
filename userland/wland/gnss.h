#ifndef GNSS_H
#define GNSS_H
#include <stdint.h>
#include "link.h"

#define GNSS_SERVICE	0x10		/* QMI LOC: the modem's GNSS engine */
#define GNSS_PORT	0x14		/* our client port */
#define GNSS_ON		"/var/local/gnss.on"
#define GNSS_FIX	"/var/local/gnss.fix"

void gnss_new_server(uint32_t node, uint32_t port);
void gnss_handle(const struct qrtr_hdr *h, const uint8_t *d, uint32_t n);
void gnss_poll(void);		/* from wland's idle loop */

#endif
