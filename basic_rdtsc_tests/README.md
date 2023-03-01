# Basic Testing

Basic initial testing of accessing RDTSC/P in a loop respectively versus accessing an atomic integer in a loop as threads scale.

## Configuration

The following defines must be modified in accordance with the machine used to run the experiment:

```
#define CUTOFF 96
#define NUMA_ZONES 4
#define NUMA_ZONE_THREADS 48
```

+ `CUTOFF` is the point at which thread id's become hyperthreads (ex: thread id 0 and thread id 96 are on the same physical core)

+ `NUMA_ZONES` is the number of NUMA zones in the machine

+ `NUMA_ZONE_THREADS` is the number of cores per NUMA zone


## Executing program

Command to compile: 
```
g++ -W -Wall -Wextra -pthread -o ts.exe timestamp.cpp
```

Command to run:
```
./ts.exe -n [number of total operations] -m [method to run] -t [number of threads]
```

-m options: ["atomic", "atomic_v2", "atomic_no_tf", "rdtsc", "rdtscp", "rdtsc_nofence", "rdtscp_nofence"]

***Note:*** "atomic" and "atomic_no_tf" were not included in the paper, as they are versions of "atomic_v2" but with a less optimal thread pinning strategy.

`atomic` : Incrementing an atomic integer in a loop. This version's pinning policy is as follows: it first saturates a NUMA zone, employing hyperthreading as it saturates each CPU, before beginning to saturate the next NUMA zone in the same manner, and so on until exhausting the number of desired threads.

`atomic_v2` : Incrementing an atomic integer in a loop. For this method, we pin threads to a core such that first, the first NUMA zone is completely filled (including hyperthreads), then the second NUMA zone, and so on, based on the number of threads (-t option) passed in.

`atomic_no_tf` : Incrementing an atomic integer in a loop, without any thread pinning - the OS loadbalances.

`rdtsc` : Accessing RDTSC in a loop. We use the "CPUID" serializing instruction to ensure that the read of TSC occurs at the correct point in time by avoiding instruction re-ordering.

`rdtscp` : Accessing RDTSCP in a loop. As RDTSCP has it's own pseudo-serializing property wherein it waits until all preceding instructions have executed before reading the counter, however, it allows for the possibility that subsequent instructions may begin execution before the read operation of TSC is performed. To fix the latter issue, we use an LFENCE instruction immediately after the RDTSCP instruction.

`rdtsc_nofence` : Accessing RDTSC in a loop without the "CPUID" serializing instruction, i.e., in a potentially non-serializable manner.

`rdtscp` : Accessing RDTSCP in a loop without the LFENCE instruction, i.e., in a potentially non-serializable manner.
