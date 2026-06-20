`timescale 1 ns / 1 ps

// TAPA custom RTL owner-bank accumulator for CuperSpmvServiceOnly.
//
// One bank owns one output packet modulo HBM_CHANNEL_NUM.  It receives the
// eight fixed pair-lane streams that used to become eight independent TAPA
// tasks, runs the existing owner-lane RTL cores locally, and arbitrates their
// tagged float_v2 outputs into one owner stream.  The goal is to keep local
// 8-lane out-of-order accumulation while reducing top-level task/FIFO count.

`ifdef VERILATOR
`include "CuperSpmvOnly_RtlOwnerLaneAccumulatorOoo.v"
`else
`include "CuperSpmvOnly_RtlOwnerLaneAccumulatorOoo_support.vh"
`endif

module CuperSpmvOnly_RtlOwnerBankAccumulatorOoo (
    ap_clk,
    ap_rst_n,
    ap_start,
    ap_done,
    ap_idle,
    ap_ready,
    Iteration_num,
    Row_num,
    Owner_Lane_Stream_0_s_dout,
    Owner_Lane_Stream_0_s_empty_n,
    Owner_Lane_Stream_0_s_read,
    Owner_Lane_Stream_0_peek_dout,
    Owner_Lane_Stream_0_peek_empty_n,
    Owner_Lane_Stream_0_peek_read,
    Owner_Lane_Stream_1_s_dout,
    Owner_Lane_Stream_1_s_empty_n,
    Owner_Lane_Stream_1_s_read,
    Owner_Lane_Stream_1_peek_dout,
    Owner_Lane_Stream_1_peek_empty_n,
    Owner_Lane_Stream_1_peek_read,
    Owner_Lane_Stream_2_s_dout,
    Owner_Lane_Stream_2_s_empty_n,
    Owner_Lane_Stream_2_s_read,
    Owner_Lane_Stream_2_peek_dout,
    Owner_Lane_Stream_2_peek_empty_n,
    Owner_Lane_Stream_2_peek_read,
    Owner_Lane_Stream_3_s_dout,
    Owner_Lane_Stream_3_s_empty_n,
    Owner_Lane_Stream_3_s_read,
    Owner_Lane_Stream_3_peek_dout,
    Owner_Lane_Stream_3_peek_empty_n,
    Owner_Lane_Stream_3_peek_read,
    Owner_Lane_Stream_4_s_dout,
    Owner_Lane_Stream_4_s_empty_n,
    Owner_Lane_Stream_4_s_read,
    Owner_Lane_Stream_4_peek_dout,
    Owner_Lane_Stream_4_peek_empty_n,
    Owner_Lane_Stream_4_peek_read,
    Owner_Lane_Stream_5_s_dout,
    Owner_Lane_Stream_5_s_empty_n,
    Owner_Lane_Stream_5_s_read,
    Owner_Lane_Stream_5_peek_dout,
    Owner_Lane_Stream_5_peek_empty_n,
    Owner_Lane_Stream_5_peek_read,
    Owner_Lane_Stream_6_s_dout,
    Owner_Lane_Stream_6_s_empty_n,
    Owner_Lane_Stream_6_s_read,
    Owner_Lane_Stream_6_peek_dout,
    Owner_Lane_Stream_6_peek_empty_n,
    Owner_Lane_Stream_6_peek_read,
    Owner_Lane_Stream_7_s_dout,
    Owner_Lane_Stream_7_s_empty_n,
    Owner_Lane_Stream_7_s_read,
    Owner_Lane_Stream_7_peek_dout,
    Owner_Lane_Stream_7_peek_empty_n,
    Owner_Lane_Stream_7_peek_read,
    Vector_Y_Tagged_Stream_s_din,
    Vector_Y_Tagged_Stream_s_full_n,
    Vector_Y_Tagged_Stream_s_write,
    Vector_Y_Tagged_Stream_peek,
    Owner_id
);
    parameter integer HBM_CHANNEL_NUM = 16;

    input ap_clk;
    input ap_rst_n;
    input ap_start;
    output ap_done;
    output ap_idle;
    output ap_ready;
    input [31:0] Iteration_num;
    input [31:0] Row_num;

    input [129:0] Owner_Lane_Stream_0_s_dout;
    input Owner_Lane_Stream_0_s_empty_n;
    output Owner_Lane_Stream_0_s_read;
    input [129:0] Owner_Lane_Stream_0_peek_dout;
    input Owner_Lane_Stream_0_peek_empty_n;
    output Owner_Lane_Stream_0_peek_read;

    input [129:0] Owner_Lane_Stream_1_s_dout;
    input Owner_Lane_Stream_1_s_empty_n;
    output Owner_Lane_Stream_1_s_read;
    input [129:0] Owner_Lane_Stream_1_peek_dout;
    input Owner_Lane_Stream_1_peek_empty_n;
    output Owner_Lane_Stream_1_peek_read;

    input [129:0] Owner_Lane_Stream_2_s_dout;
    input Owner_Lane_Stream_2_s_empty_n;
    output Owner_Lane_Stream_2_s_read;
    input [129:0] Owner_Lane_Stream_2_peek_dout;
    input Owner_Lane_Stream_2_peek_empty_n;
    output Owner_Lane_Stream_2_peek_read;

    input [129:0] Owner_Lane_Stream_3_s_dout;
    input Owner_Lane_Stream_3_s_empty_n;
    output Owner_Lane_Stream_3_s_read;
    input [129:0] Owner_Lane_Stream_3_peek_dout;
    input Owner_Lane_Stream_3_peek_empty_n;
    output Owner_Lane_Stream_3_peek_read;

    input [129:0] Owner_Lane_Stream_4_s_dout;
    input Owner_Lane_Stream_4_s_empty_n;
    output Owner_Lane_Stream_4_s_read;
    input [129:0] Owner_Lane_Stream_4_peek_dout;
    input Owner_Lane_Stream_4_peek_empty_n;
    output Owner_Lane_Stream_4_peek_read;

    input [129:0] Owner_Lane_Stream_5_s_dout;
    input Owner_Lane_Stream_5_s_empty_n;
    output Owner_Lane_Stream_5_s_read;
    input [129:0] Owner_Lane_Stream_5_peek_dout;
    input Owner_Lane_Stream_5_peek_empty_n;
    output Owner_Lane_Stream_5_peek_read;

    input [129:0] Owner_Lane_Stream_6_s_dout;
    input Owner_Lane_Stream_6_s_empty_n;
    output Owner_Lane_Stream_6_s_read;
    input [129:0] Owner_Lane_Stream_6_peek_dout;
    input Owner_Lane_Stream_6_peek_empty_n;
    output Owner_Lane_Stream_6_peek_read;

    input [129:0] Owner_Lane_Stream_7_s_dout;
    input Owner_Lane_Stream_7_s_empty_n;
    output Owner_Lane_Stream_7_s_read;
    input [129:0] Owner_Lane_Stream_7_peek_dout;
    input Owner_Lane_Stream_7_peek_empty_n;
    output Owner_Lane_Stream_7_peek_read;

    output [128:0] Vector_Y_Tagged_Stream_s_din;
    input Vector_Y_Tagged_Stream_s_full_n;
    output Vector_Y_Tagged_Stream_s_write;
    input [128:0] Vector_Y_Tagged_Stream_peek;
    input [31:0] Owner_id;

    wire [128:0] lane_out_din [0:7];
    wire lane_out_write [0:7];
    wire lane_out_full_n [0:7];
    wire lane_done [0:7];
    wire lane_idle [0:7];
    wire lane_ready [0:7];
    wire [7:0] lane_done_vec;
    wire [7:0] lane_idle_vec;
    wire [7:0] lane_ready_vec;

    reg active;
    reg done_pulse;
    reg [7:0] done_seen;
    reg [2:0] arb_lane;

    wire rst = ~ap_rst_n;
    wire bank_start = ap_start & ~active;
    wire selected_full_n = active & Vector_Y_Tagged_Stream_s_full_n;

    assign lane_done_vec = {lane_done[7], lane_done[6], lane_done[5], lane_done[4],
                            lane_done[3], lane_done[2], lane_done[1], lane_done[0]};
    assign lane_idle_vec = {lane_idle[7], lane_idle[6], lane_idle[5], lane_idle[4],
                            lane_idle[3], lane_idle[2], lane_idle[1], lane_idle[0]};
    assign lane_ready_vec = {lane_ready[7], lane_ready[6], lane_ready[5], lane_ready[4],
                             lane_ready[3], lane_ready[2], lane_ready[1], lane_ready[0]};

    assign lane_out_full_n[0] = selected_full_n & (arb_lane == 3'd0);
    assign lane_out_full_n[1] = selected_full_n & (arb_lane == 3'd1);
    assign lane_out_full_n[2] = selected_full_n & (arb_lane == 3'd2);
    assign lane_out_full_n[3] = selected_full_n & (arb_lane == 3'd3);
    assign lane_out_full_n[4] = selected_full_n & (arb_lane == 3'd4);
    assign lane_out_full_n[5] = selected_full_n & (arb_lane == 3'd5);
    assign lane_out_full_n[6] = selected_full_n & (arb_lane == 3'd6);
    assign lane_out_full_n[7] = selected_full_n & (arb_lane == 3'd7);

    assign Vector_Y_Tagged_Stream_s_din =
        (arb_lane == 3'd0) ? lane_out_din[0] :
        (arb_lane == 3'd1) ? lane_out_din[1] :
        (arb_lane == 3'd2) ? lane_out_din[2] :
        (arb_lane == 3'd3) ? lane_out_din[3] :
        (arb_lane == 3'd4) ? lane_out_din[4] :
        (arb_lane == 3'd5) ? lane_out_din[5] :
        (arb_lane == 3'd6) ? lane_out_din[6] :
                             lane_out_din[7];

    assign Vector_Y_Tagged_Stream_s_write =
        (arb_lane == 3'd0) ? lane_out_write[0] :
        (arb_lane == 3'd1) ? lane_out_write[1] :
        (arb_lane == 3'd2) ? lane_out_write[2] :
        (arb_lane == 3'd3) ? lane_out_write[3] :
        (arb_lane == 3'd4) ? lane_out_write[4] :
        (arb_lane == 3'd5) ? lane_out_write[5] :
        (arb_lane == 3'd6) ? lane_out_write[6] :
                             lane_out_write[7];

    assign ap_done = done_pulse;
    assign ap_ready = done_pulse;
    assign ap_idle = (~active) & (~done_pulse);

    CuperSpmvOnly_RtlOwnerLaneAccumulatorOoo #(
        .HBM_CHANNEL_NUM(HBM_CHANNEL_NUM)
    ) lane0 (
        .ap_clk(ap_clk),
        .ap_rst_n(ap_rst_n),
        .ap_start(bank_start),
        .ap_done(lane_done[0]),
        .ap_idle(lane_idle[0]),
        .ap_ready(lane_ready[0]),
        .Iteration_num(Iteration_num),
        .Row_num(Row_num),
        .Owner_Lane_Stream_s_dout(Owner_Lane_Stream_0_s_dout),
        .Owner_Lane_Stream_s_empty_n(Owner_Lane_Stream_0_s_empty_n),
        .Owner_Lane_Stream_s_read(Owner_Lane_Stream_0_s_read),
        .Owner_Lane_Stream_peek_dout(Owner_Lane_Stream_0_peek_dout),
        .Owner_Lane_Stream_peek_empty_n(Owner_Lane_Stream_0_peek_empty_n),
        .Owner_Lane_Stream_peek_read(Owner_Lane_Stream_0_peek_read),
        .Vector_Y_Tagged_Stream_s_din(lane_out_din[0]),
        .Vector_Y_Tagged_Stream_s_full_n(lane_out_full_n[0]),
        .Vector_Y_Tagged_Stream_s_write(lane_out_write[0]),
        .Vector_Y_Tagged_Stream_peek(Vector_Y_Tagged_Stream_peek)
    );

    CuperSpmvOnly_RtlOwnerLaneAccumulatorOoo #(
        .HBM_CHANNEL_NUM(HBM_CHANNEL_NUM)
    ) lane1 (
        .ap_clk(ap_clk),
        .ap_rst_n(ap_rst_n),
        .ap_start(bank_start),
        .ap_done(lane_done[1]),
        .ap_idle(lane_idle[1]),
        .ap_ready(lane_ready[1]),
        .Iteration_num(Iteration_num),
        .Row_num(Row_num),
        .Owner_Lane_Stream_s_dout(Owner_Lane_Stream_1_s_dout),
        .Owner_Lane_Stream_s_empty_n(Owner_Lane_Stream_1_s_empty_n),
        .Owner_Lane_Stream_s_read(Owner_Lane_Stream_1_s_read),
        .Owner_Lane_Stream_peek_dout(Owner_Lane_Stream_1_peek_dout),
        .Owner_Lane_Stream_peek_empty_n(Owner_Lane_Stream_1_peek_empty_n),
        .Owner_Lane_Stream_peek_read(Owner_Lane_Stream_1_peek_read),
        .Vector_Y_Tagged_Stream_s_din(lane_out_din[1]),
        .Vector_Y_Tagged_Stream_s_full_n(lane_out_full_n[1]),
        .Vector_Y_Tagged_Stream_s_write(lane_out_write[1]),
        .Vector_Y_Tagged_Stream_peek(Vector_Y_Tagged_Stream_peek)
    );

    CuperSpmvOnly_RtlOwnerLaneAccumulatorOoo #(
        .HBM_CHANNEL_NUM(HBM_CHANNEL_NUM)
    ) lane2 (
        .ap_clk(ap_clk),
        .ap_rst_n(ap_rst_n),
        .ap_start(bank_start),
        .ap_done(lane_done[2]),
        .ap_idle(lane_idle[2]),
        .ap_ready(lane_ready[2]),
        .Iteration_num(Iteration_num),
        .Row_num(Row_num),
        .Owner_Lane_Stream_s_dout(Owner_Lane_Stream_2_s_dout),
        .Owner_Lane_Stream_s_empty_n(Owner_Lane_Stream_2_s_empty_n),
        .Owner_Lane_Stream_s_read(Owner_Lane_Stream_2_s_read),
        .Owner_Lane_Stream_peek_dout(Owner_Lane_Stream_2_peek_dout),
        .Owner_Lane_Stream_peek_empty_n(Owner_Lane_Stream_2_peek_empty_n),
        .Owner_Lane_Stream_peek_read(Owner_Lane_Stream_2_peek_read),
        .Vector_Y_Tagged_Stream_s_din(lane_out_din[2]),
        .Vector_Y_Tagged_Stream_s_full_n(lane_out_full_n[2]),
        .Vector_Y_Tagged_Stream_s_write(lane_out_write[2]),
        .Vector_Y_Tagged_Stream_peek(Vector_Y_Tagged_Stream_peek)
    );

    CuperSpmvOnly_RtlOwnerLaneAccumulatorOoo #(
        .HBM_CHANNEL_NUM(HBM_CHANNEL_NUM)
    ) lane3 (
        .ap_clk(ap_clk),
        .ap_rst_n(ap_rst_n),
        .ap_start(bank_start),
        .ap_done(lane_done[3]),
        .ap_idle(lane_idle[3]),
        .ap_ready(lane_ready[3]),
        .Iteration_num(Iteration_num),
        .Row_num(Row_num),
        .Owner_Lane_Stream_s_dout(Owner_Lane_Stream_3_s_dout),
        .Owner_Lane_Stream_s_empty_n(Owner_Lane_Stream_3_s_empty_n),
        .Owner_Lane_Stream_s_read(Owner_Lane_Stream_3_s_read),
        .Owner_Lane_Stream_peek_dout(Owner_Lane_Stream_3_peek_dout),
        .Owner_Lane_Stream_peek_empty_n(Owner_Lane_Stream_3_peek_empty_n),
        .Owner_Lane_Stream_peek_read(Owner_Lane_Stream_3_peek_read),
        .Vector_Y_Tagged_Stream_s_din(lane_out_din[3]),
        .Vector_Y_Tagged_Stream_s_full_n(lane_out_full_n[3]),
        .Vector_Y_Tagged_Stream_s_write(lane_out_write[3]),
        .Vector_Y_Tagged_Stream_peek(Vector_Y_Tagged_Stream_peek)
    );

    CuperSpmvOnly_RtlOwnerLaneAccumulatorOoo #(
        .HBM_CHANNEL_NUM(HBM_CHANNEL_NUM)
    ) lane4 (
        .ap_clk(ap_clk),
        .ap_rst_n(ap_rst_n),
        .ap_start(bank_start),
        .ap_done(lane_done[4]),
        .ap_idle(lane_idle[4]),
        .ap_ready(lane_ready[4]),
        .Iteration_num(Iteration_num),
        .Row_num(Row_num),
        .Owner_Lane_Stream_s_dout(Owner_Lane_Stream_4_s_dout),
        .Owner_Lane_Stream_s_empty_n(Owner_Lane_Stream_4_s_empty_n),
        .Owner_Lane_Stream_s_read(Owner_Lane_Stream_4_s_read),
        .Owner_Lane_Stream_peek_dout(Owner_Lane_Stream_4_peek_dout),
        .Owner_Lane_Stream_peek_empty_n(Owner_Lane_Stream_4_peek_empty_n),
        .Owner_Lane_Stream_peek_read(Owner_Lane_Stream_4_peek_read),
        .Vector_Y_Tagged_Stream_s_din(lane_out_din[4]),
        .Vector_Y_Tagged_Stream_s_full_n(lane_out_full_n[4]),
        .Vector_Y_Tagged_Stream_s_write(lane_out_write[4]),
        .Vector_Y_Tagged_Stream_peek(Vector_Y_Tagged_Stream_peek)
    );

    CuperSpmvOnly_RtlOwnerLaneAccumulatorOoo #(
        .HBM_CHANNEL_NUM(HBM_CHANNEL_NUM)
    ) lane5 (
        .ap_clk(ap_clk),
        .ap_rst_n(ap_rst_n),
        .ap_start(bank_start),
        .ap_done(lane_done[5]),
        .ap_idle(lane_idle[5]),
        .ap_ready(lane_ready[5]),
        .Iteration_num(Iteration_num),
        .Row_num(Row_num),
        .Owner_Lane_Stream_s_dout(Owner_Lane_Stream_5_s_dout),
        .Owner_Lane_Stream_s_empty_n(Owner_Lane_Stream_5_s_empty_n),
        .Owner_Lane_Stream_s_read(Owner_Lane_Stream_5_s_read),
        .Owner_Lane_Stream_peek_dout(Owner_Lane_Stream_5_peek_dout),
        .Owner_Lane_Stream_peek_empty_n(Owner_Lane_Stream_5_peek_empty_n),
        .Owner_Lane_Stream_peek_read(Owner_Lane_Stream_5_peek_read),
        .Vector_Y_Tagged_Stream_s_din(lane_out_din[5]),
        .Vector_Y_Tagged_Stream_s_full_n(lane_out_full_n[5]),
        .Vector_Y_Tagged_Stream_s_write(lane_out_write[5]),
        .Vector_Y_Tagged_Stream_peek(Vector_Y_Tagged_Stream_peek)
    );

    CuperSpmvOnly_RtlOwnerLaneAccumulatorOoo #(
        .HBM_CHANNEL_NUM(HBM_CHANNEL_NUM)
    ) lane6 (
        .ap_clk(ap_clk),
        .ap_rst_n(ap_rst_n),
        .ap_start(bank_start),
        .ap_done(lane_done[6]),
        .ap_idle(lane_idle[6]),
        .ap_ready(lane_ready[6]),
        .Iteration_num(Iteration_num),
        .Row_num(Row_num),
        .Owner_Lane_Stream_s_dout(Owner_Lane_Stream_6_s_dout),
        .Owner_Lane_Stream_s_empty_n(Owner_Lane_Stream_6_s_empty_n),
        .Owner_Lane_Stream_s_read(Owner_Lane_Stream_6_s_read),
        .Owner_Lane_Stream_peek_dout(Owner_Lane_Stream_6_peek_dout),
        .Owner_Lane_Stream_peek_empty_n(Owner_Lane_Stream_6_peek_empty_n),
        .Owner_Lane_Stream_peek_read(Owner_Lane_Stream_6_peek_read),
        .Vector_Y_Tagged_Stream_s_din(lane_out_din[6]),
        .Vector_Y_Tagged_Stream_s_full_n(lane_out_full_n[6]),
        .Vector_Y_Tagged_Stream_s_write(lane_out_write[6]),
        .Vector_Y_Tagged_Stream_peek(Vector_Y_Tagged_Stream_peek)
    );

    CuperSpmvOnly_RtlOwnerLaneAccumulatorOoo #(
        .HBM_CHANNEL_NUM(HBM_CHANNEL_NUM)
    ) lane7 (
        .ap_clk(ap_clk),
        .ap_rst_n(ap_rst_n),
        .ap_start(bank_start),
        .ap_done(lane_done[7]),
        .ap_idle(lane_idle[7]),
        .ap_ready(lane_ready[7]),
        .Iteration_num(Iteration_num),
        .Row_num(Row_num),
        .Owner_Lane_Stream_s_dout(Owner_Lane_Stream_7_s_dout),
        .Owner_Lane_Stream_s_empty_n(Owner_Lane_Stream_7_s_empty_n),
        .Owner_Lane_Stream_s_read(Owner_Lane_Stream_7_s_read),
        .Owner_Lane_Stream_peek_dout(Owner_Lane_Stream_7_peek_dout),
        .Owner_Lane_Stream_peek_empty_n(Owner_Lane_Stream_7_peek_empty_n),
        .Owner_Lane_Stream_peek_read(Owner_Lane_Stream_7_peek_read),
        .Vector_Y_Tagged_Stream_s_din(lane_out_din[7]),
        .Vector_Y_Tagged_Stream_s_full_n(lane_out_full_n[7]),
        .Vector_Y_Tagged_Stream_s_write(lane_out_write[7]),
        .Vector_Y_Tagged_Stream_peek(Vector_Y_Tagged_Stream_peek)
    );

    always @(posedge ap_clk) begin
        if (rst) begin
            active <= 1'b0;
            done_pulse <= 1'b0;
            done_seen <= 8'd0;
            arb_lane <= 3'd0;
        end else begin
            done_pulse <= 1'b0;

            if (!active) begin
                if (ap_start) begin
                    active <= 1'b1;
                    done_seen <= 8'd0;
                    arb_lane <= 3'd0;
                end
            end else begin
                done_seen <= done_seen | lane_done_vec;

                if (Vector_Y_Tagged_Stream_s_full_n) begin
                    arb_lane <= arb_lane + 3'd1;
                end

                if ((done_seen | lane_done_vec) == 8'hff) begin
                    active <= 1'b0;
                    done_pulse <= 1'b1;
                end
            end
        end
    end

    wire unused_owner = |Owner_id;
    wire unused_lane_status = (|lane_idle_vec) ^ (|lane_ready_vec);
endmodule

`ifndef VERILATOR
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
    ) fadd_alias (
        .clk(clk),
        .reset(reset),
        .ce(ce),
        .din0(din0),
        .din1(din1),
        .dout(dout)
    );
endmodule
`endif
