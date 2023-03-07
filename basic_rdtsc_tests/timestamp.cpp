// Author: Olivia Grimes

#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>
#include <mutex>
#include <unistd.h>
#include <cstdlib>
#include <cstring>

#define MILLION 1000000

// configure based on machine used for testing:
#define CUTOFF 96
#define NUMA_ZONES 4
#define NUMA_ZONE_THREADS 48

enum method_t {
    m_atomic=1,
    m_rdtsc=2,
    m_rdtscp=3,
    m_atomic_ntf=4,
    m_atomic_v2=5,
    m_rdtscp_nof=6,
    m_rdtsc_nof=7,
};

struct config_t {
    // variables passed by user
    long long num_ops;
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

// atomic approach WITHOUT pinning threads
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

/*
This version of "atomic" first saturates a NUMA zone with one thread per CPU,
and then employs hyperthreading in the same NUMA zone (i.e., two hyperthreads per CPU)
before beginning to saturate the next NUMA zone in the same manner, and so on

We found this version to perform worse than atomic_v2
*/
void atomic(std::vector<std::thread> *threads) {
    for (int j = 0; j < NUMA_ZONES; j++) {
        for (int i = 0; i < NUMA_ZONE_THREADS; i++) {
            int thread_num = (j * NUMA_ZONE_THREADS) + i;
            if (thread_num < cfg.threads) {
                int cpu_id = i * NUMA_ZONES + j;
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
                return;
            }
        }
    }
}


/*
This version of "atomic" first saturates a NUMA zone, employing hyperthreading as it saturates each CPU,
before beginning to saturate the next NUMA zone in the same manner, and so on

We found this version of atomic to outperform the other version, atomic()
*/
void atomic_v2(std::vector<std::thread> *threads) {
    for (int j = 0; j < NUMA_ZONES; j++) {
        for (int i = 0; i < NUMA_ZONE_THREADS; i+=2) {
            int thread_num = (j * NUMA_ZONE_THREADS) + i;
            int cpu_id = 0;
            if (thread_num < cfg.threads) {
                int cpu_id = i * 2 + j;
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
                return;
            }
            
            int thread_num2 = thread_num + 1;
            if (thread_num2 < cfg.threads) {
                int cpu_id2 = cpu_id + CUTOFF;
                if (thread_num2 == cfg.threads - 1) {
                    int ops = cfg.num_ops - (thread_num2 * ops_per_thread);
                    (*threads)[thread_num2] = std::thread([=]{
                        increment(ops);
                    });
                } else {
                    (*threads)[thread_num2] = std::thread([=]{
                        increment(ops_per_thread);
                    });
                }
                // pin thread to a core
                cpu_set_t cpuset;
                CPU_ZERO(&cpuset);
                CPU_SET(cpu_id2, &cpuset);
                int rc = pthread_setaffinity_np((*threads)[thread_num2].native_handle(), sizeof(cpu_set_t), &cpuset);
                if (rc != 0) {
                    std::cerr << "Error calling pthread_setaffinity_np: " << rc << "\n";
                }
            } else {
                return;
            }
        }
    }
}

// method for each thread to to access rdtscp
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

// starts threads which access rdtscp
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

// version of accessing rdtscp but without LFENCE
void inline getTSrdtscp_NoFences(int ops) {
    cfg.running.fetch_add(1);
    __sync_synchronize();

    while (!cfg.start) {
        __sync_synchronize();
    }

    long long cycles_low, cycles_high, ts;
    for (int i = 0; i < ops; i++) {
        asm volatile (
            "RDTSCP\n\t"
            "mov %%rdx, %0\n\t"
            "mov %%rax, %1\n\t": "=r" (cycles_high), "=r" (cycles_low)::
            "%rax", "%rcx", "%rdx");
        ts = (((uint64_t)cycles_high << 32) | cycles_low);
    }
}

// version of spawning threads which accesses rdtscp without the LFENCE instruction
// NOTE: due to extremely fast rates without fences, num_ops is PER thread here
//       (in all other cases except rdtsc with fences, num_ops is the total number
//        of operations performed by all threads)
void rdtscp_NoFences(std::vector<std::thread> *threads) {
    for (int i = 0; i < cfg.threads; i++){
        (*threads)[i] = std::thread([=]{
            getTSrdtscp_NoFences(cfg.num_ops);
        });
    }
}

// version of accessing rdtscp but without CPUID
void inline getTSrdtsc_NoFences(int ops) {
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
            "mov %%rax, %1\n\t": "=r" (cycles_high), "=r" (cycles_low)::
            "%rax", "%rcx", "%rdx");
        ts = (((uint64_t)cycles_high << 32) | cycles_low);
    }
}

// version of spawning threads which accesses rdtsc without the LFENCE instruction
// NOTE: due to extremely fast rates without fences, num_ops is PER thread here
//       (in all other cases except rdtscp with fences, num_ops is the total number
//        of operations performed by all threads)
void rdtsc_NoFences(std::vector<std::thread> *threads) {
    for (int i = 0; i < cfg.threads; i++){
        (*threads)[i] = std::thread([=]{
            getTSrdtsc_NoFences(cfg.num_ops);
        });
        
    }
}

// method for each thread to to access rdtscp
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

// start threads to access rdtsc
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
        case m_rdtscp_nof:
            rdtscp_NoFences(&threads);
            break;
        case m_rdtsc_nof:
            rdtsc_NoFences(&threads);
            break;
        case m_atomic_ntf:
            atomic_no_tf(&threads);
            break;
        case m_atomic_v2:
            atomic_v2(&threads);
            break;
        case m_atomic_test:
            atomic_test(&threads);
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
    long double opsPerSec;

    // NOTE: due to speed of rdtsc/p without fences, num_ops is ops PER thread, not total of ALL threads
    if ((cfg.method == m_rdtscp_nof) || (cfg.method == m_rdtsc_nof)) {
        long double tot_ops = (double)cfg.num_ops * cfg.threads;
        std::cout << "Total number of operations: " << tot_ops << "\n";
        opsPerSec = (double)cfg.num_ops * (double)cfg.threads / (double)time;
    } else {
        opsPerSec = (double)cfg.num_ops / (double)time;
    }

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
            if (!strcmp(optarg, "atomic")) {
                cfg.method = m_atomic;
            } else if(!strcmp(optarg, "rdtsc")) {
                cfg.method = m_rdtsc;
            } else if(!strcmp(optarg, "rdtscp")) {
                cfg.method = m_rdtscp;
            } else if(!strcmp(optarg, "atomic_no_tf")) {
                cfg.method = m_atomic_ntf;
            } else if(!strcmp(optarg, "atomic_v2")) {
                cfg.method = m_atomic_v2;
            } else if(!strcmp(optarg, "rdtscp_nofence")) {
                cfg.method = m_rdtscp_nof;
            } else if(!strcmp(optarg, "rdtsc_nofence")) {
                cfg.method = m_rdtsc_nof;
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
