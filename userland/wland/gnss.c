/*
 * gnss: the modem's GNSS engine (QMI LOC, service 0x10) for iOS's Location Services (ios-sweet iOS 27, 2026-11-19,
 * Claude). Message ids and TLVs as ios-redfin's sweetlocd (phone/locd: pdsemu.h, loc.c, englock.h), which follows
 * Qualcomm's location_service_v02.
 *
 * Switch file /var/local/gnss.on (re-read every 10 s; absent = no LOC traffic at all). Words in it:
 *   interval=MS   fix interval (default 1000, 100..60000)
 *   unlock        (owner-approved 2026-11-19) the engine lock: see below. unlock=all: also open network-initiated.
 * While gnss.on exists and the modem's LOC server is known: INFORM_CLIENT_REVISION 2, GET_ENGINE_LOCK for the DD and
 * DV subscriptions (logged), [unlock], REG_EVENTS (position reports), START a periodic session. Every good fix
 * (status 0 with latitude and longitude) goes to /var/local/gnss.fix (tmp + rename, 0600), one line:
 *     t=<unix ms> lat=<deg> lon=<deg> acc=<m> alt=<m> spd=<m/s> hdg=<deg> n=<fix count>
 * (missing values -1; alt -9999). jb/sweet-gnssfeed hands it to locationd. When gnss.on goes: STOP, REG_EVENTS 0
 * (and the engine lock restore).
 *
 * Engine lock (englock.h of ios-redfin; lock MASK bit 0 = MI/AFW: our own sessions, bit 1 = NI/NFW: network-initiated
 * incl. E911/SUPL-MT, bit 2 = privileged). On this modem START failed with QMI error 0x2e and session status 7
 * ENGINE_LOCKED (outputs/cell4c2). With the word "unlock": the DD and DV values are written to
 * /var/local/gnss.lock-orig (fsync'd) BEFORE any change, then each subscription whose value has the MI bit gets
 * SET_ENGINE_LOCK (lockType = its value without the MI bit; "unlock=all": lockType NONE) for that subscription only,
 * and GET again to check. Restore (SET each subscription back to its original, GET to check, then the file is removed)
 * when the word goes or gnss.on goes. wland has no exit hook, so a wland that dies while unlocked leaves the file:
 * when it exists and the word is not set, the originals are put back as soon as the LOC server is known (next start).
 * Never sent: any other setting (operation mode, NV, SUPL/NI config, injects). One request in flight, 5 s timeout.
 * Coordinates in the log are rounded to 0.01 deg.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include "link.h"
#include "qmi.h"
#include "gnss.h"

#define LOC_INFORM_CLIENT_REV	0x0020
#define LOC_REG_EVENTS		0x0021
#define LOC_START		0x0022
#define LOC_STOP		0x0023
#define LOC_IND_POSITION	0x0024
#define LOC_SET_ENGINE_LOCK	0x003a
#define LOC_GET_ENGINE_LOCK	0x003b
#define LOC_EVMASK		0x101ull	/* POSITION_REPORT | FIX_SESSION_STATE */
#define SESSION_ID		1
#define LOCK_ORIG		"/var/local/gnss.lock-orig"
#define SUB_DV			1
#define SUB_DD			2
#define now_ms			link_ms

static uint32_t srv_node, srv_port;
static int srv_up, want, want_unlock, running, failed, restart;	/* want_unlock: 0 no, 1 MI, 2 all */
static uint32_t interval_ms = 1000;
static uint64_t next_check;
static unsigned nfix, ngood;
static uint16_t txn;

/* one request in flight; lock requests also wait for their indication */
static struct {
	int busy, ind;		/* ind: the response came, waiting for the indication of id */
	uint16_t txn, id;
	uint64_t at;
} cur;

/* engine lock state */
static int lk_known[3];			/* [SUB_DV], [SUB_DD] */
static uint64_t lk_val[3], lk_orig[3];
static int lk_have_orig, lk_changed, lk_fail;
static unsigned lk_sub;			/* the subscription of the request in flight */

enum {
	ST_IDLE, ST_REV, ST_GET_DD, ST_GET_DV, ST_UNLOCK, ST_REG, ST_START, ST_RUN,	/* bring-up */
	ST_STOP, ST_UNREG, ST_RESTORE,							/* shutdown */
};
static int st;
static int lk_phase;		/* ST_UNLOCK / ST_RESTORE: 0 SET DD, 1 GET DD, 2 SET DV, 3 GET DV, 4 done */

static uint16_t tlv(uint8_t *b, uint16_t o, uint8_t t, const void *v, uint16_t l)
{
	b[o] = t;
	b[o + 1] = (uint8_t)l;
	b[o + 2] = (uint8_t)(l >> 8);
	memcpy(b + o + 3, v, l);
	return (uint16_t)(o + 3 + l);
}

static int send_req(uint16_t id, const void *tl, uint16_t tn)
{
	struct qmi_buf q;
	qmi_start(&q, QMI_REQUEST, ++txn, id);
	if (tn)
		memcpy(q.b + q.n, tl, tn), q.n += tn;
	size_t n = qmi_finish(&q);
	cur.busy = 1;
	cur.ind = 0;
	cur.txn = txn;
	cur.id = id;
	cur.at = now_ms();
	return qrtr_send(QRTR_TYPE_DATA, GNSS_PORT, srv_node, srv_port, q.b, (uint32_t)n);
}

static const char *lk_str(uint64_t m, char *b, size_t n)
{
	snprintf(b, n, "0x%llx (own sessions %s, network-initiated %s%s)", (unsigned long long)m, (m & 1) ? "LOCKED" : "open",
		 (m & 2) ? "locked" : "open", (m & 4) ? ", privileged locked" : "");
	return b;
}

static uint32_t type_of(uint64_t m)
{
	static const uint32_t t[4] = { 1, 2, 3, 4 };	/* NONE, MI, MT, ALL */
	return t[m & 3];
}

static void lk_get(unsigned sub)
{
	uint8_t t[16];
	uint32_t s = sub;
	lk_sub = sub;
	send_req(LOC_GET_ENGINE_LOCK, t, tlv(t, 0, 0x10, &s, 4));
}

static void lk_set(unsigned sub, uint64_t m, const char *why)
{
	uint8_t t[48];
	uint32_t ty = type_of(m), s = sub;
	uint16_t n = tlv(t, 0, 0x01, &ty, 4);
	char a[96], b[96];
	n = tlv(t, n, 0x10, &s, 4);
	if (m & ~3ull)
		n = tlv(t, n, 0x11, &m, 8);	/* lockClient: only when bits beyond MI/NI must be kept */
	printf("[%6llu] gnss: ENGINE LOCK %s: %s subscription %s -> %s\n", (unsigned long long)now_ms(), why,
	       sub == SUB_DD ? "DD" : "DV", lk_known[sub] ? lk_str(lk_val[sub], a, sizeof(a)) : "?", lk_str(m, b, sizeof(b)));
	lk_sub = sub;
	lk_changed = 1;
	send_req(LOC_SET_ENGINE_LOCK, t, n);
}

static int lk_save(void)
{
	char line[96];
	int fd = open(LOCK_ORIG ".tmp", O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0)
		return -1;
	int n = snprintf(line, sizeof(line), "dd=0x%llx dv=0x%llx\n", (unsigned long long)lk_orig[SUB_DD],
			 (unsigned long long)lk_orig[SUB_DV]);
	if (write(fd, line, (size_t)n) != n || fsync(fd)) {
		close(fd);
		unlink(LOCK_ORIG ".tmp");
		return -1;
	}
	close(fd);
	return rename(LOCK_ORIG ".tmp", LOCK_ORIG);
}

static int lk_load(void)
{
	unsigned long long dd, dv;
	FILE *f = fopen(LOCK_ORIG, "r");
	if (!f)
		return -1;
	int r = fscanf(f, "dd=%llx dv=%llx", &dd, &dv);
	fclose(f);
	if (r != 2)
		return -2;
	lk_orig[SUB_DD] = dd;
	lk_orig[SUB_DV] = dv;
	lk_have_orig = 1;
	return 0;
}

/* the target of a subscription while unlocked */
static uint64_t lk_target(unsigned sub)
{
	return want_unlock == 2 ? (lk_orig[sub] & ~3ull) : (lk_orig[sub] & ~1ull);
}

/* ST_UNLOCK / ST_RESTORE: SET DD, GET DD, SET DV, GET DV. Returns 1 while a request went out. */
static int lk_steps(int restore)
{
	while (lk_phase < 4) {
		unsigned sub = lk_phase < 2 ? SUB_DD : SUB_DV;
		uint64_t goal = restore ? lk_orig[sub] : lk_target(sub);
		if ((lk_phase & 1) == 0) {
			if (lk_known[sub] && lk_val[sub] == goal) {
				lk_phase += 2;		/* already there: no SET, no check */
				continue;
			}
			lk_phase++;
			lk_set(sub, goal, restore ? "restore" : "unlock");
			return 1;
		}
		lk_phase++;
		lk_known[sub] = 0;
		lk_get(sub);
		return 1;
	}
	return 0;
}

static void lk_restored(void)
{
	char a[96], b[96];
	int ok = lk_known[SUB_DD] && lk_known[SUB_DV] && lk_val[SUB_DD] == lk_orig[SUB_DD] &&
		 lk_val[SUB_DV] == lk_orig[SUB_DV];
	if (ok) {
		unlink(LOCK_ORIG);
		printf("[%6llu] gnss: ENGINE LOCK restored (DD %s, DV %s); %s removed\n", (unsigned long long)now_ms(),
		       lk_str(lk_orig[SUB_DD], a, sizeof(a)), lk_str(lk_orig[SUB_DV], b, sizeof(b)), LOCK_ORIG);
		lk_have_orig = 0;
		lk_changed = 0;
	} else {
		printf("[%6llu] gnss: ENGINE LOCK RESTORE NOT CONFIRMED: %s kept, tried again when wland next starts\n",
		       (unsigned long long)now_ms(), LOCK_ORIG);
		lk_fail = 1;
	}
}

static int lk_wanted_restore(void)
{
	return lk_have_orig && !lk_fail && (!want || !want_unlock);
}

/* the next step of the bring-up / shutdown */
static void step(void)
{
	uint8_t t[64];
	uint16_t n = 0;
	char a[96], b[96];
	if (cur.busy || !srv_up)
		return;
	if (st == ST_RESTORE) {
		if (!lk_steps(1)) {
			lk_restored();
			st = ST_IDLE;
		}
		return;
	}
	/* restore wanted (word gone, or crash repair): after the session is down */
	if (lk_wanted_restore() && (st == ST_IDLE || st == ST_GET_DD || st == ST_GET_DV)) {
		st = ST_RESTORE;
		lk_phase = 0;
		lk_steps(1);
		return;
	}
	if (want && !failed) {
		switch (st) {
		case ST_IDLE: {
			uint32_t rev = 2;
			n = tlv(t, 0, 0x01, &rev, 4);
			st = ST_REV;
			send_req(LOC_INFORM_CLIENT_REV, t, n);
			return;
		}
		case ST_REV:
			st = ST_GET_DD;
			lk_get(SUB_DD);
			return;
		case ST_GET_DD:
			st = ST_GET_DV;
			lk_get(SUB_DV);
			return;
		case ST_GET_DV:
			printf("[%6llu] gnss: engine lock: DD %s, DV %s\n", (unsigned long long)now_ms(),
			       lk_known[SUB_DD] ? lk_str(lk_val[SUB_DD], a, sizeof(a)) : "unknown",
			       lk_known[SUB_DV] ? lk_str(lk_val[SUB_DV], b, sizeof(b)) : "unknown");
			if (want_unlock && !lk_fail) {
				if (!lk_have_orig) {
					if (!lk_known[SUB_DD] || !lk_known[SUB_DV]) {
						printf("[%6llu] gnss: engine lock unknown for a subscription: not unlocking\n",
						       (unsigned long long)now_ms());
						lk_fail = 1;
						st = ST_UNLOCK;
						step();
						return;
					}
					lk_orig[SUB_DD] = lk_val[SUB_DD];
					lk_orig[SUB_DV] = lk_val[SUB_DV];
					if (lk_save()) {
						printf("[%6llu] gnss: could not save %s (%s): not unlocking\n",
						       (unsigned long long)now_ms(), LOCK_ORIG, strerror(errno));
						lk_fail = 1;
						st = ST_UNLOCK;
						step();
						return;
					}
					lk_have_orig = 1;
					printf("[%6llu] gnss: ENGINE LOCK originals saved to %s\n", (unsigned long long)now_ms(),
					       LOCK_ORIG);
				}
				if (lk_target(SUB_DD) == lk_val[SUB_DD] && lk_target(SUB_DV) == lk_val[SUB_DV])
					printf("[%6llu] gnss: engine lock already as wanted%s\n", (unsigned long long)now_ms(),
					       want_unlock == 1 ? " (own sessions open on both: if START still fails, try unlock=all)" : "");
				st = ST_UNLOCK;
				lk_phase = 0;
				if (lk_steps(0))
					return;
			} else {
				st = ST_UNLOCK;
			}
			/* fall through to REG */
			step();
			return;
		case ST_UNLOCK: {
			if (want_unlock && !lk_fail && lk_steps(0))
				return;
			if (want_unlock && !lk_fail)
				printf("[%6llu] gnss: engine lock now DD %s, DV %s\n", (unsigned long long)now_ms(),
				       lk_known[SUB_DD] ? lk_str(lk_val[SUB_DD], a, sizeof(a)) : "unknown",
				       lk_known[SUB_DV] ? lk_str(lk_val[SUB_DV], b, sizeof(b)) : "unknown");
			uint64_t mask = LOC_EVMASK;
			n = tlv(t, 0, 0x01, &mask, 8);
			st = ST_REG;
			send_req(LOC_REG_EVENTS, t, n);
			return;
		}
		case ST_REG: {
			uint8_t id = SESSION_ID;
			uint32_t rec = 1 /* periodic */, inter = 2 /* intermediate reports off */;
			n = tlv(t, 0, 0x01, &id, 1);
			n = tlv(t, n, 0x10, &rec, 4);
			n = tlv(t, n, 0x12, &inter, 4);
			n = tlv(t, n, 0x13, &interval_ms, 4);
			st = ST_START;
			send_req(LOC_START, t, n);
			printf("[%6llu] gnss: starting a session (interval %u ms)\n", (unsigned long long)now_ms(), interval_ms);
			return;
		}
		default:
			return;
		}
	}
	if ((!want || failed) && (st == ST_RUN || st == ST_START || st == ST_REG)) {
		uint8_t id = SESSION_ID;
		n = tlv(t, 0, 0x01, &id, 1);
		st = ST_STOP;
		send_req(LOC_STOP, t, n);
		return;
	}
	if ((!want || failed) && st == ST_STOP) {
		uint64_t mask = 0;
		n = tlv(t, 0, 0x01, &mask, 8);
		st = ST_UNREG;
		send_req(LOC_REG_EVENTS, t, n);
		return;
	}
	if ((!want || failed) && st == ST_UNREG) {
		st = ST_IDLE;
		running = 0;
		printf("[%6llu] gnss: session stopped (%u reports, %u fixes)\n", (unsigned long long)now_ms(), nfix, ngood);
		if (restart) {
			restart = 0;
			failed = 0;		/* settings changed: up again (after the lock restore, if wanted) */
		}
		step();			/* the lock restore, if wanted */
		return;
	}
	if (!want && st != ST_IDLE && st != ST_RESTORE)
		st = ST_IDLE;
}

static void read_on(void)
{
	char b[256] = "";
	FILE *f = fopen(GNSS_ON, "r");
	int w = f != NULL, u = 0;
	uint32_t iv = 1000;
	if (f) {
		size_t k = fread(b, 1, sizeof(b) - 1, f);
		b[k] = 0;
		fclose(f);
		const char *p = strstr(b, "interval=");
		if (p)
			iv = (uint32_t)strtoul(p + 9, NULL, 10);
		if (iv < 100 || iv > 60000)
			iv = 1000;
		if (strstr(b, "unlock=all"))
			u = 2;
		else if (strstr(b, "unlock"))
			u = 1;
	}
	if (w != want || iv != interval_ms || u != want_unlock) {
		printf("[%6llu] gnss: %s %s (interval %u ms%s)%s\n", (unsigned long long)now_ms(), GNSS_ON,
		       w ? "present" : "gone", iv, u == 2 ? ", unlock=all" : u ? ", unlock" : "",
		       failed && w ? ": trying again after an earlier failure" : "");
		if (w && running && (iv != interval_ms || u != want_unlock)) {
			failed = 1;		/* stop, then come up again with the new settings */
			restart = 1;
		} else {
			failed = 0;
		}
		lk_fail = 0;
	}
	want = w;
	want_unlock = w ? u : 0;
	interval_ms = iv;
}

void gnss_new_server(uint32_t node, uint32_t port)
{
	if (srv_up && node == srv_node && port == srv_port)
		return;
	srv_node = node;
	srv_port = port;
	srv_up = 1;
	st = ST_IDLE;
	cur.busy = 0;
	printf("[%6llu] gnss: LOC server at %u:%x\n", (unsigned long long)now_ms(), node, port);
	int r = lk_load();
	if (r == 0)
		printf("[%6llu] gnss: %s exists: an earlier unlock (originals DD 0x%llx, DV 0x%llx)\n",
		       (unsigned long long)now_ms(), LOCK_ORIG, (unsigned long long)lk_orig[SUB_DD],
		       (unsigned long long)lk_orig[SUB_DV]);
	else if (r == -2) {
		printf("[%6llu] gnss: %s exists but is unreadable: not touching the engine lock\n",
		       (unsigned long long)now_ms(), LOCK_ORIG);
		lk_fail = 1;
	}
	read_on();
	step();
}

void gnss_poll(void)
{
	uint64_t now = now_ms();
	if (now >= next_check) {
		next_check = now + 10000;
		read_on();
		if (!want && st == ST_RUN)
			step();
	}
	if (cur.busy && now - cur.at > 5000) {
		printf("[%6llu] gnss: request 0x%04x: no %s in 5 s\n", (unsigned long long)now, cur.id,
		       cur.ind ? "indication" : "answer");
		cur.busy = 0;
		if (cur.id == LOC_SET_ENGINE_LOCK || cur.id == LOC_GET_ENGINE_LOCK) {
			if (st == ST_RESTORE) {
				lk_phase = 4;	/* give up this round; lk_restored() reports it */
			} else if (st == ST_UNLOCK) {
				lk_fail = 1;
			}
		} else if (cur.id == LOC_START || cur.id == LOC_REG_EVENTS) {
			failed = 1;
		}
	}
	step();
}

static void write_fix(const struct qmi_msg *m)
{
	uint16_t l;
	const uint8_t *v;
	double lat, lon;
	float unc = -1, alt = -9999, spd = -1, hdg = -1;
	uint64_t utc = 0;
	if (!(v = qmi_get(m, 0x10, &l)) || l != 8)
		return;
	memcpy(&lat, v, 8);
	if (!(v = qmi_get(m, 0x11, &l)) || l != 8)
		return;
	memcpy(&lon, v, 8);
	if ((v = qmi_get(m, 0x12, &l)) && l == 4)
		memcpy(&unc, v, 4);
	if ((v = qmi_get(m, 0x1b, &l)) && l == 4)
		memcpy(&alt, v, 4);		/* altitude above mean sea level */
	else if ((v = qmi_get(m, 0x1a, &l)) && l == 4)
		memcpy(&alt, v, 4);		/* else above the ellipsoid */
	if ((v = qmi_get(m, 0x18, &l)) && l == 4)
		memcpy(&spd, v, 4);
	if ((v = qmi_get(m, 0x20, &l)) && l == 4)
		memcpy(&hdg, v, 4);
	if ((v = qmi_get(m, 0x25, &l)) && l == 8)
		memcpy(&utc, v, 8);
	if (!utc) {
		struct timespec ts;
		clock_gettime(CLOCK_REALTIME, &ts);
		utc = (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
	}
	ngood++;
	FILE *f = fopen(GNSS_FIX ".tmp", "w");
	if (!f)
		return;
	fprintf(f, "t=%llu lat=%.7f lon=%.7f acc=%.1f alt=%.1f spd=%.2f hdg=%.1f n=%u\n", (unsigned long long)utc, lat, lon,
		unc, alt, spd, hdg, ngood);
	fclose(f);
	chmod(GNSS_FIX ".tmp", 0600);
	rename(GNSS_FIX ".tmp", GNSS_FIX);
	if (ngood <= 3 || ngood % 60 == 0)
		printf("[%6llu] gnss: fix #%u lat %.2f lon %.2f +- %.0f m\n", (unsigned long long)now_ms(), ngood, lat, lon, unc);
}

static void lock_ind(const struct qmi_msg *m)
{
	uint16_t l = 0;
	const uint8_t *v;
	uint32_t status = 0xffffffff;
	if ((v = qmi_get(m, 0x01, &l)) && l == 4)
		memcpy(&status, v, 4);
	if (m->id == LOC_GET_ENGINE_LOCK) {
		uint64_t client = 0;
		uint32_t type = 0;
		int hc = 0, ht = 0;
		if ((v = qmi_get(m, 0x12, &l)) && l == 8)
			memcpy(&client, v, 8), hc = 1;
		if ((v = qmi_get(m, 0x10, &l)) && l == 4)
			memcpy(&type, v, 4), ht = 1;
		if (status == 0 && (hc || (ht && type >= 1 && type <= 4))) {
			static const uint64_t of_type[5] = { 0, 0, 1, 2, 3 };
			lk_val[lk_sub] = hc ? client : of_type[type];
			lk_known[lk_sub] = 1;
		} else {
			printf("[%6llu] gnss: GET_ENGINE_LOCK %s: status %u, no usable value (type %s%u, client %s0x%llx)\n",
			       (unsigned long long)now_ms(), lk_sub == SUB_DD ? "DD" : "DV", status, ht ? "" : "absent ", type,
			       hc ? "" : "absent ", (unsigned long long)client);
		}
	} else if (status != 0) {
		printf("[%6llu] gnss: SET_ENGINE_LOCK %s: status %u\n", (unsigned long long)now_ms(),
		       lk_sub == SUB_DD ? "DD" : "DV", status);
	}
}

void gnss_handle(const struct qrtr_hdr *h, const uint8_t *d, uint32_t n)
{
	struct qmi_msg m;
	uint16_t l = 0;
	const uint8_t *v;
	(void)h;
	if (qmi_parse(d, n, &m))
		return;
	if (m.type == QMI_RESPONSE) {
		if (!cur.busy || m.txn != cur.txn)
			return;
		const uint8_t *r = qmi_get(&m, 0x02, &l);
		unsigned res = r && l >= 4 ? (unsigned)(r[0] | r[1] << 8) : 1, err = r && l >= 4 ? (unsigned)(r[2] | r[3] << 8) : 0xffff;
		if (res) {
			cur.busy = 0;
			printf("[%6llu] gnss: request 0x%04x failed (QMI error 0x%x)\n", (unsigned long long)now_ms(), m.id, err);
			if (m.id == LOC_START || m.id == LOC_REG_EVENTS) {
				failed = 1;
				printf("[%6llu] gnss: not retried until %s changes\n", (unsigned long long)now_ms(), GNSS_ON);
			} else if (m.id == LOC_SET_ENGINE_LOCK || m.id == LOC_GET_ENGINE_LOCK) {
				if (st == ST_RESTORE)
					lk_phase = 4;
				else if (st == ST_UNLOCK)
					lk_fail = 1;
			}
		} else if (m.id == LOC_SET_ENGINE_LOCK || m.id == LOC_GET_ENGINE_LOCK) {
			cur.ind = 1;		/* the value / status comes in the indication */
			cur.at = now_ms();
			return;
		} else {
			cur.busy = 0;
			if (m.id == LOC_START) {
				st = ST_RUN;
				running = 1;
				printf("[%6llu] gnss: session running\n", (unsigned long long)now_ms());
			}
		}
		step();
		return;
	}
	if (m.type != QMI_INDICATION)
		return;
	if (m.id == LOC_GET_ENGINE_LOCK || m.id == LOC_SET_ENGINE_LOCK) {
		lock_ind(&m);
		if (cur.busy && cur.id == m.id) {
			cur.busy = 0;
			step();
		}
		return;
	}
	if (m.id == LOC_IND_POSITION) {
		uint32_t status = 0xff;
		nfix++;
		if ((v = qmi_get(&m, 0x01, &l)) && l == 4)
			memcpy(&status, v, 4);
		if (status == 0) {
			write_fix(&m);
		} else if (nfix <= 5 || nfix % 60 == 0) {
			printf("[%6llu] gnss: report #%u: session status %u (0 fix, 1 in progress, 7 engine locked)\n",
			       (unsigned long long)now_ms(), nfix, status);
		}
		return;
	}
}
