# Basic Testing

Basic initial testing of accessing RDTSC/P in a loop respectively versus accessing an atomic integer in a loop as threads scale.

## Executing program

Command to compile: 
```
g++ -W -Wall -Wextra -pthread -o ts.exe timestamp.cpp
```

Command to run:
```
./ts.exe -n [number of total operations] -m [method to run] -t [number of threads]
```
-m: ["atomic", "rdtsc", "rdtscp", "atomic_no_tf"]

"atomic": Incrementing an atomic integer in a loop. For this method, we pin threads to a core such that first, the first NUMA zone is completely filled (including hyperthreads), then the second NUMA zone, and so on, based on the number of threads (-t option) passed in.

"rdtsc": Accessing RDTSC in a loop. We use the "CPUID" serializing instruction to ensure that the read of TSC occurs at the correct point in time by avoiding instruction re-ordering.

"rdtscp": Accessing RDTSCP in a loop. As RDTSCP has it's own pseudo-serializing property wherein it waits until all preceding instructions have executed before reading the counter, however, it allows for the possibility that subsequent instructions may begin execution before the read operation of TSC is performed. To fix the latter issue, we use an LFENCE instruction immediately after the RDTSCP instruction.

"atomic_no_tf": Incrementing an atomic integer in a loop, without any thread pinning - the OS loadbalances.
