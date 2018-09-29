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

#include "OCCPlatform.hpp"
#include "geopm_message.h"
#include "geopm_time.h"
#include "config.h"
#include "Exception.hpp"

/* FIXME: This is hard coded for now, but
 * we should see whether there is a way
 * to find programatically out number 
 * size of L3 cache line in bytes
 */

#define L3_CACHE_LINE_SIZE 128

namespace geopm
{

    OCCPlatform::OCCPlatform()
        : Platform(GEOPM_CONTROL_DOMAIN_POWER),
        m_description("occ")
    {
        geopm_time(&m_prev_sample_time);
    }

    OCCPlatform::~OCCPlatform() {
    }

    int OCCPlatform::control_domain() {
        return GEOPM_CONTROL_DOMAIN_POWER;
    }

    bool OCCPlatform::model_supported(int platform_id, const std::string &description) const {
        return ((platform_id == POWER9_PLATFORM_ID) && (description == m_description));
    }

    void OCCPlatform::initialize(void) {
        /// here will go initialisations once we implement
        /// interface with OCC
        printf(" OCC: Setup counters\n");
        m_num_counter_domain = m_imp->num_domain(m_imp->performance_counter_domain());
        m_num_energy_domain = m_imp->num_domain(m_imp->power_control_domain());
        m_batch_desc.resize(m_num_energy_domain * m_imp->num_energy_signal() + m_num_counter_domain * m_imp->num_counter_signal());

        printf(" OCC: Setup domains\n");
        int count = 0;
        int energy_domain = m_imp->power_control_domain();
        int counter_domain = m_imp->performance_counter_domain();
        int counter_domain_per_energy_domain = m_num_counter_domain / m_num_energy_domain;

        for(int i = 0; i < m_num_energy_domain; ++i) {
            m_batch_desc[count].device_type = energy_domain;
            m_batch_desc[count].device_index = i;
            m_batch_desc[count].signal_type = GEOPM_TELEMETRY_TYPE_PKG_ENERGY;
            m_batch_desc[count].value = 0;
            ++count;

            m_batch_desc[count].device_type = energy_domain;
            m_batch_desc[count].device_index = i;
            m_batch_desc[count].signal_type = GEOPM_TELEMETRY_TYPE_DRAM_ENERGY;
            m_batch_desc[count].value = 0;
            ++count;

            printf(" OCC: Initialize counter per domain\n");
            for(int j = i * counter_domain_per_energy_domain;
                    j < i * counter_domain_per_energy_domain + counter_domain_per_energy_domain;
                    ++j) {
                m_batch_desc[count].device_type = counter_domain;
                m_batch_desc[count].device_index = j;
                m_batch_desc[count].signal_type = GEOPM_TELEMETRY_TYPE_FREQUENCY;
                m_batch_desc[count].value = 0;
                ++count;

                m_batch_desc[count].device_type = counter_domain;
                m_batch_desc[count].device_index = j;
                m_batch_desc[count].signal_type = GEOPM_TELEMETRY_TYPE_INST_RETIRED;
                m_batch_desc[count].value = 0;
                ++count;

                m_batch_desc[count].device_type = counter_domain;
                m_batch_desc[count].device_index = j;
                m_batch_desc[count].signal_type = GEOPM_TELEMETRY_TYPE_CLK_UNHALTED_CORE;
                m_batch_desc[count].value = 0;
                ++count;
                m_batch_desc[count].device_type = counter_domain;
                m_batch_desc[count].device_index = j;
                m_batch_desc[count].signal_type = GEOPM_TELEMETRY_TYPE_CLK_UNHALTED_REF;
                m_batch_desc[count].value = 0;
                ++count;
                m_batch_desc[count].device_type = counter_domain;
                m_batch_desc[count].device_index = j;
                m_batch_desc[count].signal_type = GEOPM_TELEMETRY_TYPE_READ_BANDWIDTH;
                m_batch_desc[count].value = 0;
                ++count;
                m_batch_desc[count].device_type = counter_domain;
                m_batch_desc[count].device_index = j;
                m_batch_desc[count].signal_type = GEOPM_TELEMETRY_TYPE_GPU_ENERGY;
                m_batch_desc[count].value = 0;
                ++count;
            }
        }

        printf(" OCC: Read batch signal\n");
        m_imp->batch_read_signal(m_batch_desc, true);    
        printf(" OCC: End of initialize\n");
    }

    size_t OCCPlatform::capacity(void) {
        /// number of signals that will be returned
        /// when sample method is called

        /// for now we just say 1 (total energy)
        /// maybe we could use results from implementation class (m_imp)
        return m_imp->num_domain(m_imp->power_control_domain()) * (m_imp->num_energy_signal() + m_imp->num_counter_signal());
    }

    void OCCPlatform::bound(double &upper_bound, double &lower_bound) {

        //!@todo: Change this to query power domain values 
        // from the OPAL interface.
        // For now, setting fixed values for the 
        // interface.
        upper_bound = 3050;
        lower_bound = 500;
    }

    void OCCPlatform::sample(std::vector<struct geopm_msr_message_s> &occ_values) {
        struct geopm_time_s time;

        m_imp->batch_read_signal(m_batch_desc, false);
        geopm_time(&time);
        double time_diff = geopm_time_diff(&m_prev_sample_time, &time);

        int count = 0;
        int signal_index = 0;
        int energy_domain = GEOPM_DOMAIN_PACKAGE;

        for(int i = 0; i < m_num_energy_domain; i++) {
            occ_values[count].domain_type = energy_domain;
            occ_values[count].domain_index = i;
            occ_values[count].timestamp = time;
            occ_values[count].signal_type = GEOPM_TELEMETRY_TYPE_PKG_ENERGY;
            occ_values[count].signal += m_batch_desc[signal_index++].value * time_diff;

            count++;
            occ_values[count].domain_type = energy_domain;
            occ_values[count].domain_index = i;
            occ_values[count].timestamp = time;
            occ_values[count].signal_type = GEOPM_TELEMETRY_TYPE_DRAM_ENERGY;
            occ_values[count].signal += m_batch_desc[signal_index++].value * time_diff;
            count++;

            occ_values[count].domain_type = energy_domain;
            occ_values[count].domain_index = i;
            occ_values[count].timestamp = time;
            occ_values[count].signal_type = GEOPM_TELEMETRY_TYPE_FREQUENCY;
            occ_values[count].signal = m_batch_desc[signal_index++].value;
            count++;

            occ_values[count].domain_type = energy_domain;
            occ_values[count].domain_index = i;
            occ_values[count].timestamp = time;
            occ_values[count].signal_type = GEOPM_TELEMETRY_TYPE_INST_RETIRED;
            occ_values[count].signal = m_batch_desc[signal_index++].value;
            count++;

            occ_values[count].domain_type = energy_domain;
            occ_values[count].domain_index = i;
            occ_values[count].timestamp = time;
            occ_values[count].signal_type = GEOPM_TELEMETRY_TYPE_CLK_UNHALTED_CORE;
            occ_values[count].signal = m_batch_desc[signal_index++].value;
            count++;

            occ_values[count].domain_type = energy_domain;
            occ_values[count].domain_index = i;
            occ_values[count].timestamp = time;
            occ_values[count].signal_type = GEOPM_TELEMETRY_TYPE_CLK_UNHALTED_REF;
            occ_values[count].signal = m_batch_desc[signal_index++].value;
            count++;

            occ_values[count].domain_type = energy_domain;
            occ_values[count].domain_index = i;
            occ_values[count].timestamp = time;
            occ_values[count].signal_type = GEOPM_TELEMETRY_TYPE_READ_BANDWIDTH;
            /* TODO: Check if this formula is correct for memory bandwith */
            double mbytes_per_s = 
                (int)ceil((double)m_batch_desc[signal_index++].value * L3_CACHE_LINE_SIZE * (1e-6) / time_diff);

            occ_values[count].signal = mbytes_per_s;
            count++;    

            occ_values[count].domain_type = energy_domain;
            occ_values[count].domain_index = i;
            occ_values[count].timestamp = time;
            occ_values[count].signal_type = GEOPM_TELEMETRY_TYPE_GPU_ENERGY;
            occ_values[count].signal += m_batch_desc[signal_index++].value * time_diff;
            count++;    
        }   

        m_prev_sample_time = time;
    }

    void OCCPlatform::enforce_policy(uint64_t region_id, IPolicy &policy) const {
        // this funnction is used to enforce policy for a given region
        // it should use the method write_control() from m_imp
        int control_type;
        std::vector<double> target(m_num_energy_domain);
        policy.target(region_id, target);

        if ((m_control_domain_type == GEOPM_CONTROL_DOMAIN_POWER) &&
            (m_num_energy_domain == (int)target.size())) {
            control_type = GEOPM_TELEMETRY_TYPE_PKG_ENERGY;
            for (int i = 0; i < m_num_energy_domain; ++i) {
                m_imp->write_control(m_imp->power_control_domain(), i, control_type, target[i]);
            }
        }
        else {
            if (m_control_domain_type != GEOPM_CONTROL_DOMAIN_POWER) {
                throw geopm::Exception("OCCPlatform::enforce_policy: OCCPlatform Only handles power control domains", GEOPM_ERROR_INVALID, __FILE__, __LINE__);
            }
            if (m_num_energy_domain != (int)target.size()) {
                throw geopm::Exception("OCCPlatform::enforce_policy: Policy size does not match domains of control", GEOPM_ERROR_INVALID, __FILE__, __LINE__);
            }
        }
    }

} // geopm
