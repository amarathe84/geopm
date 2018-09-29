/*
 * Copyright 2017, 2018 Science and Technology Facilities Council (UK)
 * IBM Confidential
 * OCO Source Materials
 * 5747-SM3
 * (c) Copyright IBM Corp. 2017, 2018
 * The source code for this program is not published or otherwise
 * divested of its trade secrets, irrespective of what has
 * been deposited with the U.S. Copyright Office.
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

#include "geopm_arch.h"
#include "PowerPlatformImp.hpp"
#include "geopm_message.h"
#include "geopm_error.h"
#include "Exception.hpp"
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <fcntl.h>

/* !@todo: Query the information from the Control Register
 * as to whether SMT4 or SMT8 mode is enabled.
 * Since this is a controlled setup, and we know that SMT4 
 * is enabled manually, setting hyperthreading information
 * manually.
 */
namespace geopm
{

    static const std::map<std::string, std::pair<off_t, unsigned long> > &power9_hwc_map(void);

    int PowerPlatformImp::platform_id(void) {
        /* Assigning platform ID provided in the P9 documentation */
        return POWER9_PLATFORM_ID;
    }

    PowerPlatformImp::PowerPlatformImp() 
        : PlatformImp(2, 6, 50.0, &(power9_hwc_map()))
          , M_MODEL_NAME("Power9")
          , M_PLATFORM_ID(platform_id())
          , m_total_unit_devices(0) {
          }

    PowerPlatformImp::PowerPlatformImp(const PowerPlatformImp &other) 
        : PlatformImp(other)
          , M_MODEL_NAME(other.M_MODEL_NAME)
          , M_PLATFORM_ID(other.M_PLATFORM_ID)
          , m_total_unit_devices(0) {
          }

    PowerPlatformImp::~PowerPlatformImp() {
        for(int c = 0; c < m_num_logical_cpu; ++c) {
          free(m_pf_event_read_data[c]);
        }
        free(m_pf_event_read_data);

        for(std::vector<int>::iterator it = m_cpu_freq_file_desc.begin();
                it != m_cpu_freq_file_desc.end();
                ++it) {
            close(*it);
            *it = -1;
        }

        for(std::vector<int>::iterator it = m_cpu_dvfs_file_desc.begin();
                it != m_cpu_dvfs_file_desc.end();
                ++it) {
            close(*it);
            *it = -1;
        }
        free(m_unit_devices_file_desc);

        /* shutdown */
        nvmlShutdown();
    }

    bool PowerPlatformImp::model_supported(int platform_id) {
        return (M_PLATFORM_ID == platform_id);
    }

    std::string PowerPlatformImp::platform_name() {
        return M_MODEL_NAME;
    }

    int PowerPlatformImp::power_control_domain(void) const {
        return GEOPM_DOMAIN_PACKAGE;
    }

    int PowerPlatformImp::frequency_control_domain(void) const {
        return GEOPM_DOMAIN_PACKAGE_CORE;
    }

    int PowerPlatformImp::performance_counter_domain(void) const {
        return GEOPM_DOMAIN_PACKAGE;
    }

    void PowerPlatformImp::bound(int control_type, double &upper_bound, double &lower_bound) {
        upper_bound = 3050;
        lower_bound = 500;
    }

    double PowerPlatformImp::throttle_limit_mhz(void) const {
        return 0.5; // the same value as it is in KNL implementation
    }

    double PowerPlatformImp::read_signal(int device_type, int device_index, int signal_type) {
        double value = 0.0;

        switch(signal_type) {
            case GEOPM_TELEMETRY_TYPE_PKG_ENERGY:
                value = occ_cpu_power_read(device_index);
                break;

            case GEOPM_TELEMETRY_TYPE_DRAM_ENERGY:
                value = occ_mem_power_read(device_index);
                break;

            case GEOPM_TELEMETRY_TYPE_FREQUENCY:
                {
                    value = 0.0;

                    int cpu_per_socket = m_num_logical_cpu / m_num_package;
                    for(int cpu = device_index * cpu_per_socket;
                            cpu < (device_index + 1) * cpu_per_socket;
                            ++cpu) {
                        value += cpu_freq_read(cpu);
                    }

                    /* Calculate the average frequency of the processor */
                    /* Last value of cpu_per_socket is the largest CPU ID */
                    value /= (double)cpu_per_socket;

                    break;
                }

            case GEOPM_TELEMETRY_TYPE_INST_RETIRED:
                {
                    value = 0;

                    int cpu_per_socket = m_num_logical_cpu / m_num_package;
                    for(int cpu = device_index * cpu_per_socket;
                            cpu < (device_index + 1) * cpu_per_socket;
                            ++cpu) {
                        value += m_pf_event_read_data[cpu][M_INST_RETIRED + 1];
                    }

                    break;
                }

            case GEOPM_TELEMETRY_TYPE_CLK_UNHALTED_CORE: 
                {
                    value = 0;

                    int cpu_per_socket = m_num_logical_cpu / m_num_package;
                    for(int cpu = device_index * cpu_per_socket;
                            cpu < (device_index + 1) * cpu_per_socket;
                            ++cpu) {

                        value += m_pf_event_read_data[cpu][M_CLK_UNHALTED_CORE + 1];
                    }

                    break;
                }

            case GEOPM_TELEMETRY_TYPE_CLK_UNHALTED_REF:
                {
                    value = 0;

                    int cpu_per_socket = m_num_logical_cpu / m_num_package;
                    for(int cpu = device_index * cpu_per_socket;
                            cpu < (device_index + 1) * cpu_per_socket;
                            ++cpu) {

                        value += m_pf_event_read_data[cpu][M_CLK_UNHALTED_REF + 1];
                    }

                    break;
                }

            case GEOPM_TELEMETRY_TYPE_READ_BANDWIDTH:
                {
                    value = 0;
                    int cpu_per_socket = m_num_logical_cpu / m_num_package;
                    for(int cpu = device_index * cpu_per_socket;
                            cpu < (device_index + 1) * cpu_per_socket;
                            ++cpu) {
                        value += m_pf_event_read_data[cpu][M_DATA_FROM_LMEM + 1] +
                            m_pf_event_read_data[cpu][M_DATA_FROM_RMEM + 1];
                    }

                    break;
                }

            case GEOPM_TELEMETRY_TYPE_GPU_ENERGY:
                {
                    /* !@todo: This assumes that the number of GPUs is evenly 
                     * spread between sockets,
                     * but we should look at finding that out
                     * in some programmatic way */
                    value = 0.0;

                    unsigned int power;
                    int gpus_per_socket = m_total_unit_devices / m_num_package;
                    for(int d = device_index * gpus_per_socket;
                            d < (device_index + 1) * gpus_per_socket;
                            ++d) {
                        nvmlDeviceGetPowerUsage(m_unit_devices_file_desc[d], &power);
                        value += (double)power * 0.001;
                    }

                    break;
                }

            default:
                throw geopm::Exception("PowerPlatformImp::read_signal: Invalid signal type", 
                        GEOPM_ERROR_INVALID, 
                        __FILE__, 
                        __LINE__);
                break;
        }
        printf("Read signal: %d, device_index:%d, value:%lf\n", signal_type, device_index, value);

        return value;
    }

    void PowerPlatformImp::batch_read_signal(std::vector<struct geopm_signal_descriptor> &signal_desc, bool is_changed) {
        /* !@todo: Perform reads in batch mode */ 

        /* Obtain results from performance counters */
        for(int cpu = 0; cpu < m_num_logical_cpu; ++cpu)
            pf_event_read(cpu);

        for(auto it = signal_desc.begin(); it != signal_desc.end(); ++it) {
            (*it).value = read_signal((*it).device_type, (*it).device_index, (*it).signal_type);
        }

    }

    void PowerPlatformImp::write_control(int device_type, int device_index, int signal_type, double value) {
        /* !@todo: Query this information from the available frequencies
         *         once a consistent interface to frequency is available
         */
        long av_freq[] = {3000000,2983000,2966000,2950000,2933000,2916000,2900000,2883000,2866000,2850000,2833000,2816000,2800000,2783000,2766000,2750000,2733000,2716000,2700000,2683000,2666000,2650000,2633000,2616000,2600000,2583000,2566000,2550000,2533000,2516000,2500000,2483000,2466000,2450000,2433000,2416000,2400000,2383000,2366000,2350000,2333000,2316000,2300000}; 

        /* Get modeled frequency based on the target power limit */
        long target_freq = (47835.22f)*value + 1145.4f;
       
        /* Get the frequency index closest to the modeled value */
        /* Default frequency is maximum */
        int freqiter=0;
        for(freqiter = 1; freqiter < 42; freqiter++) {
            if(av_freq[freqiter-1] > target_freq) {
                break;
            } 
        } 

        int err;
        /* Set power limit based on the provided value */
        for(std::vector<int>::iterator it = m_cpu_dvfs_file_desc.begin();
                it != m_cpu_dvfs_file_desc.end();
                ++it) {
            err = write(*it, &av_freq[freqiter], sizeof(long));
            if(err < 0) { 
                throw Exception("Could not set frequency level\n",
                        GEOPM_ERROR_MSR_OPEN,
                        __FILE__,
                        __LINE__);
            }
        }
    }

    void PowerPlatformImp::occ_paths(int chip) {
        struct stat s;
        int err;

        snprintf(m_power_path, NAME_MAX, "/sys/firmware/opal/exports/occ_inband_sensors");
        err = stat(m_power_path, &s);
        if(err == 0) {
            snprintf(m_memory_path, NAME_MAX, "/sys/firmware/opal/exports/occ_inband_sensors");
            err = stat(m_memory_path, &s);
            if(err == 0) 
                return;
        }

        throw Exception("no power-vdd or power-memory in occ_sensors directory", 
                GEOPM_ERROR_MSR_OPEN,
                __FILE__,
                __LINE__);
    }

    int PowerPlatformImp::occ_open(char* path) {
        int fd;

        printf("---- here 10\n");
        fd = open(path, O_RDONLY);
        //report errors
        if (fd < 0) {
            char error_string[NAME_MAX];
            if (errno == ENXIO || errno == ENOENT) {
                snprintf(error_string, NAME_MAX, "device %s does not exist", path);
            }
            else if (errno == EPERM || errno == EACCES) {
                snprintf(error_string, NAME_MAX, "permission denied opening device %s", path);
            }
            else {
                snprintf(error_string, NAME_MAX, "system error opening cpu device %s", path);
            }
            throw Exception(error_string, GEOPM_ERROR_MSR_OPEN, __FILE__, __LINE__);

            return -1;
        }

        return fd;
    }

#define OCC_SENSOR_DATA_BLOCK_OFFSET    0x00580000
#define OCC_SENSOR_DATA_BLOCK_SIZE  0x00025800

    unsigned long PowerPlatformImp::read_sensor(
        struct occ_sensor_data_header *hb, 
        uint32_t offset,
        int attr)
    {
        struct occ_sensor_record *sping, *spong;
        struct occ_sensor_record *sensor = NULL;
        uint8_t *ping, *pong;
    
        ping = (uint8_t *)((uint64_t)hb + be32toh(hb->reading_ping_offset));
        pong = (uint8_t *)((uint64_t)hb + be32toh(hb->reading_pong_offset));
        sping = (struct occ_sensor_record *)((uint64_t)ping + offset);
        spong = (struct occ_sensor_record *)((uint64_t)pong + offset);
    
        if (*ping && *pong) {
            if (be64toh(sping->timestamp) > be64toh(spong->timestamp))
                sensor = sping;
            else
                sensor = spong;
        } else if (*ping && !*pong) {
            sensor = sping;
        } else if (!*ping && *pong) {
            sensor = spong;
        } else if (!*ping && !*pong) {
            return 0;
        }
    
        switch (attr) {
        case SENSOR_SAMPLE:
            return be16toh(sensor->sample);
        case SENSOR_ACCUMULATOR:
            return be64toh(sensor->accumulator);
        default:
            break;
        }
    
        return 0;
    }

    unsigned long PowerPlatformImp::read_counter(
        struct occ_sensor_data_header *hb, 
        uint32_t offset)
    {
        struct occ_sensor_counter *sping, *spong;
        struct occ_sensor_counter *sensor = NULL;
        uint8_t *ping, *pong;
    
        ping = (uint8_t *)((uint64_t)hb + be32toh(hb->reading_ping_offset));
        pong = (uint8_t *)((uint64_t)hb + be32toh(hb->reading_pong_offset));
        sping = (struct occ_sensor_counter *)((uint64_t)ping + offset);
        spong = (struct occ_sensor_counter *)((uint64_t)pong + offset);
    
        if (*ping && *pong) {
            if (be64toh(sping->timestamp) > be64toh(spong->timestamp))
                sensor = sping;
            else
                sensor = spong;
        } else if (*ping && !*pong) {
            sensor = sping;
        } else if (!*ping && *pong) {
            sensor = spong;
        } else if (!*ping && !*pong) {
            return 0;
        }
    
        return be64toh(sensor->accumulator);
    }
    
    double PowerPlatformImp::get_sensor_data(int chipID, char *sensor_name) {
        int sensorfd, rc, databytes;
        void *readbuff;

        sensorfd = open("/sys/firmware/opal/exports/occ_inband_sensors", O_RDONLY);
        if (0 > sensorfd) {
            throw Exception("Could not open occ_inband_sensors file", 
                    GEOPM_ERROR_MSR_READ, 
                    __FILE__,
                    __LINE__);
        }

        readbuff = malloc(OCC_SENSOR_DATA_BLOCK_SIZE);
        if (!readbuff) {
            throw Exception("Could not allocate sensor buffer", 
                    GEOPM_ERROR_MSR_READ, 
                    __FILE__,
                    __LINE__);
        }

        lseek(sensorfd, chipID * OCC_SENSOR_DATA_BLOCK_SIZE, SEEK_CUR);
        for (rc = databytes = 0; databytes < OCC_SENSOR_DATA_BLOCK_SIZE; databytes += rc) {
            rc = read(sensorfd, (char *)readbuff + databytes, OCC_SENSOR_DATA_BLOCK_SIZE - databytes);
            if (!rc || rc < 0)
                break;
        }

        if (databytes != OCC_SENSOR_DATA_BLOCK_SIZE) {
            throw Exception("Could not read sensor data", 
                    GEOPM_ERROR_MSR_READ, 
                    __FILE__,
                    __LINE__);
        }

        struct occ_sensor_data_header *hb;
        struct occ_sensor_name *md;
        int i = 0;

        hb = (struct occ_sensor_data_header *)(uint64_t)readbuff;
        md = (struct occ_sensor_name *)((uint64_t)hb +
                be32toh(hb->names_offset));

        for (i = 0; i < be16toh(hb->nr_sensors); i++) {
            uint32_t offset =  be32toh(md[i].reading_offset);
            uint32_t scale = be32toh(md[i].scale_factor);
            uint64_t sample;

            if (be16toh(md[i].type) ==OCC_SENSOR_TYPE_POWER) {
                if (md[i].structure_type == OCC_SENSOR_READING_FULL) {
                    sample = read_sensor(hb, offset, SENSOR_SAMPLE);

                    if(!strcmp(sensor_name, "ENGPROC") && 
                        !strcmp(md[i].name, "PWRPROC")) {
                        uint64_t energy = read_sensor(hb, offset,
                                          SENSOR_ACCUMULATOR);
                        uint32_t freq = be32toh(md[i].freq);

                        return (uint64_t)(energy / TO_FP(freq));
                    } else if(!strcmp(md[i].name, "PWRPROC") || 
                              !strcmp(sensor_name, "PWRMEM")) {
//                        uint64_t energy = read_sensor(hb, offset,
//                                          SENSOR_ACCUMULATOR);
//                        uint32_t freq = be32toh(md[i].freq);

                        return (uint64_t)(sample * TO_FP(scale));
                    } else 
                        return 0;
                }
            } 
        }
        return 0;
    }

    double PowerPlatformImp::occ_energy_read(int idx) {

        return get_sensor_data(idx, (char *) "ENGPROC");
    }

    double PowerPlatformImp::occ_cpu_power_read(int idx) {
        return get_sensor_data(idx, (char *) "PWRPROC");
    }

    double PowerPlatformImp::occ_mem_power_read(int idx) {
        return get_sensor_data(idx, (char *) "PWRMEM");
    }

    double PowerPlatformImp::cpu_freq_read(int cpuid) {
        double value = 0.0;

        char file_text[BUFSIZ];
        pread(m_cpu_freq_file_desc[cpuid], &file_text[0], sizeof(file_text), 0);

        std::string s(file_text);
        value = atof(s.c_str()) / 1e6; /* in GHz */

        return value;
    }

    bool PowerPlatformImp::is_updated(void) {
        printf("here 6\n");
        uint64_t curr_value = (uint64_t)occ_energy_read(0);
        bool result = (m_trigger_value && curr_value != m_trigger_value);
        m_trigger_value = curr_value;

        return result;
    }

    void PowerPlatformImp::msr_initialize() {
        /* We need to follow naming conventions of Intel GEOPM to
         * support the control and telemetry register interface
         * provided by IBM Power9 platform 
         */

        /* Frequency information */
        char cpu_freq_path[NAME_MAX];
        char cpu_dvfs_path[NAME_MAX];

        /* !@todo: Change to query HT from the Control Register */
        int step = 1;
        if(m_num_cpu_per_core == 1)
            step = HYPERTHREADS;
        printf("Power9:Counter Initialize: num logical CPU:%d\n", m_num_logical_cpu);
        for(int c = 0; c < m_num_logical_cpu; c+=4) {
            struct stat s;
            int fd = -1;
            int dvfsfd = -1;

            snprintf(cpu_freq_path, NAME_MAX, "/sys/devices/system/cpu/cpufreq/policy%d/scaling_cur_freq", c);
            int err = stat(cpu_freq_path, &s);
            if(err == 0) 
                fd = open(cpu_freq_path, O_RDONLY);

            m_cpu_freq_file_desc.push_back(fd);

            snprintf(cpu_dvfs_path, NAME_MAX, "/sys/devices/system/cpu/cpufreq/policy%d/scaling_setspeed", c);
            err = stat(cpu_dvfs_path, &s);
            if(err == 0) 
                dvfsfd = open(cpu_dvfs_path, O_RDONLY);

            m_cpu_dvfs_file_desc.push_back(dvfsfd);
        }
        
        /* Initialize OPAL power cap interface */
        int pcapfd;
        char pcap_path[NAME_MAX];
        snprintf(pcap_path, NAME_MAX, "/sys/firmware/opal/powercap/system-powercap/powercap-current");
        struct stat s;
        int err = stat(pcap_path,  &s);
        if(err != 0) 
            throw Exception("no OPAL power capping interface found",
                    GEOPM_ERROR_MSR_OPEN,
                    __FILE__,
                    __LINE__);

        pcapfd = open(pcap_path, O_WRONLY);
        m_cpu_pcap_file_desc = pcapfd;

        printf("Power9: Counter Initialize: OPAL Power cap control initialized\n");

        /* Initialize GPU reading */
        nvmlReturn_t result;
        result = nvmlInit();
        printf("Power9:Counter Initialize: NVML initialized\n");
        if (result != NVML_SUCCESS) {
            /* !@todo: Exit gracefully if no GPUs present */ 
            throw Exception("PowerPlatformImp::initialize: Failed to initialize NVML\n",
                    GEOPM_ERROR_RUNTIME, 
                    __FILE__, 
                    __LINE__);
        }

        nvmlDeviceGetCount(&m_total_unit_devices);
        m_unit_devices_file_desc = (nvmlDevice_t*) malloc(sizeof(nvmlDevice_t) * m_total_unit_devices);

        printf("Power9:Counter Initialize: NVML device allocated\n"); 
        /* get handles to all devices */
        for(unsigned int d = 0; d < m_total_unit_devices; ++d) {
            int power;
            char msg[128];

            result = nvmlDeviceGetHandleByIndex(d, &m_unit_devices_file_desc[d]);
            if (result != NVML_SUCCESS) {
                sprintf(msg, "PowerPlatformImp::initialize: Failed to get handle for device %d: %s\n", d, nvmlErrorString(result));
                throw Exception(msg,
                        GEOPM_ERROR_RUNTIME, 
                        __FILE__, 
                        __LINE__);
            }

            /* check to see whether we can read power */
            result = nvmlDeviceGetPowerUsage(m_unit_devices_file_desc[d], (unsigned int *)&power);
            if (result != NVML_SUCCESS) {
                sprintf(msg, "PowerPlatformImp::initialize: Failed to read power on device %d: %s\n", d, nvmlErrorString(result));
                throw Exception(msg,
                        GEOPM_ERROR_RUNTIME, 
                        __FILE__, 
                        __LINE__);
            }
        }
        printf("Power9:Counter Initialize: NVML device initialized\n"); 
        m_pf_event_read_data = (uint64_t**)malloc(m_num_logical_cpu * sizeof(uint64_t*));

        for(int c = 0; c < m_num_logical_cpu; c++) {
            pf_event_open(c*step);
            m_pf_event_read_data[c] = (uint64_t*) malloc(pf_event_read_data_size());
        }


        /* Enable all hardware counters */
        for(int c = 0; c < m_num_logical_cpu; ++c) {
            ioctl(m_cpu_file_desc[c], PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);
        }
        printf("Power9:Counter Initialize: Performance events initialized\n");
    }

    void PowerPlatformImp::pf_event_reset() {
        for(int i = 0; i < m_num_logical_cpu; ++i) {
            ioctl(m_cpu_file_desc[i], PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP);
        }
    }

    void PowerPlatformImp::msr_reset() {
        pf_event_reset();
    }

    static const std::map<std::string, std::pair<off_t, unsigned long> > &power9_hwc_map(void) {
        static const std::map<std::string, std::pair<off_t, unsigned long> > r_map({
                { "PM_CYC",            {0x1e,    0x00}},
                { "PM_DATA_FROM_LMEM", {0x2c048, 0x0}},
                { "PM_DATA_FROM_RMEM", {0x3c04a, 0x0}},
                { "PM_RUN_INST_CMPL",  {0x500fa, 0x0}},
                { "PM_RUN_CYC",        {0x600f4, 0x0}}
                });

        return r_map;
    }


}
