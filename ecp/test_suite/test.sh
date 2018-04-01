#!/bin/bash

source geopm-env.sh
numnodes=$1
numtasks=$2

trace_dump=~/ecp_traces/test
for pcap in ${pcaplist}; #240 220 200 180 160 140 120;
do
    sed "s/POWER_BUDGET/${pcap}/g" policy_balanced.json > temp_policy.json

    tracepath=${trace_dump}.${pcap}.${runiter}.balanced
    mkdir -p ${tracepath}
    MPIEXEC="srun"
    LD_DYNAMIC_WEAK=true \
    LD_PRELOAD=/g/g92/marathe1/geopm/install-dev/lib/libgeopm.so \
    GEOPM_PMPI_CTL=process \
    GEOPM_REPORT="" \
    GEOPM_TRACE="${tracepath}/trace" \
    GEOPM_POLICY=./temp_policy.json \
    GEOPM_SHMKEY=/geopm-shm-l2-${numnodes}-`whoami` \
    srun -N ${numnodes} -n ${numtasks} \
    -m block \
    --sockets-per-node=2 \
    --ntasks-per-node=24 \
    ./main.geo

#    2>& 1) >& ${tracepath}/output
done
