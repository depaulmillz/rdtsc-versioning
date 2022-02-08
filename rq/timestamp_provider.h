// Olivia Grimes
//
// Timestamp provider
// Timestamp interface for "traditional" timestamping (Bundling, Vcas), RDTSC and RDTSCP
// implementations of various data structures

#pragma once

#include <atomic>
#define MIN_TIMESTAMP 1LL

typedef long long timestamp_t;

// for the BundlingTimestamp & VcasTimestamp
static thread_local int backoff_amt = 0;

class TimestampProvider {
    public:
        virtual inline timestamp_t Read() = 0;
        virtual inline timestamp_t Advance() = 0;
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
        inline timestamp_t Read() {
            return readRdtsc();
        }

        inline timestamp_t Advance() {
            return readRdtsc();
        }
};

class RdtscpTimestamp: public TimestampProvider {
    private:
        inline timestamp_t readRdtscp() {
            unsigned long long cycles_low, cycles_high;
            asm volatile (
                "LFENCE\n\t"
                "RDTSCP\n\t"
                "mov %%rdx, %0\n\t"
                "mov %%rax, %1\n\t": "=r" (cycles_high), "=r" (cycles_low)::
                "%rax", "%rcx", "%rdx"
                );
            return (((uint64_t)cycles_high << 32) | cycles_low);
        }
    
    public:
        inline timestamp_t Read() {
            return readRdtscp();
        }

        inline timestamp_t Advance() {
            return readRdtscp();
        }
};

class BasicTimestamp: public TimestampProvider {
    private:
        volatile timestamp_t curr_timestamp;

        inline void backoff(int amount) {
            if (amount == 0) return;
            volatile long long sum = 0;
            int limit = amount;
            for (int i = 0; i < limit; i++) sum += i;
        }

        inline timestamp_t getNextTS() {
            timestamp_t ts = curr_timestamp;
            backoff(backoff_amt);
            std::atomic_thread_fence(std::memory_order_seq_cst);
            if (ts == curr_timestamp) {
                if (__sync_fetch_and_add(&curr_timestamp, 1) == ts)
                    backoff_amt /= 2;
                else
                    backoff_amt *= 2;
            }
            if (backoff_amt < 1) backoff_amt = 1;
            if (backoff_amt > 512) backoff_amt = 512;
            return ts;
        }

    public:
        BasicTimestamp() {
            curr_timestamp = MIN_TIMESTAMP;
        }

        inline timestamp_t Read() {
            return curr_timestamp;
        }

        inline timestamp_t Advance() {
            return getNextTS();
        }
};