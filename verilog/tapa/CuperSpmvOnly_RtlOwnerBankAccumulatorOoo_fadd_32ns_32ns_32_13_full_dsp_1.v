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
`ifdef VERILATOR
`ifdef CUPER_VERILATOR_DPI_FP
    import "DPI-C" function int cuper_verilator_fadd32(input int a, input int b);
`endif

    reg [dout_WIDTH-1:0] pipe [0:31];
    reg [dout_WIDTH-1:0] dout_model;
    integer i;

    function [dout_WIDTH-1:0] fadd_model;
        input [din0_WIDTH-1:0] a;
        input [din1_WIDTH-1:0] b;
        begin
`ifdef CUPER_VERILATOR_DPI_FP
            fadd_model = cuper_verilator_fadd32(a, b);
`else
            fadd_model = $shortrealtobits($bitstoshortreal(a) +
                                          $bitstoshortreal(b));
`endif
        end
    endfunction

    always @(posedge clk) begin
        if (reset) begin
            dout_model <= {dout_WIDTH{1'b0}};
            for (i = 0; i < 32; i = i + 1) begin
                pipe[i] <= {dout_WIDTH{1'b0}};
            end
        end else if (ce) begin
            pipe[0] <= fadd_model(din0, din1);
            for (i = 1; i < 32; i = i + 1) begin
                if (i < NUM_STAGE) begin
                    pipe[i] <= pipe[i - 1];
                end
            end
            if (NUM_STAGE == 1) begin
                dout_model <= fadd_model(din0, din1);
            end else begin
                dout_model <= pipe[NUM_STAGE - 2];
            end
        end
    end

    assign dout = dout_model;

    wire unused_id = |ID;
`else
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
`endif
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
