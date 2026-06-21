`timescale 1ns/1ps

`ifdef CUPER_SPMV_ONLY_INCLUDE_FADD_IP_SIM
`include "CuperSpmvOnly_RtlOwnerBankAccumulatorOoo_fadd_32ns_32ns_32_13_full_dsp_1_ip.v"
`endif

module CuperSpmvOnly_RtlOwnerBankAccumulatorOoo_fadd_32ns_32ns_32_13_full_dsp_1
#(parameter ID = 1,
  parameter NUM_STAGE = 13,
  parameter din0_WIDTH = 32,
  parameter din1_WIDTH = 32,
  parameter dout_WIDTH = 32)
(
    input wire clk,
    input wire reset,
    input wire ce,
    input wire [din0_WIDTH-1:0] din0,
    input wire [din1_WIDTH-1:0] din1,
    output wire [dout_WIDTH-1:0] dout
);
    wire [dout_WIDTH-1:0] dout_i;
    reg [din0_WIDTH-1:0] din0_buf1;
    reg [din1_WIDTH-1:0] din1_buf1;
    reg ce_r;
    reg [dout_WIDTH-1:0] dout_r;

    CuperSpmvOnly_RtlOwnerBankAccumulatorOoo_fadd_32ns_32ns_32_13_full_dsp_1_ip fadd_ip_u (
        .aclk(clk),
        .aclken(ce_r),
        .s_axis_a_tvalid(1'b1),
        .s_axis_a_tdata(din0_buf1),
        .s_axis_b_tvalid(1'b1),
        .s_axis_b_tdata(din1_buf1),
        .m_axis_result_tvalid(),
        .m_axis_result_tdata(dout_i)
    );

    always @(posedge clk) begin
        if (ce) begin
            din0_buf1 <= din0;
            din1_buf1 <= din1;
        end
    end

    always @(posedge clk) begin
        ce_r <= ce;
    end

    always @(posedge clk) begin
        if (ce_r) begin
            dout_r <= dout_i;
        end
    end

    assign dout = ce_r ? dout_i : dout_r;

    wire unused_reset = reset;
    wire unused_id = |ID;
    wire unused_stage = |NUM_STAGE;
endmodule

module CuperSpmvOnly_RtlOwnerLaneAccumulatorOoo_fadd_32ns_32ns_32_13_full_dsp_1
#(parameter ID = 1,
  parameter NUM_STAGE = 13,
  parameter din0_WIDTH = 32,
  parameter din1_WIDTH = 32,
  parameter dout_WIDTH = 32)
(
    input wire clk,
    input wire reset,
    input wire ce,
    input wire [din0_WIDTH-1:0] din0,
    input wire [din1_WIDTH-1:0] din1,
    output wire [dout_WIDTH-1:0] dout
);
    CuperSpmvOnly_RtlOwnerBankAccumulatorOoo_fadd_32ns_32ns_32_13_full_dsp_1 #(
        .ID(ID),
        .NUM_STAGE(NUM_STAGE),
        .din0_WIDTH(din0_WIDTH),
        .din1_WIDTH(din1_WIDTH),
        .dout_WIDTH(dout_WIDTH)
    ) alias_u (
        .clk(clk),
        .reset(reset),
        .ce(ce),
        .din0(din0),
        .din1(din1),
        .dout(dout)
    );
endmodule
