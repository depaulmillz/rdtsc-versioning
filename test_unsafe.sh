#!/bin/bash

# multiple values okay
threads="1 2 24 48 72 96 120 144 168 192"
benchs="luigi.bst.rq_unsafe.out"
iterations="1 2 3 4 5"
sizes="1000000"
rqsize="100"

# only one value - defines the workload, rest is contains
insert="0"
delete="0"
range_query="20"

# shouldn't change
beg_loads="env LD_PRELOAD=lib/libjemalloc.so"
duration="5000"

# 48 per numa zone
bind="0,4,8,12,16,20,24,28,32,36,40,44,48,52,56,60,64,68,72,76,80,84,88,92,96,100,104,108,112,116,120,124,128,132,136,140,144,148,152,156,160,164,168,172,176,180,184,188,1,5,9,13,17,21,25,29,33,37,41,45,49,53,57,61,65,69,73,77,81,85,89,93,97,101,105,109,113,117,121,125,129,133,137,141,145,149,153,157,161,165,169,173,177,181,185,189,2,6,10,14,18,22,26,30,34,38,42,46,50,54,58,62,66,70,74,78,82,86,90,94,98,102,106,110,114,118,122,126,130,134,138,142,146,150,154,158,162,166,170,174,178,182,186,190,3,7,11,15,19,23,27,31,35,39,43,47,51,55,59,63,67,71,75,79,83,87,91,95,99,103,107,111,115,119,123,127,131,135,139,143,147,151,155,159,163,167,171,175,179,183,187,191"

# where to find the executables
bin=executables

ds="bst"
logdir="log-unsafe-${ds}-${insert}-${delete}-${range_query}"

if [ ! -d ${logdir} ]; then
	mkdir ${logdir}
fi

for size in ${sizes}
do
 for thread in ${threads}
 do
  for bench in ${benchs}
  do
   for iter in ${iterations}
   do 
     ${beg_loads} ${bin}/${bench} -i ${insert} -d ${delete} -rq ${range_query} -rqsize ${rqsize} -k ${size} -p -t ${duration} -nrq 0 -nwork ${thread} -bind ${bind} | grep 'total throughput' >> ${logdir}/${bench}-n${thread}.log
   done
   echo "Done experimenting for $bench with $thread threads" 
  done
 done
done
