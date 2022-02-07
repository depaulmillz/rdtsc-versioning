// Olivia Grimes
//
// Timestamp provider
// Timestamp interface for Bundling, Vcas, RDTSC and RDTSCP
// implementations of various data structures

#pragma once

#include <atomic>
#define BUNDLE_MIN_TIMESTAMP 1LL

typedef long long timestamp_t;

// for the BundlingTimestamp & VcasTimestamp
static thread_local int backoff_amt = 0;

class TimestampProvider {
    public:
        virtual timestamp_t Read() = 0;
        virtual timestamp_t Advance() = 0;
};

class RdtscTimestamp: public TimestampProvider {
    private:
        inline timestamp_t readRdtsc() {
            unsigned long long cycles_low, cycles_high;
            asm volatile (
                "CPUID\n\t" // waits for previous code to finish executing 
                "RDTSC\n\t" // reads timestamp and stores in registers rdx and rax
                "mov %%rdx, %0\n\t"
                "mov %%rax, %1\n\t": "=r" (cycles_high), "=r" (cycles_low)::
                "%rax", "%rdx", "%rbx", "%rcx");
            return (((uint64_t)cycles_high << 32) | cycles_low);
        }
    
    public:
        timestamp_t Read() {
            return readRdtsc();
        }

        timestamp_t Advance() {
            return readRdtsc();
        }
};

class RdtscpTimestamp: public TimestampProvider {
    private:
        inline timestamp_t readRdtscp() {
            // TODO: fix memory issues -- LFENCE and MFENCE - is it this simple ?
            unsigned long long cycles_low, cycles_high;
            asm volatile (
                "LFENCE\n\t"
                "RDTSCP\n\t"
                "mov %%rdx, %0\n\t"
                "mov %%rax, %1\n\t"
                "MFENCE\n\t": "=r" (cycles_high), "=r" (cycles_low)::
                "%rax", "%rcx", "%rdx"
                );
            return (((uint64_t)cycles_high << 32) | cycles_low);
        }
    
    public:
        timestamp_t Read() {
            return readRdtscp();
        }

        timestamp_t Advance() {
            return readRdtscp();
        }
};

class BundlingTimestamp: public TimestampProvider {
    private:
        // Timestamp used by range queries to linearize accesses for bundling
        std::atomic<timestamp_t> curr_timestamp_;

        inline void backoff(int amount) {
            if (amount == 0) return;
            volatile long long sum = 0;
            int limit = amount;
            for (int i = 0; i < limit; i++) sum += i;
        }

        inline timestamp_t getNextTS() {
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
        }

    public:
        BundlingTimestamp() {
            curr_timestamp_ = BUNDLE_MIN_TIMESTAMP;
        }

        timestamp_t Read() {
            return curr_timestamp_.load();
        }

        timestamp_t Advance() {
            return getNextTS();
        }
};

class VcasTimestamp: public TimestampProvider {
    private:
        // TODO: difference with volatile vs atomic timestamp? (vcas vs bundling)
        // --> can bundling and vcas be combined into one more general timestamp class, or nah
        volatile timestamp_t timestamp;

        inline void backoff(int amount) {
            if (amount == 0) return;
            volatile long long sum = 0;
            int limit = amount;
            for (int i = 0; i < limit; i++) sum += i;
        }

        inline timestamp_t takeSnapshot() {
            timestamp_t ts = timestamp;
            backoff(backoff_amt);
            std::atomic_thread_fence(std::memory_order_seq_cst);
            if (ts == timestamp) {
            if (__sync_fetch_and_add(&timestamp, 1) == ts)
                backoff_amt /= 2;
            else
                backoff_amt *= 2;
            } else {
            }
            if (backoff_amt < 1) backoff_amt = 1;
            if (backoff_amt > 512) backoff_amt = 512;
            return ts;
        }

    public:
        VcasTimestamp() {
            timestamp = 1;
        }

        timestamp_t Read() {
            return timestamp;
        }

        timestamp_t Advance() {
            return takeSnapshot();
        }
};