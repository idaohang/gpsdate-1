/* gpsdate - small utility to set system RTC based on gpsd time 
 * (C) 2013 by sysmocom - s.f.m.c. GmbH, Author: Harald Welte
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/* The idea of this program is that you run it once at system boot time,
 * to set the local RTC to the time received by GPS.  Further synchronization
 * during system runtime is then handled by ntpd, interfacing with gpsd using
 * the ntp shared memory protocol.
 *
 * However, ntpd is unable to accept a GPS time that's off by more than four
 * hours from the system RTC, so initial synchronization has to be done
 * externally.  'ntpdate' is the usual option, but doesn't work if you're
 * offline.  Thus, this gpsdate utilith was created to fill the gap.
 */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <syslog.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <gps.h>

#define NUM_RETRIES	60	/* Number of gpsd re-connects */
#define RETRY_SLEEP	1	/* Seconds to sleep between re-connects */

static struct gps_data_t gpsdata;

static void callback(struct gps_data_t *gpsdata)
{
	struct timeval tv;
	time_t time;
	int rc;

	if (!(gpsdata->set & TIME_SET))
		return;

	tv.tv_sec = gpsdata->fix.time;
	/* FIXME: use the fractional part for microseconds */
	tv.tv_usec = 0;

	time = tv.tv_sec;

	rc = settimeofday(&tv, NULL);
	gps_close(gpsdata);
	if (rc == 0) {
		syslog(LOG_NOTICE, "Successfully set RTC time to GPSD time:"
			" %s", ctime(&time));
		closelog();
		exit(EXIT_SUCCESS);
	} else {
		syslog(LOG_ERR, "Error setting RTC: %d (%s)\n",
			errno, strerror(errno));
		closelog();
		exit(EXIT_FAILURE);
	}
}

static int osmo_daemonize(void)
{
	int rc;
	pid_t pid, sid;

	/* Check if parent PID == init, in which case we are already a daemon */
	if (getppid() == 1)
		return -EEXIST;

	/* Fork from the parent process */
	pid = fork();
	if (pid < 0) {
		/* some error happened */
		return pid;
	}

	if (pid > 0) {
		/* if we have received a positive PID, then we are the parent
		 * and can exit */
		exit(0);
	}

	/* FIXME: do we really want this? */
	umask(0);

	/* Create a new session and set process group ID */
	sid = setsid();
	if (sid < 0)
		return sid;

	/* Change to the /tmp directory, which prevents the CWD from being locked
	 * and unable to remove it */
	rc = chdir("/tmp");
	if (rc < 0)
		return rc;

	/* Redirect stdio to /dev/null */
/* since C89/C99 says stderr is a macro, we can safely do this! */
#ifdef stderr
	freopen("/dev/null", "r", stdin);
	freopen("/dev/null", "w", stdout);
	freopen("/dev/null", "w", stderr);
#endif

	return 0;
}

int main(int argc, char **argv)
{
	char *host = "localhost";
	int i, rc;

	openlog("gpsdate", LOG_PERROR, LOG_CRON);

	if (argc > 1)
		host = argv[1];

	for (i = 1; i <= NUM_RETRIES; i++) {
		printf("Attempt #%d to connect to gpsd at %s...\n", i, host);
		rc = gps_open(host, DEFAULT_GPSD_PORT, &gpsdata);
		if (!rc)
			break;
		sleep(RETRY_SLEEP);
	}

	if (rc) {
		syslog(LOG_ERR, "no gpsd running or network error: %d, %s\n",
			errno, gps_errstr(errno));
		closelog();
		exit(EXIT_FAILURE);
	}

	osmo_daemonize();

	gps_stream(&gpsdata, WATCH_ENABLE|WATCH_JSON, NULL);

	/* We run in an endless loop.  The only reasonable way to exit is after
	 * a correct GPS timestamp has been received in callback() */
	while (1)
		gps_mainloop(&gpsdata, INT_MAX, callback);

	gps_close(&gpsdata);

	closelog();
	exit(EXIT_SUCCESS);
}
