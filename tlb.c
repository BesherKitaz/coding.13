#define _GNU_SOURCE
/*
 * Coding Assignment 13 - Chapter 19: Paging: Faster Translations (TLBs)
 *
 * Question 1:
 * gettimeofday() reports seconds and microseconds, so its nominal
 * resolution is 1 microsecond (1,000 nanoseconds). The effective timer
 * precision depends on the computer and operating system. Because one
 * memory access is much faster than a microsecond, we must measure many
 * accesses together. A run lasting at least tens of milliseconds (or
 * preferably longer) reduces the timer's relative error. This program
 * reports the smallest nonzero timer increment observed on the machine.
 *
 * Question 2:
 * This program accepts two command-line arguments: the number of pages
 * to access and the number of trials. It allocates an array, accesses one
 * unsigned integer per page for every trial, times the whole operation,
 * and prints the average nanoseconds per page access.
 *
 * Question 5:
 * The array is accessed through a volatile pointer, forcing the compiler
 * to keep the loads and stores instead of optimizing away the timed loop.
 * The program also reads and prints a checksum after the timing ends.
 *
 * Question 6:
 * On Linux, sched_setaffinity() pins this process to one available CPU.
 * Without pinning, the scheduler could move it between CPUs. Because
 * different CPUs have separate TLBs, a move may cause extra TLB misses
 * and make our measurements slower and less consistent.
 *
 * Question 7:
 * Yes. First accesses may trigger page faults, allocation, and demand
 * zeroing, making the measured time artificially high. To avoid including
 * those costs, the program initializes each page and performs warm-up
 * accesses before starting the timer.
 */

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

static int parse_positive(const char *input, uint64_t *result)
{
    char *end = NULL;
    errno = 0;
    if (input[0] == '-' || input[0] == '\0') return 0;
    unsigned long long value = strtoull(input, &end, 10);
    if (errno != 0 || end == input || *end != '\0' || value == 0) return 0;
    *result = (uint64_t)value;
    return 1;
}

static double time_us(const struct timeval *t)
{
    return (double)t->tv_sec * 1000000.0 + (double)t->tv_usec;
}

static long observed_timer_step_us(void)
{
    struct timeval previous, current;
    long min_positive_step = LONG_MAX;
    gettimeofday(&previous, NULL);
    for (int i = 0; i < 20000; i++) {
        gettimeofday(&current, NULL);
        long diff = (current.tv_sec - previous.tv_sec) * 1000000L
                  + (current.tv_usec - previous.tv_usec);
        if (diff > 0 && diff < min_positive_step) min_positive_step = diff;
        previous = current;
    }
    return min_positive_step == LONG_MAX ? -1 : min_positive_step;
}

static void pin_to_one_cpu(void)
{
    cpu_set_t allowed;
    if (sched_getaffinity(0, sizeof(allowed), &allowed) != 0) {
        perror("Warning: cannot check CPU affinity");
        return;
    }
    for (int cpu = 0; cpu < CPU_SETSIZE; cpu++) {
        if (CPU_ISSET(cpu, &allowed)) {
            cpu_set_t single;
            CPU_ZERO(&single);
            CPU_SET(cpu, &single);
            if (sched_setaffinity(0, sizeof(single), &single) != 0) {
                perror("Warning: cannot pin to a CPU");
            } else {
                printf("Pinned to CPU: %d\n", cpu);
            }
            return;
        }
    }
    fprintf(stderr, "Warning: no available CPU found for pinning\n");
}

int main(int argc, char *argv[])
{
    uint64_t pages_arg, trials;
    if (argc != 3 || !parse_positive(argv[1], &pages_arg)
                  || !parse_positive(argv[2], &trials)) {
        fprintf(stderr, "Usage: %s <number_of_pages> <number_of_trials>\n", argv[0]);
        return 1;
    }

    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0 || pages_arg > SIZE_MAX / (size_t)page_size) {
        fprintf(stderr, "Invalid page size or page count too large\n");
        return 1;
    }

    size_t pages = (size_t)pages_arg;
    size_t jump = (size_t)page_size / sizeof(unsigned int);
    size_t elements = pages * jump;
    volatile unsigned int *a = malloc(pages * (size_t)page_size);
    if (a == NULL) {
        perror("malloc");
        return 1;
    }

    pin_to_one_cpu();
    printf("System page size: %ld bytes\n", page_size);
    long timer_step = observed_timer_step_us();
    if (timer_step > 0)
        printf("Smallest observed gettimeofday() step: %ld us\n", timer_step);

    /* Initialize all pages before timing to avoid first-touch costs. */
    for (size_t i = 0; i < elements; i += jump)
        a[i] = 0;

    /* Warm up the pages and their address translations. */
    for (int warmup = 0; warmup < 5; warmup++)
        for (size_t i = 0; i < elements; i += jump)
            a[i] += 1;

    struct timeval start, end;
    if (gettimeofday(&start, NULL) != 0) {
        perror("gettimeofday");
        free((void *)a);
        return 1;
    }

    for (uint64_t t = 0; t < trials; t++) {
        for (size_t i = 0; i < elements; i += jump)
            a[i] += 1;
    }

    if (gettimeofday(&end, NULL) != 0) {
        perror("gettimeofday");
        free((void *)a);
        return 1;
    }

    double elapsed_us = time_us(&end) - time_us(&start);
    double accesses = (double)pages * (double)trials;
    double ns_per_access = elapsed_us * 1000.0 / accesses;

    uint64_t checksum = 0;
    for (size_t i = 0; i < elements; i += jump)
        checksum += a[i];

    printf("Pages touched: %zu\n", pages);
    printf("Trials: %" PRIu64 "\n", trials);
    printf("Elapsed time: %.3f ms\n", elapsed_us / 1000.0);
    printf("Average time per access: %.3f ns\n", ns_per_access);
    printf("Checksum: %" PRIu64 "\n", checksum);
    if (elapsed_us < 10000.0)
        fprintf(stderr, "Note: short run; increase trials for more reliable results.\n");

    free((void *)a);
    return 0;
}
