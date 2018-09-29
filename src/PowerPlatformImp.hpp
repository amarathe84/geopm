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

#ifndef POWERPLATFORMIMP_HPP_INCLUDE
#define POWERPLATFORMIMP_HPP_INCLUDE

#include "PlatformImp.hpp"
#include <nvml.h>

#ifndef NAME_MAX
#define NAME_MAX 1024
#endif

#define MAX_OCCS            8
#define MAX_CHARS_SENSOR_NAME       16
#define MAX_CHARS_SENSOR_UNIT       4

#define HYPERTHREADS 4
struct occ_sensor_record {
    uint16_t gsid;
    uint64_t timestamp;
    uint16_t sample;
    uint16_t sample_min;
    uint16_t sample_max;
    uint16_t csm_min;
    uint16_t csm_max;
    uint16_t profiler_min;
    uint16_t profiler_max;
    uint16_t job_scheduler_min;
    uint16_t job_scheduler_max;
    uint64_t accumulator;
    uint32_t update_tag;
    uint8_t pad[8];
} __attribute__((__packed__));

struct occ_sensor_counter {
    uint16_t gsid;
    uint64_t timestamp;
    uint64_t accumulator;
    uint8_t sample;
    uint8_t pad[5];
} __attribute__((__packed__));


struct occ_sensor_name {
    char name[MAX_CHARS_SENSOR_NAME];
    char units[MAX_CHARS_SENSOR_UNIT];
    uint16_t gsid;
    uint32_t freq;
    uint32_t scale_factor;
    uint16_t type;
    uint16_t location;
    uint8_t structure_type;
    uint32_t reading_offset;
    uint8_t sensor_data;
    uint8_t pad[8];
} __attribute__((__packed__));

struct occ_sensor_data_header {
    uint8_t valid;
    uint8_t version;
    uint16_t nr_sensors;
    uint8_t reading_version;
    uint8_t pad[3];
    uint32_t names_offset;
    uint8_t names_version;
    uint8_t name_length;
    uint16_t reserved;
    uint32_t reading_ping_offset;
    uint32_t reading_pong_offset;
} __attribute__((__packed__));

enum sensor_attr {
    SENSOR_SAMPLE,
    SENSOR_ACCUMULATOR,
};

enum occ_sensor_type {
    OCC_SENSOR_TYPE_GENERIC     = 0x0001,
    OCC_SENSOR_TYPE_CURRENT     = 0x0002,
    OCC_SENSOR_TYPE_VOLTAGE     = 0x0004,
    OCC_SENSOR_TYPE_TEMPERATURE = 0x0008,
    OCC_SENSOR_TYPE_UTILIZATION = 0x0010,
    OCC_SENSOR_TYPE_TIME        = 0x0020,
    OCC_SENSOR_TYPE_FREQUENCY   = 0x0040,
    OCC_SENSOR_TYPE_POWER       = 0x0080,
    OCC_SENSOR_TYPE_PERFORMANCE = 0x0200,
};

enum occ_sensor_location {
    OCC_SENSOR_LOC_SYSTEM       = 0x0001,
    OCC_SENSOR_LOC_PROCESSOR    = 0x0002,
    OCC_SENSOR_LOC_PARTITION    = 0x0004,
    OCC_SENSOR_LOC_MEMORY       = 0x0008,
    OCC_SENSOR_LOC_VRM      = 0x0010,
    OCC_SENSOR_LOC_OCC      = 0x0020,
    OCC_SENSOR_LOC_CORE     = 0x0040,
    OCC_SENSOR_LOC_GPU      = 0x0080,
    OCC_SENSOR_LOC_QUAD     = 0x0100,
};

enum sensor_struct_type {
    OCC_SENSOR_READING_FULL     = 0x01,
    OCC_SENSOR_READING_COUNTER  = 0x02,
};

#define TO_FP(f)    ((f >> 8) * pow(10, ((int8_t)(f & 0xFF))))

namespace geopm
{
  /// @brief This class provides a base class for Power processor line
  class PowerPlatformImp : public PlatformImp {
  public:
    /// @brief Default constructor.
    PowerPlatformImp();
    /// @brief Copy constructor.
    PowerPlatformImp(const PowerPlatformImp &other);
    /// @brief Default destructor.
    virtual ~PowerPlatformImp();
    
    virtual bool model_supported(int platform_id);
    virtual std::string platform_name(void);
    virtual int power_control_domain(void) const;
    virtual int frequency_control_domain(void) const;
    virtual int performance_counter_domain(void) const;
    virtual void bound(int control_type, double &upper_bound, double &lower_bound);
    virtual double throttle_limit_mhz(void) const;
    virtual double read_signal(int device_type, int device_index, int signal_type);
    virtual void batch_read_signal(std::vector<struct geopm_signal_descriptor> &signal_desc, bool is_changed);
    virtual void write_control(int device_type, int device_index, int signal_type, double value);
    virtual void msr_initialize(void);
    virtual void msr_reset(void);

    virtual bool is_updated(void);

    static int platform_id(void);
    unsigned long read_sensor(struct occ_sensor_data_header *hb, uint32_t offset, int attr);
    unsigned long read_counter(struct occ_sensor_data_header *hb, uint32_t offset);
    double get_sensor_data(int chipID, char *sensor_name);

    double occ_energy_read(int idx);
    double occ_cpu_power_read(int idx);
    double occ_mem_power_read(int idx);

  protected:
    const std::string M_MODEL_NAME;
    const int M_PLATFORM_ID;

    std::vector<int> m_occ_file_desc;

    enum {
      M_CLK_UNHALTED_REF,
      M_DATA_FROM_LMEM,
      M_DATA_FROM_RMEM,
      M_INST_RETIRED,
      M_CLK_UNHALTED_CORE
    } m_signal_offset_e;


  private:
    void occ_paths(int chips);
    int occ_open(char* path);
    double occ_read(int idx);

    double cpu_freq_read(int cpuid);

    void pf_event_reset(void);

    /// @brief File path to power-vdd in occ_sensors
    char m_power_path[NAME_MAX];
    /// @brief File path to power-memory in occ_sensors
    char m_memory_path[NAME_MAX];
    // @brief handlers for GPU devices
    // FIXME: it is hard coded for now but information
    // on how many there are should be done in more generic way
    nvmlDevice_t* m_unit_devices_file_desc;
    unsigned int m_total_unit_devices;
    // @brief handlers for CPU frequency
    std::vector<int> m_cpu_freq_file_desc;
    std::vector<int> m_cpu_dvfs_file_desc; 
    int m_cpu_pcap_file_desc;
  };
}

#endif
