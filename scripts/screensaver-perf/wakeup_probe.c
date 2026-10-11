// SPDX-License-Identifier: GPL-3.0-or-later
//
// Wake-up latency probe for boards without cyclictest. Sleeps to 1 ms deadlines on
// CLOCK_MONOTONIC with clock_nanosleep under the default scheduler, records how late each
// wake-up is, and prints the histogram in the format cyclictest -h writes, so ctparse.py
// reads it unchanged.
//
// usage: wakeup_probe <seconds>
// Build commands are in README.md beside this file.

#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define INTERVAL_NS 1000000L
#define HISTOGRAM_US 20000

static int64_t to_ns(const struct timespec* ts) {
    return (int64_t)ts->tv_sec * 1000000000LL + ts->tv_nsec;
}

static uint64_t histogram[HISTOGRAM_US];

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <seconds>\n", argv[0]);
        return 2;
    }
    char* end = NULL;
    long seconds = strtol(argv[1], &end, 10);
    if (argv[1][0] == '\0' || *end != '\0' || seconds <= 0 || seconds > 3600) {
        fprintf(stderr, "seconds must be a whole number from 1 to 3600\n");
        return 2;
    }

    uint64_t overflow = 0;
    uint64_t samples = 0;
    int64_t max_us = 0;
    struct timespec deadline;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    const int64_t stop_ns = to_ns(&deadline) + (int64_t)seconds * 1000000000LL;

    for (;;) {
        deadline.tv_nsec += INTERVAL_NS;
        if (deadline.tv_nsec >= 1000000000L) {
            deadline.tv_nsec -= 1000000000L;
            deadline.tv_sec++;
        }
        int rc;
        do {
            rc = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, NULL);
        } while (rc == EINTR);
        if (rc != 0) {
            fprintf(stderr, "clock_nanosleep failed: %d\n", rc);
            return 1;
        }
        clock_gettime(CLOCK_MONOTONIC, &now);
        const int64_t late_ns = to_ns(&now) - to_ns(&deadline);
        int64_t late_us = late_ns / 1000;
        if (late_us < 0) {
            late_us = 0;
        }
        if (late_us > max_us) {
            max_us = late_us;
        }
        if (late_us >= HISTOGRAM_US) {
            overflow++;
        } else {
            histogram[late_us]++;
        }
        samples++;
        if (to_ns(&now) >= stop_ns) {
            break;
        }
        // A wake-up more than an interval late starts the schedule again from now instead of
        // owing the missed deadlines as a burst of zero-length sleeps.
        if (late_ns > INTERVAL_NS) {
            deadline = now;
        }
    }

    for (int us = 0; us < HISTOGRAM_US; us++) {
        if (histogram[us] != 0) {
            printf("%06d %06llu\n", us, (unsigned long long)histogram[us]);
        }
    }
    printf("# Total: %09llu\n", (unsigned long long)samples);
    printf("# Histogram Overflows: %05llu\n", (unsigned long long)overflow);
    printf("# Max Latencies: %05lld\n", (long long)max_us);
    return 0;
}
