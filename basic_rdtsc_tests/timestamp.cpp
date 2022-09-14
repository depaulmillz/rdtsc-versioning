#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>
#include <mutex>
#include <iostream>
#include <unistd.h>
#include <cstdlib>
#include <cstring>

#define MILLION 1000000
#define CUTOFF 96
#define MAX_THREADS 192

#define NUMA_ZONES 4
#define NUMA_ZONE_THREADS 48

// g++ -W -Wall -Wextra -pthread -o ts.exe timestamp.cpp
// ./ts.exe -n 400000000 -m atomic -t 196

enum method_t {
    m_atomic=1,
    m_rdtsc=2,
    m_rdtscp=3,
    m_atomic_ntf=4,
};

struct config_t {
    // variables passed by user
    int num_ops;
    method_t method;
    int threads;

    // whether timing has started
    bool start;

    // number of threads running
    std::atomic<int> running;
};

std::atomic<int> ts{0};
config_t cfg;
int ops_per_thread;

void increment(int ops) {
    cfg.running.fetch_add(1);
    __sync_synchronize();

    while (!cfg.start) {
        __sync_synchronize();
    }
    
    for (int i = 0; i < ops; i++) {
        ts.fetch_add(1);
    }
}

void atomic_no_tf(std::vector<std::thread> *threads) {
    for (int i = 0; i < cfg.threads; i++){
        if (i == cfg.threads - 1) {
            int ops = cfg.num_ops - (i * ops_per_thread);
            (*threads)[i] = std::thread([=]{
                increment(ops);
            });

        } else {
            (*threads)[i] = std::thread([=]{
                increment(ops_per_thread);
            });
        }
    }
}

void atomic(std::vector<std::thread> *threads) {
    for (int j = 0; j < NUMA_ZONES; j++) {
        for (int i = 0; i < NUMA_ZONE_THREADS; i++) {
            int thread_num = (j * NUMA_ZONE_THREADS) + i;
            if (thread_num < cfg.threads) {
                int cpu_id = (thread_num % NUMA_ZONE_THREADS) * NUMA_ZONES + j;
                if (thread_num == cfg.threads - 1) {
                    int ops = cfg.num_ops - (thread_num * ops_per_thread);
                    (*threads)[thread_num] = std::thread([=]{
                        increment(ops);
                    });
                } else {
                    (*threads)[thread_num] = std::thread([=]{
                        increment(ops_per_thread);
                    });
                }
                // pin thread to a core
                cpu_set_t cpuset;
                CPU_ZERO(&cpuset);
                CPU_SET(cpu_id, &cpuset);
                int rc = pthread_setaffinity_np((*threads)[thread_num].native_handle(), sizeof(cpu_set_t), &cpuset);
                if (rc != 0) {
                    std::cerr << "Error calling pthread_setaffinity_np: " << rc << "\n";
                }
            } else {
                std::cout << "Goodbye, thread num is " << thread_num << "\n";
                return;
            }
        }
    }
}

void inline getTSrdtscp(int ops) {
    cfg.running.fetch_add(1);
    __sync_synchronize();

    while (!cfg.start) {
        __sync_synchronize();
    }

    long long cycles_low, cycles_high, ts;
    for (int i = 0; i < ops; i++) {
        asm volatile (
            "RDTSCP\n\t" // reads timestamp and stores in registers edx and eax
            "mov %%rdx, %0\n\t"
            "mov %%rax, %1\n\t"
            "LFENCE\n\t": "=r" (cycles_high), "=r" (cycles_low)::
            "%rax", "%rcx", "%rdx");
        ts = (((uint64_t)cycles_high << 32) | cycles_low);
    }
}

void rdtscp(std::vector<std::thread> *threads) {
    for (int i = 0; i < cfg.threads; i++){
        if (i == cfg.threads - 1) {
            int ops = cfg.num_ops - (i * ops_per_thread);
            (*threads)[i] = std::thread([=]{
                getTSrdtscp(ops);
            });
        } else {
            (*threads)[i] = std::thread([=]{
                getTSrdtscp(ops_per_thread);
            });
        }
    }
}

void inline getTSrdtsc(int ops) {
    cfg.running.fetch_add(1);
    __sync_synchronize();

    while (!cfg.start) {
        __sync_synchronize();
    }

    long long cycles_low, cycles_high, ts;
    for (int i = 0; i < ops; i++) {
        asm volatile (
            "CPUID\n\t" // waits for previous code to finish executing 
            "RDTSC\n\t" // reads timestamp and stores in registers edx and eax
            "mov %%rdx, %0\n\t"
            "mov %%rax, %1\n\t": "=r" (cycles_high), "=r" (cycles_low)::
            "%rax", "%rdx", "%rbx", "%rcx");
        ts = (((uint64_t)cycles_high << 32) | cycles_low);
    }
}

void rdtsc(std::vector<std::thread> *threads) {
    for (int i = 0; i < cfg.threads; i++){
        if (i == cfg.threads - 1) {
            int ops = cfg.num_ops - (i * ops_per_thread);
            (*threads)[i] = std::thread([=]{
                getTSrdtsc(ops);
            });
        } else {
            (*threads)[i] = std::thread([=]{
                getTSrdtsc(ops_per_thread);
            });
        }
    }
}

void trial() {
    std::chrono::time_point<std::chrono::high_resolution_clock> start;
    std::chrono::time_point<std::chrono::high_resolution_clock> end;
    std::vector<std::thread> threads(cfg.threads);

    // do work
    switch (cfg.method) {
        case m_atomic:
            atomic(&threads);
            break;
        case m_rdtsc:
            rdtsc(&threads);
            break;
        case m_rdtscp:
            rdtscp(&threads);
            break;
        case m_atomic_ntf:
            atomic_no_tf(&threads);
            break;
        default:
            std::cout << "Invalid method entered. Try again. \n";
            return;
    }
    __sync_synchronize();

    // wait on all threads to be ready
    while (!(cfg.running == cfg.threads)) {}
    __sync_synchronize();

    // take the starting time
    start = std::chrono::high_resolution_clock::now();
    __sync_synchronize();

    // allow threads to start
    cfg.start = true;

    for (int i = 0; i < cfg.threads; i++) {
        threads.at(i).join();
    }
    __sync_synchronize();
    end = std::chrono::high_resolution_clock::now();

    int time = std::chrono::duration_cast<std::chrono::microseconds>(end-start).count();
    double timeSec = ((double)time / MILLION);
    double opsPerSec = cfg.num_ops / (timeSec * MILLION);

    std::cout << "Total time micro-seconds: " << time << "\n";
    std::cout << "Total time seconds: " << timeSec << "\n";
    std::cout << "ops/second (in millions): " << opsPerSec << "\n";

}

void usage() {
    std::cout
        << "Command-Line Options:" << std::endl
        << "  -n <int>    : number of total operations" << std::endl
        << "  -m <string> : the chosen method to run (atomic, rdtsc, rdtscp, atomic_no_tf *)" << std::endl
        << "  -t <int>    : the number of threads in the experiment" << std::endl
        << "  -h          : display this message and exit" << std::endl
        << "                * \"tf\" = thread affinity" << std::endl << std::endl;
    exit(0);
}

bool parseargs(int argc, char** argv) {
    // parse the command-line options
    int opt;
    while ((opt = getopt(argc, argv, "t:n:m:h")) != -1) {
        switch (opt) {
          case 'n': cfg.num_ops = atoi(optarg); break;
          case 'm': 
            if (!strcmp(optarg, "atomic")){
                cfg.method = m_atomic;
            }else if(!strcmp(optarg, "rdtsc")){
                cfg.method = m_rdtsc;
            }else if(!strcmp(optarg, "rdtscp")){
                cfg.method = m_rdtscp;
            }else if(!strcmp(optarg, "atomic_no_tf")){
                cfg.method = m_atomic_ntf;
            }
            break;
          case 't': cfg.threads = atoi(optarg); break;
          case 'h': usage(); exit(0); break;
          default: return false; break;
        }
    }
    return true;
}

int main(int argc, char *argv[]) {
    cfg.num_ops = -1;
    cfg.threads = -1;
    
    if (!parseargs(argc, argv)) {
        std::cout << "Error parsing args.\n";
        return 0;
    }

    if (cfg.num_ops < 0 || cfg.threads < 0) {
        std::cout << "-t and -n must be passed, and be greater than 0. Try again.\n";
        return 0;
    }

    ops_per_thread = cfg.num_ops / cfg.threads;
    trial();
}






// TODO: try printing thing from: https://eli.thegreenplace.net/2016/c11-threads-affinity-and-hyperthreading/
    
    // std::vector<cpu_set_t> cpu_sets(NUM_THREADS);
    // std::vector<pthread_attr_t> attributes(NUM_THREADS);

    // for (int i = 0; i < cpu_sets.size(); i++) {
    //     CPU_ZERO(&cpu_sets.at(i));
    //     CPU_SET(i, &cpu_sets.at(i));
    // }