/*
 * Sigma Control API DUT (station/AP)
 * Copyright (c) 2010-2011, Atheros Communications, Inc.
 * Copyright (c) 2011-2014, Qualcomm Atheros, Inc.
 * All Rights Reserved.
 * Licensed under the Clear BSD license. See README for more details.
 */

#include "sigma_dut.h"
#include <sys/stat.h>
#include "wpa_ctrl.h"
#include "wpa_helpers.h"


extern char *sigma_main_ifname;
extern char *sigma_station_ifname;
extern char *sigma_wpas_ctrl;


char * get_main_ifname(void)
{
	enum driver_type drv = get_driver_type();
	enum openwrt_driver_type openwrt_drv = get_openwrt_driver_type();

	if (sigma_main_ifname)
		return sigma_main_ifname;

      if (if_nametoindex("p2p0") > 0)
	      return "p2p0";
      if (if_nametoindex("wlan1") > 0) {
	      struct stat s;
	      if (stat("/sys/module/mac80211", &s) == 0 &&
		  if_nametoindex("wlan0")) {
		      /*
		       * Likely a dual-radio AP device; use wlan0 for STA/P2P
		       * operations.
		       */
		      return "wlan0";
	      }
	      return "wlan1";
      }
      if (if_nametoindex("wlan0") > 0)
	      return "wlan0";

      if (drv == DRIVER_ATHEROS || openwrt_drv == OPENWRT_DRIVER_ATHEROS) {
	      if (if_nametoindex("ath2") > 0)
		      return "ath2";
	      else if (if_nametoindex("ath1") > 0)
		      return "ath1";
	      else
		      return "ath0";
      }

      return "unknown";
}


char * get_station_ifname(void)
{
	if (sigma_station_ifname)
		return sigma_station_ifname;

	/*
	 * If we have both wlan0 and wlan1, assume the first one is the station
	 * interface.
	 */
	if (if_nametoindex("wlan1") > 0 && if_nametoindex("wlan0") > 0)
		return "wlan0";

	if (if_nametoindex("ath0") > 0)
		return "ath0";

	/* If nothing else matches, hope for best and guess.. */
	return "wlan0";
}


void dut_ifc_reset(struct sigma_dut *dut)
{
	char buf[256];
	char *ifc = get_station_ifname();

	snprintf(buf, sizeof(buf), "ifconfig %s down", ifc);
	run_system(dut, buf);
	snprintf(buf, sizeof(buf), "ifconfig %s up", ifc);
	run_system(dut, buf);
}


int wpa_command(const char *ifname, const char *cmd)
{
	struct wpa_ctrl *ctrl;
	char buf[128];
	size_t len;

	printf("wpa_command(ifname='%s', cmd='%s')\n", ifname, cmd);
	snprintf(buf, sizeof(buf), "%s%s", sigma_wpas_ctrl, ifname);
	ctrl = wpa_ctrl_open(buf);
	if (ctrl == NULL) {
		printf("wpa_command: wpa_ctrl_open(%s) failed\n", buf);
		return -1;
	}
	len = sizeof(buf);
	if (wpa_ctrl_request(ctrl, cmd, strlen(cmd), buf, &len, NULL) < 0) {
		printf("wpa_command: wpa_ctrl_request failed\n");
		wpa_ctrl_close(ctrl);
		return -1;
	}
	wpa_ctrl_close(ctrl);
	buf[len] = '\0';
	if (strncmp(buf, "FAIL", 4) == 0) {
		printf("wpa_command: Command failed (FAIL received)\n");
		return -1;
	}
	return 0;
}


int wpa_command_resp(const char *ifname, const char *cmd,
		     char *resp, size_t resp_size)
{
	struct wpa_ctrl *ctrl;
	char buf[128];
	size_t len;

	printf("wpa_command(ifname='%s', cmd='%s')\n", ifname, cmd);
	snprintf(buf, sizeof(buf), "%s%s", sigma_wpas_ctrl, ifname);
	ctrl = wpa_ctrl_open(buf);
	if (ctrl == NULL) {
		printf("wpa_command: wpa_ctrl_open(%s) failed\n", buf);
		return -1;
	}
	len = resp_size;
	if (wpa_ctrl_request(ctrl, cmd, strlen(cmd), resp, &len, NULL) < 0) {
		printf("wpa_command: wpa_ctrl_request failed\n");
		wpa_ctrl_close(ctrl);
		return -1;
	}
	wpa_ctrl_close(ctrl);
	resp[len] = '\0';
	return 0;
}


struct wpa_ctrl * open_wpa_mon(const char *ifname)
{
	struct wpa_ctrl *ctrl;
	char path[256];

	snprintf(path, sizeof(path), "%s%s", sigma_wpas_ctrl, ifname);
	ctrl = wpa_ctrl_open(path);
	if (ctrl == NULL)
		return NULL;
	if (wpa_ctrl_attach(ctrl) < 0) {
		wpa_ctrl_close(ctrl);
		return NULL;
	}

	return ctrl;
}


int get_wpa_cli_events(struct sigma_dut *dut, struct wpa_ctrl *mon,
		       const char **events, char *buf, size_t buf_size)
{
	int fd, ret;
	fd_set rfd;
	char *pos;
	struct timeval tv;
	time_t start, now;
	int i;

	for (i = 0; events[i]; i++) {
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"Waiting for wpa_cli event: %s", events[i]);
	}
	fd = wpa_ctrl_get_fd(mon);
	if (fd < 0)
		return -1;

	time(&start);
	while (1) {
		size_t len;

		FD_ZERO(&rfd);
		FD_SET(fd, &rfd);

		time(&now);
		if ((unsigned int) (now - start) >= dut->default_timeout)
			tv.tv_sec = 1;
		else
			tv.tv_sec = dut->default_timeout -
				(unsigned int) (now - start) + 1;
		tv.tv_usec = 0;
		ret = select(fd + 1, &rfd, NULL, NULL, &tv);
		if (ret == 0) {
			sigma_dut_print(dut, DUT_MSG_INFO, "Timeout on "
					"waiting for events");
			return -1;
		}
		if (ret < 0) {
			sigma_dut_print(dut, DUT_MSG_INFO, "select: %s",
					strerror(errno));
			return -1;
		}
		len = buf_size;
		if (wpa_ctrl_recv(mon, buf, &len) < 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR, "Failure while "
					"waiting for events");
			return -1;
		}
		if (len == buf_size)
			len--;
		buf[len] = '\0';

		pos = strchr(buf, '>');
		if (pos) {
			for (i = 0; events[i]; i++) {
				if (strncmp(pos + 1, events[i],
					    strlen(events[i])) == 0)
					return 0; /* Event found */
			}
		}

		time(&now);
		if ((unsigned int) (now - start) > dut->default_timeout) {
			sigma_dut_print(dut, DUT_MSG_INFO, "Timeout on "
					"waiting for event");
			return -1;
		}
	}
}


int get_wpa_cli_event2(struct sigma_dut *dut, struct wpa_ctrl *mon,
		       const char *event, const char *event2,
		       char *buf, size_t buf_size)
{
	const char *events[3] = { event, event2, NULL };
	return get_wpa_cli_events(dut, mon, events, buf, buf_size);
}


int get_wpa_cli_event(struct sigma_dut *dut, struct wpa_ctrl *mon,
		      const char *event, char *buf, size_t buf_size)
{
	return get_wpa_cli_event2(dut, mon, event, NULL, buf, buf_size);
}


int get_wpa_status(const char *ifname, const char *field, char *obuf,
		   size_t obuf_size)
{
	struct wpa_ctrl *ctrl;
	char buf[4096];
	char *pos, *end;
	size_t len, flen;

	snprintf(buf, sizeof(buf), "%s%s", sigma_wpas_ctrl, ifname);
	ctrl = wpa_ctrl_open(buf);
	if (ctrl == NULL)
		return -1;
	len = sizeof(buf);
	if (wpa_ctrl_request(ctrl, "STATUS", 6, buf, &len, NULL) < 0) {
		wpa_ctrl_close(ctrl);
		return -1;
	}
	wpa_ctrl_close(ctrl);
	buf[len] = '\0';

	flen = strlen(field);
	pos = buf;
	while (pos + flen < buf + len) {
		if (pos > buf) {
			if (*pos != '\n') {
				pos++;
				continue;
			}
			pos++;
		}
		if (strncmp(pos, field, flen) != 0 || pos[flen] != '=') {
			pos++;
			continue;
		}
		pos += flen + 1;
		end = strchr(pos, '\n');
		if (end == NULL)
			return -1;
		*end++ = '\0';
		if (end - pos > (int) obuf_size)
			return -1;
		memcpy(obuf, pos, end - pos);
		return 0;
	}

	return -1;
}


int wait_ip_addr(struct sigma_dut *dut, const char *ifname, int timeout)
{
	char ip[30];
	int count = timeout;

	while (count > 0) {
		sigma_dut_print(dut, DUT_MSG_DEBUG, "%s: ifname='%s' - %d "
				"seconds remaining",
				__func__, ifname, count);
		count--;
		if (get_wpa_status(ifname, "ip_address", ip, sizeof(ip)) == 0
		    && strlen(ip) > 0) {
			sigma_dut_print(dut, DUT_MSG_INFO, "IP address "
					"found: '%s'", ip);
			return 0;
		}
		sleep(1);
	}
	sigma_dut_print(dut, DUT_MSG_INFO, "%s: Could not get IP address for "
			"ifname='%s'", __func__, ifname);
	return -1;
}


void remove_wpa_networks(const char *ifname)
{
	char buf[4096];
	char cmd[256];
	char *pos;

	if (wpa_command_resp(ifname, "LIST_NETWORKS", buf, sizeof(buf)) < 0)
		return;

	/* Skip the first line (header) */
	pos = strchr(buf, '\n');
	if (pos == NULL)
		return;
	pos++;
	while (pos && pos[0]) {
		int id = atoi(pos);
		snprintf(cmd, sizeof(cmd), "REMOVE_NETWORK %d", id);
		wpa_command(ifname, cmd);
		pos = strchr(pos, '\n');
		if (pos)
			pos++;
	}
}


int add_network(const char *ifname)
{
	char res[30];

	if (wpa_command_resp(ifname, "ADD_NETWORK", res, sizeof(res)) < 0)
		return -1;
	return atoi(res);
}


int set_network(const char *ifname, int id, const char *field,
		const char *value)
{
	char buf[200];
	snprintf(buf, sizeof(buf), "SET_NETWORK %d %s %s", id, field, value);
	return wpa_command(ifname, buf);
}


int set_network_quoted(const char *ifname, int id, const char *field,
		       const char *value)
{
	char buf[200];
	snprintf(buf, sizeof(buf), "SET_NETWORK %d %s \"%s\"",
		 id, field, value);
	return wpa_command(ifname, buf);
}


int add_cred(const char *ifname)
{
	char res[30];

	if (wpa_command_resp(ifname, "ADD_CRED", res, sizeof(res)) < 0)
		return -1;
	return atoi(res);
}


int set_cred(const char *ifname, int id, const char *field, const char *value)
{
	char buf[200];
	snprintf(buf, sizeof(buf), "SET_CRED %d %s %s", id, field, value);
	return wpa_command(ifname, buf);
}


int set_cred_quoted(const char *ifname, int id, const char *field,
		    const char *value)
{
	char buf[200];
	snprintf(buf, sizeof(buf), "SET_CRED %d %s \"%s\"",
		 id, field, value);
	return wpa_command(ifname, buf);
}


int start_sta_mode(struct sigma_dut *dut)
{
	FILE *f;
	char buf[100];
	char *ifname;
	char *tmp, *pos;

	if (dut->mode == SIGMA_MODE_STATION)
		return 0;

	if (dut->mode == SIGMA_MODE_AP) {
		if (system("killall hostapd") == 0) {
			int i;

			/* Wait some time to allow hostapd to complete cleanup
			 * before starting a new process */
			for (i = 0; i < 10; i++) {
				usleep(500000);
				if (system("pidof hostapd") != 0)
					break;
			}
		}
	}

	if (dut->mode == SIGMA_MODE_SNIFFER && dut->sniffer_ifname) {
		snprintf(buf, sizeof(buf), "ifconfig %s down",
			 dut->sniffer_ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Failed to run '%s'", buf);
		}
		snprintf(buf, sizeof(buf), "iw dev %s set type station",
			 dut->sniffer_ifname);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Failed to run '%s'", buf);
		}
	}

	dut->mode = SIGMA_MODE_STATION;

	ifname = get_main_ifname();
	if (wpa_command(ifname, "PING") == 0)
		return 0; /* wpa_supplicant is already running */

	/* Start wpa_supplicant */
	f = fopen(SIGMA_TMPDIR "/sigma_dut-sta.conf", "w");
	if (f == NULL)
		return -1;

	tmp = strdup(sigma_wpas_ctrl);
	if (tmp == NULL) {
		fclose(f);
		return -1;
	}
	pos = tmp;
	while (pos[0] != '\0' && pos[1] != '\0')
		pos++;
	if (*pos == '/')
		*pos = '\0';
	fprintf(f, "ctrl_interface=%s\n", tmp);
	free(tmp);
	fprintf(f, "device_name=Test client\n");
	fprintf(f, "device_type=1-0050F204-1\n");
	fclose(f);

#ifdef  __QNXNTO__
	snprintf(buf, sizeof(buf), "wpa_supplicant -Dqca -i%s -B "
		 "-c" SIGMA_TMPDIR "/sigma_dut-sta.conf", ifname);
#else /*__QNXNTO__*/
	snprintf(buf, sizeof(buf), "wpa_supplicant -Dnl80211 -i%s -B "
		 "-c" SIGMA_TMPDIR "/sigma_dut-sta.conf", ifname);
#endif /*__QNXNTO__*/
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_INFO, "Failed to run '%s'", buf);
		return -1;
	}

	sleep(1);

	if (wpa_command(ifname, "PING")) {
		sigma_dut_print(dut, DUT_MSG_INFO, "Failed to communicate "
				"with wpa_supplicant");
		return -1;
	}

	return 0;
}


void stop_sta_mode(struct sigma_dut *dut)
{
	wpa_command("wlan0", "TERMINATE");
	wpa_command("wlan1", "TERMINATE");
	wpa_command("ath0", "TERMINATE");
	wpa_command("ath1", "TERMINATE");
}
