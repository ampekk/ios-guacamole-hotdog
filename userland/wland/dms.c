/*
 * DMS (QMI service 0x02): ask the modem for the phone's IMEI once its device-management service is up, and keep
 * /var/local/cell/imei in step with it (ios-sweet iOS 27 cell3, 2026-11-19, Claude).
 *
 * The kernel's baseband stand-in (xnu bsd/dev/sweet_abp.c, cell3) hands that file's IMEI to CommCenter when it asks
 * (DMS 0x0054 per subscription). Only slot 1 (the modem's default subscription): one SIM slot is supported.
 * QMI_DMS_GET_DEVICE_SERIAL_NUMBERS (0x0025), no TLVs; the answer: 0x10 ESN, 0x11 IMEI, 0x12 MEID, 0x13 IMEI SV, all
 * strings (libqmi qmi-service-dms.json); a 1-byte SV is accepted too. The request only reads; nothing is written to the modem. The IMEI is never printed.
 * The file is rewritten (tmp + rename, 0600) only when its content would change.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "link.h"
#include "qmi.h"
#include "dms.h"

#define DMS_GET_SERIALS	0x0025
#define IMEI_DIR	"/var/local/cell"
#define IMEI_FILE	IMEI_DIR "/imei"
#define DMS_TRIES	5

static uint32_t srv_node, srv_port;
static uint16_t txn;
static int tries, done;

static void ask(void)
{
	struct qmi_buf q;
	if (done || tries >= DMS_TRIES)
		return;
	tries++;
	qmi_start(&q, QMI_REQUEST, ++txn, DMS_GET_SERIALS);
	size_t n = qmi_finish(&q);
	if (qrtr_send(QRTR_TYPE_DATA, DMS_PORT, srv_node, srv_port, q.b, (uint32_t)n))
		printf("dms: request %d not sent\n", tries);
}

void dms_new_server(uint32_t node, uint32_t port)
{
	if (srv_port && done)
		return;			/* the first DMS server (instance) is enough */
	srv_node = node;
	srv_port = port;
	printf("dms: server at %u:%x: asking for the IMEI\n", node, port);
	ask();
}

static void store(const char *imei, int sv)
{
	char want[32], have[32] = "";
	int n = snprintf(want, sizeof(want), "%s\n", imei);
	if (sv >= 0 && sv < 100)
		n += snprintf(want + n, sizeof(want) - (size_t)n, "sv %02d\n", sv);
	FILE *f = fopen(IMEI_FILE, "r");
	if (f) {
		size_t r = fread(have, 1, sizeof(have) - 1, f);
		have[r] = 0;
		fclose(f);
	}
	if (!strcmp(have, want)) {
		printf("dms: IMEI unchanged (%s up to date)\n", IMEI_FILE);
		return;
	}
	mkdir(IMEI_DIR, 0755);
	f = fopen(IMEI_FILE ".tmp", "w");
	if (!f || fputs(want, f) < 0 || fclose(f) || chmod(IMEI_FILE ".tmp", 0600) || rename(IMEI_FILE ".tmp", IMEI_FILE)) {
		printf("dms: could not write %s: %s\n", IMEI_FILE, strerror(errno));
		return;
	}
	printf("dms: IMEI written to %s%s\n", IMEI_FILE, sv >= 0 ? " (with SV)" : "");
}

void dms_handle(const struct qrtr_hdr *h, const uint8_t *d, uint32_t n)
{
	struct qmi_msg m;
	uint16_t l = 0;
	if (qmi_parse(d, n, &m) || m.type != QMI_RESPONSE || m.id != DMS_GET_SERIALS || done)
		return;
	const uint8_t *r = qmi_get(&m, 0x02, &l);
	unsigned res = (r && l >= 4) ? (unsigned)(r[0] | (r[1] << 8)) : 1, err = (r && l >= 4) ? (unsigned)(r[2] | (r[3] << 8)) : 0;
	if (res != 0) {
		printf("dms: serial numbers request %d failed (error %#x)%s\n", tries, err, tries < DMS_TRIES ? ": asking again" : "");
		ask();
		return;
	}
	const uint8_t *v = qmi_get(&m, 0x11, &l);
	unsigned digits = 0;
	while (v && digits < l && v[digits] >= '0' && v[digits] <= '9')
		digits++;
	if (!v || l != 15 || digits != 15) {
		printf("dms: no 15-digit IMEI in the answer (TLV 0x11 %s, %u bytes)\n", v ? "present" : "absent", v ? l : 0);
		done = 1;
		return;
	}
	char imei[16];
	memcpy(imei, v, 15);
	imei[15] = 0;
	uint16_t svl = 0;
	const uint8_t *sv = qmi_get(&m, 0x13, &svl);
	int svn = -1;
	if (sv && svl == 2 && sv[0] >= '0' && sv[0] <= '9' && sv[1] >= '0' && sv[1] <= '9')
		svn = (sv[0] - '0') * 10 + (sv[1] - '0');
	else if (sv && svl == 1)
		svn = sv[0];
	done = 1;
	store(imei, svn);
}
