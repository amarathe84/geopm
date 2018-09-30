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

#ifndef __OPCODES_H
#define __OPCODES_H

#define __PPC_RA(a)		(((a) & 0x1f) << 16)
#define __PPC_RB(b)		(((b) & 0x1f) << 11)
#define __PPC_XA(a)		((((a) & 0x1f) << 16) | (((a) & 0x20) >> 3))
#define __PPC_XB(b)		((((b) & 0x1f) << 11) | (((b) & 0x20) >> 4))
#define __PPC_XS(s)		((((s) & 0x1f) << 21) | (((s) & 0x20) >> 5))
#define __PPC_XT(s)		__PPC_XS(s)
#define VSX_XX3(t, a, b)	(__PPC_XT(t) | __PPC_XA(a) | __PPC_XB(b))
#define VSX_XX1(s, a, b)	(__PPC_XS(s) | __PPC_RA(a) | __PPC_RB(b))

#define PPC_INST_VPMSUMW	0x10000488
#define PPC_INST_VPMSUMD	0x100004c8
#define PPC_INST_MFVSRD		0x7c000066
#define PPC_INST_MTVSRD		0x7c000166

#define VPMSUMW(t, a, b)	.long PPC_INST_VPMSUMW | VSX_XX3((t), a, b)
#define VPMSUMD(t, a, b)	.long PPC_INST_VPMSUMD | VSX_XX3((t), a, b)
#define MFVRD(a, t)		.long PPC_INST_MFVSRD | VSX_XX1((t)+32, a, 0)
#define MTVRD(t, a)		.long PPC_INST_MTVSRD | VSX_XX1((t)+32, a, 0)

#endif
