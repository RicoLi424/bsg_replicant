// Copyright (c) 2019, University of Washington All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
// Redistributions of source code must retain the above copyright notice, this list
// of conditions and the following disclaimer.
//
// Redistributions in binary form must reproduce the above copyright notice, this
// list of conditions and the following disclaimer in the documentation and/or
// other materials provided with the distribution.
//
// Neither the name of the copyright holder nor the names of its contributors may
// be used to endorse or promote products derived from this software without
// specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
// ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
// ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#include <bsg_manycore_coordinate.h>
#include <bsg_manycore_tile.h>
#include <bsg_manycore_vcache.h>
#include <bsg_manycore_cuda.h>
#include <bsg_manycore_printing.h>
#include <bsg_manycore_config_pod.h>
#include <cl_manycore_regression.h>
#include "bsg_mem_dma.hpp"
#include <inttypes.h>
#include <vector>
#include <string>
#include <sstream>


///////////////////////////////////////////////////////////////////////////////
// This tests checks that the software defined map from NPA <-> DRAM         //
// matches the mapping defined in the hardware.                              //
//                                                                           //
// To do this, we iterate over each address bit, for each bank and each pod. //
// We write the NPA to the address using DMA and then read it bank           //
// using the on-chip network.                                                //
///////////////////////////////////////////////////////////////////////////////


#define bitidx_lo 7
#define bitidx_hi 24

int test_dma (int argc, char **argv) {
    hb_mc_manycore_t mc;
    BSG_CUDA_CALL(hb_mc_manycore_init(&mc, "test_dma", 0));

    const hb_mc_config_t *cfg = hb_mc_manycore_get_config(&mc);

    // do this foreach pod
    hb_mc_coordinate_t pod;
    hb_mc_config_foreach_pod(pod, cfg)
    {
        // iterate over each bank
        hb_mc_coordinate_t bank;
        hb_mc_config_pod_foreach_dram(bank, pod, cfg)
        {
            unsigned bitidx;
            for (bitidx = bitidx_lo; bitidx < bitidx_hi; bitidx++) {
                // setup address
                hb_mc_npa_t addr;
                addr.x   = bank.x;
                addr.y   = bank.y;
                addr.epa = 1 << bitidx;

                // set data = address
                hb_mc_npa_t data_i;
                data_i.x = addr.x;
                data_i.y = addr.y;
                data_i.epa = addr.epa;

                // dma write
                BSG_CUDA_CALL(hb_mc_manycore_dma_write_no_cache_ainv(&mc, &addr, &data_i, sizeof(data_i)));
            }
        }

        hb_mc_config_pod_foreach_dram(bank, pod, cfg)
        {
            unsigned bitidx;
            for (bitidx = bitidx_lo; bitidx < bitidx_hi; bitidx++) {
                char addr_str  [256];
                char data_i_str[256];
                char data_o_str[256];

                // setup address
                hb_mc_npa_t addr;
                addr.x   = bank.x;
                addr.y   = bank.y;
                addr.epa = 1 << bitidx;

                // validate
                // network read
                // set data = address
                hb_mc_npa_t data_i;
                data_i.x = addr.x;
                data_i.y = addr.y;
                data_i.epa = addr.epa;

                hb_mc_npa_t data_o;
                BSG_CUDA_CALL(hb_mc_manycore_read_mem(&mc, &addr, &data_o, sizeof(data_o)));

                // print out addresses
                hb_mc_npa_to_string(&addr,   addr_str,   sizeof(addr_str));
                hb_mc_npa_to_string(&data_i, data_i_str, sizeof(data_i_str));
                hb_mc_npa_to_string(&data_o, data_o_str, sizeof(data_o_str));
                bsg_pr_info("[%s] DMA Wrote %s, NW Read %s\n",
                            addr_str, data_i_str, data_o_str);

                if (data_i.x   != data_o.x ||
                    data_i.y   != data_o.y ||
                    data_i.epa != data_o.epa) {
                    bsg_pr_err(BSG_RED("Mismatch") "\n");
                    return HB_MC_FAIL;
                }
            }
        }

    }

    BSG_CUDA_CALL(hb_mc_manycore_exit(&mc));
    return HB_MC_SUCCESS;
}

#ifdef VCS
int vcs_main(int argc, char ** argv) {
#else
int main(int argc, char ** argv) {
#endif

        bsg_pr_test_info("test_rom Regression Test \n");
        int rc = test_dma(argc, argv);
        bsg_pr_test_pass_fail(rc == HB_MC_SUCCESS);
        return rc;
}
