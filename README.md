# 1. Opportunities and Limitations of Hardware Timestamps in Concurrent Data Structures

In this paper we analyze techniques meant to augment concurrent data stcutures to include the linearizable range query operation. Each of the techniques analyzed rely on an atomic timestamp to provide MVCC (multi-version concurrency control). Past work has leveraged TSC, the CPUs timestamp counter, to replace the bottleneck-producing atomic timestamp for multi-versioned systems. In this paper, we analyze the opportunities and limitations of using a hardware timestamps in concurrent data structures.

# 2. Implementation notes

This work builds on a benchmark framework provided by Arbel-Raviv and Brown (https://bitbucket.org/trbot86/implementations/src/master/cpp/range_queries/) to augment concurrent data structures to include lineariable range queries. The three range query techniques studied in this paper include Bundled Refs (or simply "Bundling"), vCAS, and EBR-RQ, each of which stem from Arbel-Raviv and Brown's framework. Each of the RQ techniques rely on an atomic timestamp to provide concurrency control.

This work uses the Bundled References codebase (https://github.com/sss-lehigh/bundledrefs) provided by Jacob Nelson as a starting point (as it is the most recently published work and contains all three RQ implementations), and adds to the range query provider in order to support reading from TSC instead of using a logical timestamp. 

The hardware timestamp API provided by this work can be found in 'rq/timestamp_provider.h.'. The data structure implementations with bundling are found in 'bundled_\*' directories, those that use vCAS are found in 'vcas_\*' directories, and any other listed data structures (e.g., bst/) are implemented using EBR-RQ. The scripts to produce the plots found in our paper are included under the 'microbench/' directory.

# 3. Getting Started Guide

## a. Important files and directories

**Implementation**

`rq/timestamp_provider.h` provides the hardware timestamping API allowing access to a timestamp that either (1) reads TSC using one of two methods: one which uses the RDTSC instruction, and one which uses the RDTSCP instruction, or (2) reads and writes (increments) a logical, atomic timestamp variable.

`basic_rdtsc_tests/` is a directory which contains the code used in our initial testing (Figure 1 in the paper). Refer to the README located in that directory for documentation.

**Experiments**

`config.mk` is the primary configuration file and is used across all experiments. Users will need to update this file to match their system (more on this below).

The experiments that we report in the paper are located in `microbench/` which tests each data structure in isolation. The experiment is run with the `microbench/runscript.sh` script.

**Generated files**

After running `sh microbench/runscript.sh` the following files are generated:

+ `/microbench/experiment_list.txt` lists all of the experiments to be performed based on the config options set in `/microbench/experiment_list_generate.sh`

+ `/microbench/data` - tests are run based on reading lines from `/microbench/experiment_list.txt`. The full output of each run are sent to their own file in the `microbench/data/workloads/<rq _technique>/` directory, and a formatted one-line-per-run output is written to `microbench/data/summary.txt`.

After running the plot.py script with the proper options passed (see below) the following files are generated:

+ `/microbench/data/workloads/<data_structure>.csv` - first the output is read from `microbench/data/summary.txt` as previously created, and turned into a csv file (if it didn't already exist)

+ `/figures` stores the generated plots - plotly produces figures which can be found in the `/figures` directory. The figures produced are html files and must be downloaded and opened in a browser.


## b. Requirements

The experiments from the paper were executed on a 4-socket machine with Intel Xeon Platinum 8160 processors running Ubuntu 20.04. The following are required for building and running the experiments, as well as plotting the results:

**Unix Utilities**
+ make (e.g., `sudo apt install make`)
+ dos2unix (e.g., `sudo apt install dos2unix`)
+ bc (e.g., `sudo apt install bc`)

**C++ Libraries:**
+ libnuma (e.g., `sudo apt install libnuma-dev`)
+ libjemalloc

**Python libraries:**
+ python (v3.10)
+ plotly (v5.1.0)
+ psutil (v5.8.0)
+ requests (v2.26.0)
+ pandas (v1.3.4)
+ absl-py (v0.13.0)

Please refer to the Bundled References documentation for instructions on how to install the python libraries (`README_bundling.md`).

To install jemalloc, download the following tar file and follow the instructions contained inside: https://github.com/jemalloc/jemalloc/releases/download/5.2.1/jemalloc-5.2.1.tar.bz2


## c. Building the Project

Once configured, build the binaries for each of the data structures, range query techniques, and timestamp types with the following:

```
cd microbench
make -j lazylist skiplistlock citrus bst
```

The arguments to the `make` command build the EBR-based approach from Arbel-Raviv and Brown, the vCAS approach of Wei et al., the Bundling approach of Nelson et al., and an unsafe version of each that has no consistency guarantees for range queries. For each afformentioned approach, The produced executables are named according to the following convention:

`<hostname>.<data structure>.<rq technique>.<timestamp type>.out`

(Note that `<timestamp type>` is not included in some cases for range query techniques that are out of the scope of this paper).

## d. Running Individual Experiments

Finally, run individual tests to obtain results for a given configuration. The following command runs a workload of 5% inserts (`-i 5`), 5% deletes (`-d 5`), 80% gets and 10% range queries (`-rq 10`) on a key range of 100000 (`-k 100000`). Each range query has a range of 50 keys (`-rqsize 50`) and is prefilled (`-p`) based on the ratio of inserts and deletes. The execution lasts for 1s (`-t 1000`). There are no dedicated range query threads (`-nrq 0`) but there are a total of 8 worker threads (`-nwork 8`) and they are bound to cores following the bind policy (`-bind 0-7,16-23,8-15,24-31`). Do not forget to load jemalloc and replace `<hostname>` with the correct value.

```
env LD_PRELOAD=../lib/libjemalloc.so TREE_MALLOC=../lib/libjemalloc.so \ 
./<hostname>.skiplistlock.rq_bundle.out -i 5 -d 5 -k 100000 -rq 10 \ 
-rqsize 50 -p -t 1000 -nrq 0 -nwork 8 -bind 0-7,16-23,8-15,24-31
```

For more information on the input parameters to the microbenchmark itself see README.txt.old, which is for the original benchmark implementation. We did not change any arguments.

# Microbenchmark

Our results demonstrate that techniques reliant on a timestamp to provide multi-versioning concurrency control benefit in performance when the method behind the timestamp is accessing RDTSC as opposed to reading and writing an atomic integer, as previously done.

Once the dependencies above are installed, and the data structure libraries have been built during the previous steps, we can generate the plots included in the paper. From the root directory, run the following:

```
./runscript.sh
cd ..
python plot.py --save_plots --microbench
```

As previously noted, configuration options can be modified in the following files:
+ `config.mk` stores the information of the system on which the tests are being run
+ `experiment_list_generate.sh` determines the range query techniques (line 10), data structures (line 11), and timestamp methods which will be tested (line 13), as well as the data structure key range (line 12). There are two experiments in `experiment_list_generate.sh`, and for the purpose of this paper we only use the run workloads method. You can additionally modify the range query rates (line 23) and update rates (line 24) tested in the run workloads method.
+ `runscript.sh` defines the length of runs (line 39) and the number of trials (line 9). As noted in the paper, we run 5 trials for 3 seconds long for each configuration of data structure, range query technique, and timestamp type.

**Output**

As stated previously, the microbenchmark saves data under `./microbench/data`. This raw data is used by the plotting script, but is first translated to a .csv file that is also stored in the subdirectory corresponding to each experiment in `experiment_list_generate.sh`. Upon running `plot.py` with the argument `--save_plots`, the generated graphs will be stored in `./figures` (again, in the corresponding subdirectories).