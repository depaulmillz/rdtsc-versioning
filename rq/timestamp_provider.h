// Olivia Grimes
//
// Timestamp provider

#pragma once

#include <atomic>
#include "common_bundle.h"

static thread_local int backoff_amt = 0;

class TimestampProvider {
    private:
        // Timestamp used by range queries to linearize accesses for original impl
        std::atomic<timestamp_t> curr_timestamp_;

    public:
        TimestampProvider() {
        #ifdef RQ_BUNDLE
            curr_timestamp_ = BUNDLE_MIN_TIMESTAMP;
        #endif
        }

        ~TimestampProvider() {}

        // retrieve timestamp used by range queries to linearize accesses with RDTSC
        inline timestamp_t getTS_RDTSC() {
            unsigned long long cycles_low, cycles_high;
            asm volatile (
                "CPUID\n\t" // waits for previous code to finish executing 
                "RDTSC\n\t" // reads timestamp and stores in registers rdx and rax
                "mov %%rdx, %0\n\t"
                "mov %%rax, %1\n\t": "=r" (cycles_high), "=r" (cycles_low)::
                "%rax", "%rdx", "%rbx", "%rcx");
            return (((uint64_t)cycles_high << 32) | cycles_low);
        }

        // retrieve timestamp used by range queries to linearize accesses with RDTSCP
        inline timestamp_t getTS_RDTSCP() {
            // TODO: fix memory issues
            unsigned long long cycles_low, cycles_high;
            asm volatile (
                "RDTSCP\n\t"
                "mov %%rdx, %0\n\t"
                "mov %%rax, %1\n\t": "=r" (cycles_high), "=r" (cycles_low)::
                "%rax", "%rcx", "%rdx");
            return (((uint64_t)cycles_high << 32) | cycles_low);
        }

        inline void backoff(int amount) {
            if (amount == 0) return;
            volatile long long sum = 0;
            int limit = amount;
            for (int i = 0; i < limit; i++) sum += i;
        }

        inline long long getNextTS() {
        #ifdef RQ_BUNDLE
            timestamp_t ts = curr_timestamp_.load(std::memory_order_seq_cst);
            backoff(backoff_amt);
            if (ts == curr_timestamp_.load(std::memory_order_seq_cst)) {
                if (curr_timestamp_.fetch_add(1, std::memory_order_release) == ts)
                    backoff_amt /= 2;
                else
                    backoff_amt *= 2;
            }
            if (backoff_amt < 1) backoff_amt = 1;
            if (backoff_amt > 512) backoff_amt = 512;
            return ts + 1;
        #elif defined RQ_BUNDLE_RDTSC
            return getTS_RDTSC();
        #elif defined RQ_BUNDLE_RDTSCP
            return getTS_RDTSCP();
        #else
        #error NO RQ PROVIDER DEFINED
        #endif
        }

        inline timestamp_t getTS() {
        #ifdef RQ_BUNDLE
            return curr_timestamp_;
        #elif defined RQ_BUNDLE_RDTSC
            return getTS_RDTSC();
        #elif defined RQ_BUNDLE_RDTSCP
            return getTS_RDTSCP();
        #else
        #error NO RQ PROVIDER DEFINED
        #endif
        }

        inline timestamp_t updateTS_CAS() {
            timestamp_t ts = curr_timestamp_;
            curr_timestamp_.compare_exchange_strong(ts, ts + 1);
            return ts;
        }

        inline timestamp_t loadTS() {
        #ifdef RQ_BUNDLE
            return curr_timestamp_.load(std::memory_order_acquire);
        #elif defined RQ_BUNDLE_RDTSC
            return getTS_RDTSC();
        #elif defined RQ_BUNDLE_RDTSCP
            return getTS_RDTSCP();
        #else
        #error NO RQ PROVIDER DEFINED
        #endif
        }

};