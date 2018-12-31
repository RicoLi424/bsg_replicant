// Amazon FPGA Hardware Development Kit
//
// Copyright 2016 Amazon.com, Inc. or its affiliates. All Rights Reserved.
//
// Licensed under the Amazon Software License (the "License"). You may not use
// this file except in compliance with the License. A copy of the License is
// located at
//
//    http://aws.amazon.com/asl/
//
// or in the "license" file accompanying this file. This file is distributed on
// an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, express or
// implied. See the License for the specific language governing permissions and
// limitations under the License.

module cl_fsb

(
   `include "cl_ports.vh" // Fixed port definition

);

`include "cl_common_defines.vh"     // CL Defines for all examples
`include "cl_id_defines.vh"         // Defines for ID0 and ID1 (PCI ID's)
`include "cl_fsb_defines.vh"        // CL Defines for cl_fsb


//---------------------------------------------
// Start with Tie-Off of Unused Interfaces
//---------------------------------------------
// the developer should use the next set of `include
// to properly tie-off any unused interface
// The list is put in the top of the module
// to avoid cases where developer may forget to
// remove it from the end of the file

// `include "unused_flr_template.inc" 
// Function level reset done indication is handled by CL
`include "unused_ddr_a_b_d_template.inc"
`include "unused_ddr_c_template.inc"
// `include "unused_pcim_template.inc"
// `include "unused_dma_pcis_template.inc"
`include "unused_cl_sda_template.inc"
`include "unused_sh_bar1_template.inc"
`include "unused_apppf_irq_template.inc"


`include "bsg_axi_bus_pkg.vh"

//-------------------------------------------------
// global signals
//-------------------------------------------------
logic clk;
assign clk = clk_main_a0;

(* dont_touch = "true" *) logic pipe_rst_n;
logic pre_sync_rst_n;
(* dont_touch = "true" *) logic sync_rst_n;
logic sh_cl_flr_assert_q;


//reset synchronizer

// this is simple pipeline module provided by shell, with FPGA rst optimization --XL
lib_pipe #(.WIDTH(1), .STAGES(4)) PIPE_RST_N (
  .clk    (clk),
  .rst_n  (1'b1),
  .in_bus (rst_main_n),
  .out_bus(pipe_rst_n)
);

always_ff @(negedge pipe_rst_n or posedge clk)
   if (!pipe_rst_n)
   begin
      pre_sync_rst_n <= 0;
      sync_rst_n     <= 0;
   end
   else
   begin
      pre_sync_rst_n <= 1;
      sync_rst_n     <= pre_sync_rst_n;
   end

//FLR response 
always_ff @(negedge sync_rst_n or posedge clk)
   if (!sync_rst_n)
   begin
      sh_cl_flr_assert_q <= 0;
      cl_sh_flr_done     <= 0;
   end
   else
   begin
      sh_cl_flr_assert_q <= sh_cl_flr_assert;
      cl_sh_flr_done     <= sh_cl_flr_assert_q && !cl_sh_flr_done;
   end


// Tie-Off Unused Global Signals
//-------------------------------------------
// The functionality for these signals is TBD so they can can be tied-off.
  assign cl_sh_status0[31:0] = 32'h0;
  assign cl_sh_status1[31:0] = 32'h0;


// ID Values (cl_fsb_defines.vh)
//-------------------------------------------------
  assign cl_sh_id0[31:0] = `CL_SH_ID0;
  assign cl_sh_id1[31:0] = `CL_SH_ID1;



//=================================================
// SH ocl bus multiplexer
//=================================================
(* dont_touch = "true" *) logic axi_crossbar_sync_rst_n;
lib_pipe #(.WIDTH(1), .STAGES(4)) AXI_CROSSBAR_RST_N (
  .clk    (clk)
  ,.rst_n  (1'b1)
  ,.in_bus (sync_rst_n)
  ,.out_bus(axi_crossbar_sync_rst_n)
);

localparam sh_ocl_slot_num_lp = 1;
`declare_bsg_axil_bus_s(sh_ocl_slot_num_lp, bsg_axil_mosi_bus_s, bsg_axil_miso_bus_s);

bsg_axil_mosi_bus_s sh_ocl_mosi_bus, sh_ocl_0_mosi_bus, sh_ocl_1_mosi_bus, sh_ocl_2_mosi_bus, sh_ocl_3_mosi_bus;
bsg_axil_miso_bus_s sh_ocl_miso_bus, sh_ocl_0_miso_bus, sh_ocl_1_miso_bus, sh_ocl_2_miso_bus, sh_ocl_3_miso_bus;


assign sh_ocl_mosi_bus.awvalid      = sh_ocl_awvalid;
assign sh_ocl_mosi_bus.awaddr[31:0] = sh_ocl_awaddr;
assign sh_ocl_mosi_bus.wvalid       = sh_ocl_wvalid;
assign sh_ocl_mosi_bus.wdata[31:0]  = sh_ocl_wdata;
assign sh_ocl_mosi_bus.wstrb[3:0]   = sh_ocl_wstrb;

assign sh_ocl_mosi_bus.bready = sh_ocl_bready;

assign sh_ocl_mosi_bus.arvalid      = sh_ocl_arvalid;
assign sh_ocl_mosi_bus.araddr[31:0] = sh_ocl_araddr;
assign sh_ocl_mosi_bus.rready       = sh_ocl_rready;


assign ocl_sh_awready = sh_ocl_miso_bus.awready;
assign ocl_sh_wready  = sh_ocl_miso_bus.wready;

assign ocl_sh_bresp  = sh_ocl_miso_bus.bresp;
assign ocl_sh_bvalid = sh_ocl_miso_bus.bvalid;

assign ocl_sh_arready = sh_ocl_miso_bus.arready;
assign ocl_sh_rvalid  = sh_ocl_miso_bus.rvalid;
assign ocl_sh_rresp   = sh_ocl_miso_bus.rresp;
assign ocl_sh_rdata   = sh_ocl_miso_bus.rdata[31:0];


//-------------------------------------------------
// PCIe OCL AXI-L (SH to CL, from AppPF BAR0)
// this interface has 4 address ranges to use:
// 0x0000_0000 ~ 0x0000_0FFF : ctrl axil adapter  SH <-> CL
// 0x0000_1000 ~ 0x0000_1FFF : config 2 adapters  CL --> SH
// 0x0000_2000 ~ 0x0000_2FFF : config axi adapter SH <-> CL
// 0x0000_3000 ~             : to be determined
//-------------------------------------------------
cl_axil_mux4 ocl_cfg_slv (
  .clk_i      (clk),
  .reset_i    (~axi_crossbar_sync_rst_n),
  .mst_bus_i  (sh_ocl_mosi_bus  ),
  .mst_bus_o  (sh_ocl_miso_bus  ),
  .slv_0_bus_i(sh_ocl_0_miso_bus),
  .slv_0_bus_o(sh_ocl_0_mosi_bus),
  .slv_1_bus_i(sh_ocl_1_miso_bus),
  .slv_1_bus_o(sh_ocl_1_mosi_bus),
  .slv_2_bus_i(sh_ocl_2_miso_bus),
  .slv_2_bus_o(sh_ocl_2_mosi_bus),
  .slv_3_bus_i(sh_ocl_3_miso_bus),
  .slv_3_bus_o(sh_ocl_3_mosi_bus)
);



//=================================================
// SH Read and Write to FSB via AXI-Lite interface
//=================================================

// loopback test
(* dont_touch = "true" *) logic ocl_slv_sync_rst_n;
lib_pipe #(.WIDTH(1), .STAGES(4)) OCL_SLV_SLC_RST_N (
  .clk    (clk)
  ,.rst_n  (1'b1)
  ,.in_bus (sync_rst_n)
  ,.out_bus(ocl_slv_sync_rst_n)
);

logic adpt_slave_v;
logic [79:0] adpt_slave_data;
logic adpt_slave_r;

logic adpt_master_v;
logic [79:0] adpt_master_data;
logic adpt_master_r;

s_axil_fsb_adapter s_axil_fsb (
  .clk_i           (clk)
  ,.resetn_i        (ocl_slv_sync_rst_n)
  ,.sh_ocl_bus_i(sh_ocl_0_mosi_bus)
  ,.sh_ocl_bus_o(sh_ocl_0_miso_bus)
  ,.adpt_slave_v    (adpt_slave_v)
  ,.adpt_slave_data (adpt_slave_data)
  ,.adpt_slave_r    (adpt_slave_r)
  ,.adpt_master_v   (adpt_master_v)
  ,.adpt_master_data(adpt_master_data)
  ,.adpt_master_r   (adpt_master_r));


bsg_test_node_client #(
  .ring_width_p(80),
  .master_id_p (0 ),
  .client_id_p (0 )
) fsb_client_node (
  .clk_i  (clk)
  ,.reset_i(~ocl_slv_sync_rst_n)
  ,.en_i   (1'b1)
  // input channel
  ,.v_i    (adpt_master_v)
  ,.data_i (adpt_master_data)
  ,.ready_o(adpt_master_r)
  // output channel
  ,.v_o    (adpt_slave_v)
  ,.data_o (adpt_slave_data)
  ,.yumi_i ((adpt_slave_r&&adpt_slave_v))
);


// //=================================================
// // AXI4 to FSB slave 
// //=================================================

// axi_bus_t sh_cl_dma_pcis_bus();

// assign sh_cl_dma_pcis_bus.awid[5:0] = sh_cl_dma_pcis_awid;
// assign sh_cl_dma_pcis_bus.awaddr    = sh_cl_dma_pcis_awaddr;
// assign sh_cl_dma_pcis_bus.awlen     = sh_cl_dma_pcis_awlen;
// assign sh_cl_dma_pcis_bus.awsize    = sh_cl_dma_pcis_awsize;
// assign sh_cl_dma_pcis_bus.awvalid   = sh_cl_dma_pcis_awvalid;
// assign cl_sh_dma_pcis_awready       = sh_cl_dma_pcis_bus.awready;

// assign sh_cl_dma_pcis_bus.wdata     = sh_cl_dma_pcis_wdata;
// assign sh_cl_dma_pcis_bus.wstrb     = sh_cl_dma_pcis_wstrb;
// assign sh_cl_dma_pcis_bus.wlast     = sh_cl_dma_pcis_wlast;
// assign sh_cl_dma_pcis_bus.wvalid    = sh_cl_dma_pcis_wvalid;
// assign cl_sh_dma_pcis_wready        = sh_cl_dma_pcis_bus.wready;

// assign cl_sh_dma_pcis_bid           = {2'b0, sh_cl_dma_pcis_bus.bid[3:0]};
// assign cl_sh_dma_pcis_bresp         = sh_cl_dma_pcis_bus.bresp;
// assign cl_sh_dma_pcis_bvalid        = sh_cl_dma_pcis_bus.bvalid;
// assign sh_cl_dma_pcis_bus.bready    = sh_cl_dma_pcis_bready;

// assign sh_cl_dma_pcis_bus.arid[5:0] = sh_cl_dma_pcis_arid;
// assign sh_cl_dma_pcis_bus.araddr    = sh_cl_dma_pcis_araddr;
// assign sh_cl_dma_pcis_bus.arlen     = sh_cl_dma_pcis_arlen;
// assign sh_cl_dma_pcis_bus.arsize    = sh_cl_dma_pcis_arsize;
// assign sh_cl_dma_pcis_bus.arvalid   = sh_cl_dma_pcis_arvalid;
// assign cl_sh_dma_pcis_arready       = sh_cl_dma_pcis_bus.arready;

// assign cl_sh_dma_pcis_rid           = {2'b0, sh_cl_dma_pcis_bus.rid[3:0]};
// assign cl_sh_dma_pcis_rdata         = sh_cl_dma_pcis_bus.rdata;
// assign cl_sh_dma_pcis_rresp         = sh_cl_dma_pcis_bus.rresp;
// assign cl_sh_dma_pcis_rlast         = sh_cl_dma_pcis_bus.rlast;
// assign cl_sh_dma_pcis_rvalid        = sh_cl_dma_pcis_bus.rvalid;
// assign sh_cl_dma_pcis_bus.rready    = sh_cl_dma_pcis_rready;

// (* dont_touch = "true" *) logic dma_pcis_sync_rst_n;
// lib_pipe #(.WIDTH(1), .STAGES(4)) DMA_PCIS_SLC_RST_N (
//  .clk    (clk)
//  ,.rst_n  (1'b1)
//  ,.in_bus (sync_rst_n)
//  ,.out_bus(dma_pcis_sync_rst_n)
// );


// // Simple loop back 4x128bits without FSB client.
// // TODO: AXI4-512bit bus should be able to write single FSB packet (128bit,80bit) .
// s_axi4_fsb_adapter s_axi4_fsb (
//  .clk_i           (clk),
//  .resetn_i        (dma_pcis_sync_rst_n),
//  .sh_ocl_bus      (sh_ocl_mux02),
//  .sh_cl_dma_pcis  (sh_cl_dma_pcis_bus),
//  .adpt_slave_v    (),
//  .adpt_slave_data (),
//  .adpt_slave_r    (),
//  .adpt_master_v   (),
//  .adpt_master_data(),
//  .adpt_master_r   ()
// );


// //=================================================
// // CL write to host via AXI-4 pcim Interface
// //=================================================

//  axi_bus_t #(
//   .NUM_SLOTS (1),
//   .ID_WIDTH  (6),
//   .ADDR_WIDTH(64),
//   .DATA_WIDTH(512)
// ) cl_sh_pcim_bus ();

// assign cl_sh_pcim_awid        = cl_sh_pcim_bus.awid;
// assign cl_sh_pcim_awaddr      = cl_sh_pcim_bus.awaddr;
// assign cl_sh_pcim_awlen       = cl_sh_pcim_bus.awlen;
// assign cl_sh_pcim_awsize      = cl_sh_pcim_bus.awsize;
// assign cl_sh_pcim_awvalid     = cl_sh_pcim_bus.awvalid;
// assign cl_sh_pcim_bus.awready = sh_cl_pcim_awready;

// assign cl_sh_pcim_wdata       = cl_sh_pcim_bus.wdata;
// assign cl_sh_pcim_wstrb       = cl_sh_pcim_bus.wstrb;
// assign cl_sh_pcim_wlast       = cl_sh_pcim_bus.wlast;
// assign cl_sh_pcim_wvalid      = cl_sh_pcim_bus.wvalid;
// assign cl_sh_pcim_bus.wready  = sh_cl_pcim_wready;

// assign cl_sh_pcim_bus.bid     = sh_cl_pcim_bid;
// assign cl_sh_pcim_bus.bresp   = sh_cl_pcim_bresp;
// assign cl_sh_pcim_bus.bvalid  = sh_cl_pcim_bvalid;
// assign cl_sh_pcim_bready      = cl_sh_pcim_bus.bready;

// assign cl_sh_pcim_arid        = cl_sh_pcim_bus.arid;
// assign cl_sh_pcim_araddr      = cl_sh_pcim_bus.araddr;
// assign cl_sh_pcim_arlen       = cl_sh_pcim_bus.arlen;
// assign cl_sh_pcim_arsize      = cl_sh_pcim_bus.arsize;
// assign cl_sh_pcim_arvalid     = cl_sh_pcim_bus.arvalid;
// assign cl_sh_pcim_bus.arready = sh_cl_pcim_arready;

// assign cl_sh_pcim_bus.rid     = sh_cl_pcim_rid;
// assign cl_sh_pcim_bus.rdata   = sh_cl_pcim_rdata;
// assign cl_sh_pcim_bus.rresp   = sh_cl_pcim_rresp;
// assign cl_sh_pcim_bus.rlast   = sh_cl_pcim_rlast;
// assign cl_sh_pcim_bus.rvalid  = sh_cl_pcim_rvalid;
// assign cl_sh_pcim_rready      = cl_sh_pcim_bus.rready;


// (* dont_touch = "true" *) logic pcim_mstr_sync_rst_n;
// lib_pipe #(.WIDTH(1), .STAGES(4)) PCIM_MSTR_SLC_RST_N (
//   .clk    (clk)
//   ,.rst_n  (1'b1)
//   ,.in_bus (sync_rst_n)
//   ,.out_bus(pcim_mstr_sync_rst_n)
// );

// logic fsb_wvalid;
// logic [`FSB_WIDTH-1:0] fsb_wdata;
// logic fsb_yumi;

// bsg_test_node_master #(
//   .ring_width_p(`FSB_WIDTH)
//   ,.master_id_p (4'hF)
//   ,.client_id_p (4'hF)
// ) fsb_node_master (
//   .clk_i  (clk)
//   ,.reset_i(~pcim_mstr_sync_rst_n)
//   ,.en_i   (1'b1)
//   ,.v_i    (1'b0)
//   ,.data_i ({`FSB_WIDTH{1'b0}})
//   ,.ready_o()
//   ,.v_o    (fsb_wvalid)
//   ,.data_o (fsb_wdata)
//   ,.yumi_i (fsb_yumi)
// );


// logic fsb_wvalid_copy;
// logic [`FSB_WIDTH-1:0] fsb_wdata_copy;
// logic fsb_yumi_copy;

// bsg_test_node_master #(
//   .ring_width_p(`FSB_WIDTH)
//   ,.master_id_p (4'hF)
//   ,.client_id_p (4'hF)
// ) fsb_node_master_copy (
//   .clk_i  (clk)
//   ,.reset_i(~pcim_mstr_sync_rst_n)
//   ,.en_i   (1'b1)
//   ,.v_i    (1'b0)
//   ,.data_i ({`FSB_WIDTH{1'b0}})
//   ,.ready_o()
//   ,.v_o    (fsb_wvalid_copy)
//   ,.data_o (fsb_wdata_copy)
//   ,.yumi_i (fsb_yumi_copy)
// );


//   axis_bus_t #(.TDATA_WIDTH(512)) axis_data_bus ();

//   cl_axis_test_master #(
//     .DATA_WIDTH (512),
//     .SINGLE_NUM (256),
//     .PACKET_SIZE(16 ),
//     .MULTPKT_NUM(240)
//   ) axis_stream_master (
//     .clk_i        (clk                ),
//     .reset_i      (~pcim_mstr_sync_rst_n),
//     .en_i         (1'b1                 ),
//     .axis_data_bus(axis_data_bus        ),
//     .loop_done    (                     )
//   );

// cl_to_axi4_adapter axi4_pcim_write (
//   .clk_i           (clk                 ),
//   .resetn_i        (pcim_mstr_sync_rst_n),
//   .sh_cl_flr_assert(sh_cl_flr_assert_q  ),
//   .sh_ocl_cfg_bus  (sh_ocl_mux01        ),
//   .cl_sh_pcim_bus  (cl_sh_pcim_bus      ),
//   .axis_data_bus   (axis_data_bus       ),
//   .fsb_wvalid      (fsb_wvalid          ),
//   .fsb_wdata       (fsb_wdata           ),
//   .fsb_yumi        (fsb_yumi            ),
//   .fsb_wvalid_copy (fsb_wvalid_copy     ),
//   .fsb_wdata_copy  (fsb_wdata_copy      ),
//   .fsb_yumi_copy   (fsb_yumi_copy       )
// );


endmodule
