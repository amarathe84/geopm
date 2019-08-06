/*
 * Copyright (c) 2015, 2016, 2017, 2018, 2019, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY LOG OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdint.h>
#include <string>
#include <limits.h>
#include <map>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>


#include "geopm.h"
#include "geopm_sched.h"
#include "geopm_error.h"
#include "geopm_env.h"
#include "Exception.hpp"
#include "OMPT.hpp"
#include "config.h"

#define GEOPM_ENABLE_OMPT
#ifndef GEOPM_ENABLE_OMPT

namespace geopm
{
    // If OMPT is not enabled, ompt_pretty_name is a pass through.
    void ompt_pretty_name(std::string &name)
    {

    }
}

#else // GEOPM_ENABLE_OMPT defined

#include <ompt.h>
#include <omp.h>

extern "C"
{
    int geopm_is_pmpi_prof_enabled(void);
}
#include <memory>
#include "Helper.hpp"
#include "ControlMessage.hpp"
#include "SharedMemory.hpp"
#include "AppConfigData.hpp"
#include <unistd.h>
#include <map>
#include <sys/time.h>

namespace geopm
{

    class OMPT
    {
        public:
            OMPT();
            OMPT(const std::string &map_path);
            virtual ~OMPT() = default;
            uint64_t region_id(void *parallel_function);
            void region_name(void *parallel_function, std::string &name);
            void region_name_pretty(std::string &name);
            std::unique_ptr<geopm::ISharedMemoryUser> m_app_ctl_shmem;
            struct app_interface *m_conf;
            void init_omp_shmem(void);
            int m_shm_rank;
            struct timeval start_t, end_t;
            int m_region_idx;
        private:
            /// Map from <virtual_address, is_end> pair representing
            /// half of a virtual address range to the object file
            /// asigned to the address range.
            std::map<std::pair<size_t, bool>, std::string> m_range_object_map;
            /// Map from function address to geopm region ID
            std::map<size_t, uint64_t> m_function_region_id_map;
    };

    static OMPT &ompt(void)
    {
        static OMPT instance;
        return instance;
    }

    void OMPT::init_omp_shmem(void) {
        std::string sample_key(geopm_env_shmkey());
        sample_key += configshmkey;
        // Remove shared memory file if one already exists.
        std::string sample_key_path(sample_key);
//        (void)unlink(sample_key_path.c_str());
        m_app_ctl_shmem = std::unique_ptr<ISharedMemoryUser>(new SharedMemoryUser(sample_key, geopm_env_timeout()));
        m_conf = (struct app_interface *)m_app_ctl_shmem->pointer(); 
        int iter;
        int pid = getpid();
        for(iter = 0; iter < MAX_PROCS_PER_NODE; iter++) {
            if(m_conf->pmap[iter] == pid) {
                break;
            }
        }
        m_shm_rank = iter;

//        int iter;
//        std::ofstream ofile;
//        std::string fcinit = "configinit_" + std::to_string(getpid());
//        printf("Writing to %s\n", fcinit.c_str());
//        ofile.open(fcinit, std::ios::out);
//        for(iter = 0; iter < (NUMTHREADS * NUMPCAPS); iter++) {
//            ofile << "Thread: " << m_conf->config[m_shm_rank].threads[iter] << ", Pcap" << m_conf->config[m_shm_rank].pcap[iter] << "\n";
//        }
//        ofile.close();

        printf("OMP: Rank:%d\t GetPID:%d\tGetPPID:%d\n", m_shm_rank, getpid(), getppid());
        m_region_idx = 0;
    }

    OMPT::OMPT()
        : OMPT("/proc/self/maps")
    {

    }

    OMPT::OMPT(const std::string &map_path)
    {
        std::ifstream maps_stream(map_path);
        while (maps_stream.good()) {
            std::string line;
            std::getline(maps_stream, line);
            if (line.length() == 0) {
                continue;
            }
            size_t addr_begin, addr_end;
            int n_scan = sscanf(line.c_str(), "%zx-%zx", &addr_begin, &addr_end);
            if (n_scan != 2) {
                continue;
            }

            std::string object;
            size_t object_loc = line.rfind(' ') + 1;
            if (object_loc == std::string::npos) {
                continue;
            }
            object = line.substr(object_loc);
            if (line.find(" r-xp ") != line.find(' ')) {
                continue;
            }
            //printf("object: %s, object_loc:%d, addr_begin:%zx, addr_end:%zx\n", object.c_str(), (int) object_loc, (int) addr_begin, (int)addr_end);
            std::pair<size_t, bool> aa = std::make_pair(addr_begin, false);
            std::pair<size_t, bool> bb = std::make_pair(addr_end, true);
            std::pair<std::pair<size_t, bool>, std::string> cc = std::make_pair(aa, object);
            std::pair<std::pair<size_t, bool>, std::string> dd = std::make_pair(bb, object);
            auto it0 = m_range_object_map.insert(m_range_object_map.begin(), {aa, object});
//                printf("it0:addr_begin:%zx\tit0:bool:%d\tit0:object:%s\n", it0->first.first, it0->first.second, it0->second.c_str());
            auto it1 = m_range_object_map.insert(it0, {bb, object});
            ++it0;
            if (object != "[vdso]" && it0 != it1) { 
                throw Exception("Error parsing /proc/self/maps, overlapping address ranges.",
                                GEOPM_ERROR_LOGIC, __FILE__, __LINE__);
            }
        }
        init_omp_shmem();
    }

    uint64_t OMPT::region_id(void *parallel_function)
    {
        uint64_t result = GEOPM_REGION_HASH_UNMARKED;
        auto it = m_function_region_id_map.find((size_t)parallel_function);
        if (m_function_region_id_map.end() != it) {
            result = it->second;
        }
        else {
            std::string rn;
            region_name(parallel_function, rn);
            int err = geopm_prof_region(rn.c_str(), GEOPM_REGION_HINT_UNKNOWN, &result);
            if (err) {
                result = GEOPM_REGION_HASH_UNMARKED;
            }
            else {
                m_function_region_id_map.insert(std::pair<size_t, uint64_t>((size_t)parallel_function, result));
            }
        }
        return result;
    }

    void OMPT::region_name(void *parallel_function, std::string &name)
    {
        name.clear();
        auto it_max = m_range_object_map.upper_bound(std::pair<size_t, bool>((size_t)parallel_function, false));
        auto it_min = it_max;
        --it_min;
        if (it_max != m_range_object_map.end() &&
            it_max != m_range_object_map.begin() &&
            false == it_min->first.second &&
            true == it_max->first.second) {
            size_t offset = (size_t)parallel_function - (size_t)(it_min->first.first);
            std::ostringstream name_stream;
            name_stream << "[OMPT]" << it_min->second << ":0x" << std::setfill('0') << std::setw(16) << std::hex << offset;
            name = name_stream.str();
            
        }
    }

    void OMPT::region_name_pretty(std::string &name)
    {
        const std::string left_tok = "[OMPT]";
        const std::string right_tok = ":0x";
        size_t obj_off = name.find(left_tok);
        size_t addr_off = name.rfind(right_tok);
        if (obj_off == 0 && addr_off != std::string::npos) {
            std::string obj_name = name.substr(left_tok.length(), addr_off - left_tok.length());
            std::string addr_str = name.substr(addr_off + right_tok.length());
            size_t addr;
            int num_scan = sscanf(addr_str.c_str(), "%zx", &addr);
            if (num_scan == 1) {
                std::ostringstream cmd_str;
                cmd_str << "exec bash -c '"
                        << "object=" << obj_name << "; "
                        << "addr=" << addr << "; "
                        << "tmp_file=/tmp/geopm-$$; "
                        << "readelf -h $object | grep \"Type:\" | grep -q EXEC; "
                        << "if [ $? -eq 0 ]; then "
                        << "    offset=$(readelf -l $object | grep \"LOAD           0x0000000000000000\" | awk \"{print \\$3}\"); "
                        << "else "
                        << "    offset=0x0; "
                        << "fi; "
                        << "offset=$(($offset + $addr)); "
                        << "offset=$(printf \"%016zx\" $offset); "
                        << "nm --demangle $object | egrep \" t | T \" | awk \"{print \\$1, \\$3}\"> $tmp_file; "
                        << "echo $offset \"ZZZZZZZZZZ_FUNC_OFFSET\" >> $tmp_file; "
                        << "sort $tmp_file | grep -B 1 \"ZZZZZZZZZZ_FUNC_OFFSET\" | head -n 1 | sed \"s|^[0-9a-f]* ||\"; "
                        << "rm $tmp_file"
                        << "'";

                char buffer[NAME_MAX] = "FUNCTION_UNKNOWN";
                FILE *pid;
                int err = geopm_sched_popen(cmd_str.str().c_str(), &pid);
                if (!err) {
                    size_t num_read = fread(buffer, 1, NAME_MAX - 1, pid);
                    if (num_read) {
                        buffer[num_read -1] = '\0'; // Replace new line with null terminator
                    }
                    (void)pclose(pid);
                    size_t last_slash = obj_name.rfind('/');
                    if (last_slash != std::string::npos) {
                        obj_name = obj_name.substr(last_slash + 1);
                    }
                }
                name = "[OMPT]" + obj_name + ":" + std::string(buffer) + "_" + std::to_string(addr);
            }
        }
    }

    void ompt_pretty_name(std::string &name)
    {
        ompt().region_name_pretty(name);
    }
}


extern "C"
{
    static void *g_curr_parallel_function = NULL;
    static ompt_parallel_id_t g_curr_parallel_id;
    static uint64_t g_curr_region_id = GEOPM_REGION_HASH_UNMARKED;

    struct timeval start_t, end_t;

    std::map<uint64_t, int> regionmap;
    int region_idx;
    static void on_ompt_event_parallel_begin(ompt_task_id_t parent_task_id,
                                             ompt_frame_t *parent_task_frame,
                                             ompt_parallel_id_t parallel_id,
                                             uint32_t requested_team_size,
                                             void *parallel_function,
                                             ompt_invoker_t invoker)
    {
        if (geopm_is_pmpi_prof_enabled() &&
            g_curr_parallel_function != parallel_function) {
            g_curr_parallel_function = parallel_function;
            g_curr_parallel_id = parallel_id;
            g_curr_region_id = geopm::ompt().region_id(parallel_function);
        }
        if (g_curr_region_id != GEOPM_REGION_HASH_UNMARKED) {
            geopm_prof_enter(g_curr_region_id);
        }
        int m_shm_rank = geopm::ompt().m_shm_rank;
        //struct app_config *conf1 = (struct app_config *)geopm::ompt().m_app_ctl_shmem->pointer();
        //printf("I read: %c\n", *(char *)geopm::ompt().get_shmem_ptr().pointer());

        /* Need to reduce the structure size into just per-process thread count and powercap */
        int iter;
//        printf("From OMP, .m_shm_rank:%d, epochID:%ld\n", geopm::ompt().m_shm_rank, geopm::ompt().m_conf->epochid[geopm::ompt().m_shm_rank]);
        unsigned long epochid = geopm::ompt().m_conf->epochid[m_shm_rank];
        int configepochs = geopm::ompt().m_conf->configepochs[m_shm_rank];
        if(epochid < configepochs) {
            int nthread = geopm::ompt().m_conf->config[m_shm_rank].threads[geopm::ompt().m_conf->epochid[m_shm_rank]];
            printf("Rank:%d Epoch:%ld\tPcap:%lf, threads:%d\n", m_shm_rank, epochid,
                                    geopm::ompt().m_conf->config[m_shm_rank].pcap[epochid],
                                    nthread);

            //Apply the number of threads from the power balancing plugin
            omp_set_num_threads(nthread);
//            printf("From OMP, got thread: %d\n", nthread);
        } else {
//            printf("From OMP, Out of config explore, threads: %d\n", geopm::ompt().m_conf->thread[geopm::ompt().m_shm_rank]);
//            omp_set_num_threads(geopm::ompt().m_conf->thread[geopm::ompt().m_shm_rank]);
        }

        //Output region ID, number of threads for the region
        //
//        std::ofstream ofile;
//        std::string fcinit = "configselect_" + std::to_string(getpid());
//        ofile.open(fcinit, std::ios::out);
//        for(iter = 0; iter < NUMTHREADS * NUMPCAPS; iter++) {
//            ofile << "Thread: " << geopm::ompt().m_conf->config[geopm::ompt().m_shm_rank].threads[iter] 
//                  << ", Pcap: " << geopm::ompt().m_conf->config[geopm::ompt().m_shm_rank].pcap[iter] << "\n";
//        }
//        ofile.close();
    }

    static void on_ompt_event_parallel_end(ompt_parallel_id_t parallel_id,
                                           ompt_task_id_t task_id,
                                           ompt_invoker_t invoker)
    {
        if (geopm_is_pmpi_prof_enabled() &&
            g_curr_region_id != GEOPM_REGION_HASH_UNMARKED &&
            g_curr_parallel_id == parallel_id) {
            geopm_prof_exit(g_curr_region_id);
        }

        //Stop timer
        gettimeofday(&end_t, NULL);
        double elapsedTime = (end_t.tv_sec - start_t.tv_sec) * 1000.0;      // sec to ms
        elapsedTime += (end_t.tv_usec - start_t.tv_usec) / 1000.0;

        /* If you're in the configuration exploration phase, save power usage and elapsed time */
        int m_shm_rank = geopm::ompt().m_shm_rank;
        int epochid = geopm::ompt().m_conf->epochid[m_shm_rank];
        int configepochs = geopm::ompt().m_conf->configepochs[m_shm_rank];
        if(geopm::ompt().m_conf->epochid[m_shm_rank] < 
                  geopm::ompt().m_conf->configepochs[m_shm_rank]) {

            auto it = regionmap.find(g_curr_region_id);
            if(regionmap.end() != it) {
                /* Region found, use that region ID for configuration lookup */
                region_idx = it->second;
            } else {
                region_idx = geopm::ompt().m_region_idx;
                /* Unseen region, append profiled information to the region vector */
//                geopm::ompt().m_conf->region[m_shm_rank][region_idx].elapsed_time[epochid] = elapsedTime;
//                geopm::ompt().m_conf->region[m_shm_rank][region_idx].Pkg_watts[epochid]    = elapsedTime;
               
                geopm::ompt().m_region_idx++;
            } 
        } else {
            /* Do nothing */
        }
    }

    void ompt_initialize(ompt_function_lookup_t lookup,
                         const char *runtime_version,
                         unsigned int ompt_version)
    {
        ompt_set_callback_t ompt_set_callback = (ompt_set_callback_t) lookup("ompt_set_callback");
        ompt_set_callback(ompt_event_parallel_begin, (ompt_callback_t) &on_ompt_event_parallel_begin);
        ompt_set_callback(ompt_event_parallel_end, (ompt_callback_t) &on_ompt_event_parallel_end);

    }

    ompt_initialize_t ompt_tool()
    {
        return &ompt_initialize;
    }
}

#endif // GEOPM_ENABLE_OMPT defined
