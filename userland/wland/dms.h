#ifndef DMS_H
#define DMS_H
#include <stdint.h>
#include "link.h"

#define DMS_SERVICE	0x02		/* QMI DMS (device management) on the modem */
#define DMS_PORT	0x13		/* our client port */

void dms_new_server(uint32_t node, uint32_t port);
void dms_handle(const struct qrtr_hdr *h, const uint8_t *d, uint32_t n);

#endif
