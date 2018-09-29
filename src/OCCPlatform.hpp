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

#ifndef OCCPLATFORM_HPP_INCLUDE
#define OCCPLATFORM_HPP_INCLUDE

#include "Platform.hpp"

namespace geopm
{

  /// @brief This class provides an implementation of concrete platform
  /// suporting processors which use on-chip-controller for power capping.
  /// This includes all IBM Power architectures
  class OCCPlatform : public Platform {
  public:

    /// @brief Default constructor
    OCCPlatform();
    /// @brief Default destructor
    virtual ~OCCPlatform();

    virtual int control_domain(void);
    virtual void initialize(void);
    virtual bool model_supported(int platform_id, const std::string &description) const;
    virtual size_t capacity(void);
    virtual void sample(std::vector<struct geopm_msr_message_s> &msr_values);
    virtual void enforce_policy(uint64_t region_id, IPolicy &policy) const;
    virtual void bound(double &upper_bound, double &lower_bound);

  protected:
    const std::string m_description;
    /// @brief Vector of signal read operations.
    std::vector<struct geopm_signal_descriptor> m_batch_desc;
    struct geopm_time_s m_prev_sample_time;
  };

}

#endif
