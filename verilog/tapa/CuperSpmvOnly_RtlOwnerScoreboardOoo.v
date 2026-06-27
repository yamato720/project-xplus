`timescale 1 ns / 1 ps

`ifndef VERILATOR
`include "CuperSpmvOnly_RtlIssueScoreboard8.v"
`endif

// TAPA custom RTL scoreboard scheduler for CuperSpmvServiceOnly.
//
// This is the weak RTL branch: it chooses a conflict-free subset from eight
// owner-lane FIFO heads each cycle and emits a full 8-wide vector beat.  Slots
// that cannot issue in the current cycle are marked as padding.  FP32
// accumulation, URAM partial sums and final tagged writer remain in HLS.
//
// Input CuperSpmvOnly_TaggedScalar, 130 bits:
//   {pad[129], value[128:97], scalar_lane[96:65],
//    pair_lane[64:33], packet_idx[32:1], done[0]}
//
// TAPA exposes ap_uint<1040> stream data ports as [1040:0].  Bit 1040 is
// kept zero here; bits [1039:0] carry the eight 130-bit packets.
module CuperSpmvOnly_RtlOwnerScoreboardOoo (
    ap_clk,
    ap_rst_n,
    ap_start,
    ap_done,
    ap_idle,
    ap_ready,
    Iteration_num,
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
    Scheduled_Owner_Stream_s_din,
    Scheduled_Owner_Stream_s_full_n,
    Scheduled_Owner_Stream_s_write,
    Scheduled_Owner_Stream_peek,
    Owner_id
);
    parameter integer HBM_CHANNEL_NUM = 16;
    parameter integer ADDR_WIDTH = 13;
    parameter integer SCOREBOARD_DEPTH = 12;
    parameter integer TAGGED_WIDTH = 130;
    parameter integer TAGGED_PAD_BIT = 129;

    input ap_clk;
    input ap_rst_n;
    input ap_start;
    output ap_done;
    output ap_idle;
    output ap_ready;
    input [31:0] Iteration_num;

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

    output [(8 * TAGGED_WIDTH):0] Scheduled_Owner_Stream_s_din;
    input Scheduled_Owner_Stream_s_full_n;
    output Scheduled_Owner_Stream_s_write;
    input [(8 * TAGGED_WIDTH):0] Scheduled_Owner_Stream_peek;
    input [31:0] Owner_id;

    reg active;
    reg done_pulse;
    reg [31:0] done_count;
    reg [7:0] done_seen;
    reg [7:0] head_valid_reg;
    reg [(8 * TAGGED_WIDTH)-1:0] head_payload_reg;

    wire rst = ~ap_rst_n;
    wire task_start = ap_start & ~active;
    wire scoreboard_rst = rst | task_start;
    wire [31:0] iteration_time =
        (Iteration_num == 32'd0) ? 32'd1 : Iteration_num;

    wire [7:0] input_valid = {
        Owner_Lane_Stream_7_s_empty_n,
        Owner_Lane_Stream_6_s_empty_n,
        Owner_Lane_Stream_5_s_empty_n,
        Owner_Lane_Stream_4_s_empty_n,
        Owner_Lane_Stream_3_s_empty_n,
        Owner_Lane_Stream_2_s_empty_n,
        Owner_Lane_Stream_1_s_empty_n,
        Owner_Lane_Stream_0_s_empty_n
    };

    wire [(8 * TAGGED_WIDTH)-1:0] input_payload = {
        {1'b0, Owner_Lane_Stream_7_s_dout[128:0]},
        {1'b0, Owner_Lane_Stream_6_s_dout[128:0]},
        {1'b0, Owner_Lane_Stream_5_s_dout[128:0]},
        {1'b0, Owner_Lane_Stream_4_s_dout[128:0]},
        {1'b0, Owner_Lane_Stream_3_s_dout[128:0]},
        {1'b0, Owner_Lane_Stream_2_s_dout[128:0]},
        {1'b0, Owner_Lane_Stream_1_s_dout[128:0]},
        {1'b0, Owner_Lane_Stream_0_s_dout[128:0]}
    };

    wire [7:0] fill_lane =
        active ? (input_valid & ~done_seen & ~head_valid_reg) : 8'd0;
    wire [7:0] head_valid = active ? (head_valid_reg & ~done_seen) : 8'd0;

    wire [7:0] head_done = {
        head_payload_reg[7 * TAGGED_WIDTH],
        head_payload_reg[6 * TAGGED_WIDTH],
        head_payload_reg[5 * TAGGED_WIDTH],
        head_payload_reg[4 * TAGGED_WIDTH],
        head_payload_reg[3 * TAGGED_WIDTH],
        head_payload_reg[2 * TAGGED_WIDTH],
        head_payload_reg[1 * TAGGED_WIDTH],
        head_payload_reg[0 * TAGGED_WIDTH]
    };

    wire [7:0] head_is_pong = {
        |head_payload_reg[7 * TAGGED_WIDTH + 65 +: 32],
        |head_payload_reg[6 * TAGGED_WIDTH + 65 +: 32],
        |head_payload_reg[5 * TAGGED_WIDTH + 65 +: 32],
        |head_payload_reg[4 * TAGGED_WIDTH + 65 +: 32],
        |head_payload_reg[3 * TAGGED_WIDTH + 65 +: 32],
        |head_payload_reg[2 * TAGGED_WIDTH + 65 +: 32],
        |head_payload_reg[1 * TAGGED_WIDTH + 65 +: 32],
        |head_payload_reg[0 * TAGGED_WIDTH + 65 +: 32]
    };

    wire [31:0] head_packet_idx [0:7];
    assign head_packet_idx[0] = head_payload_reg[0 * TAGGED_WIDTH + 1 +: 32];
    assign head_packet_idx[1] = head_payload_reg[1 * TAGGED_WIDTH + 1 +: 32];
    assign head_packet_idx[2] = head_payload_reg[2 * TAGGED_WIDTH + 1 +: 32];
    assign head_packet_idx[3] = head_payload_reg[3 * TAGGED_WIDTH + 1 +: 32];
    assign head_packet_idx[4] = head_payload_reg[4 * TAGGED_WIDTH + 1 +: 32];
    assign head_packet_idx[5] = head_payload_reg[5 * TAGGED_WIDTH + 1 +: 32];
    assign head_packet_idx[6] = head_payload_reg[6 * TAGGED_WIDTH + 1 +: 32];
    assign head_packet_idx[7] = head_payload_reg[7 * TAGGED_WIDTH + 1 +: 32];

    wire [31:0] head_addr_full [0:7];
    assign head_addr_full[0] = head_packet_idx[0] / HBM_CHANNEL_NUM;
    assign head_addr_full[1] = head_packet_idx[1] / HBM_CHANNEL_NUM;
    assign head_addr_full[2] = head_packet_idx[2] / HBM_CHANNEL_NUM;
    assign head_addr_full[3] = head_packet_idx[3] / HBM_CHANNEL_NUM;
    assign head_addr_full[4] = head_packet_idx[4] / HBM_CHANNEL_NUM;
    assign head_addr_full[5] = head_packet_idx[5] / HBM_CHANNEL_NUM;
    assign head_addr_full[6] = head_packet_idx[6] / HBM_CHANNEL_NUM;
    assign head_addr_full[7] = head_packet_idx[7] / HBM_CHANNEL_NUM;

    wire [ADDR_WIDTH-1:0] head_addr_lane [0:7];
    assign head_addr_lane[0] = head_addr_full[0][ADDR_WIDTH-1:0];
    assign head_addr_lane[1] = head_addr_full[1][ADDR_WIDTH-1:0];
    assign head_addr_lane[2] = head_addr_full[2][ADDR_WIDTH-1:0];
    assign head_addr_lane[3] = head_addr_full[3][ADDR_WIDTH-1:0];
    assign head_addr_lane[4] = head_addr_full[4][ADDR_WIDTH-1:0];
    assign head_addr_lane[5] = head_addr_full[5][ADDR_WIDTH-1:0];
    assign head_addr_lane[6] = head_addr_full[6][ADDR_WIDTH-1:0];
    assign head_addr_lane[7] = head_addr_full[7][ADDR_WIDTH-1:0];

    wire [(8 * ADDR_WIDTH)-1:0] head_addr = {
        head_addr_lane[7],
        head_addr_lane[6],
        head_addr_lane[5],
        head_addr_lane[4],
        head_addr_lane[3],
        head_addr_lane[2],
        head_addr_lane[1],
        head_addr_lane[0]
    };

    wire [(8 * TAGGED_WIDTH)-1:0] head_payload = head_payload_reg;

    wire issue_valid;
    wire [(8 * TAGGED_WIDTH)-1:0] issue_payload;
    wire [7:0] lane_hazard;
    wire scoreboard_empty;
    wire issue_ready = active & Scheduled_Owner_Stream_s_full_n;

    CuperSpmvOnly_RtlIssueScoreboard8 #(
        .ADDR_WIDTH(ADDR_WIDTH),
        .SCOREBOARD_DEPTH(SCOREBOARD_DEPTH)
    ) issue_u (
        .clk(ap_clk),
        .rst(scoreboard_rst),
        .head_valid(head_valid),
        .head_addr(head_addr),
        .head_is_pong(head_is_pong),
        .head_done(head_done),
        .head_payload(head_payload),
        .issue_ready(issue_ready),
        .issue_valid(issue_valid),
        .issue_payload(issue_payload),
        .lane_hazard(lane_hazard),
        .scoreboard_empty(scoreboard_empty)
    );

    wire transfer = active & Scheduled_Owner_Stream_s_full_n & issue_valid;
    wire [7:0] pop_lane = {
        transfer & ~issue_payload[7 * TAGGED_WIDTH + TAGGED_PAD_BIT],
        transfer & ~issue_payload[6 * TAGGED_WIDTH + TAGGED_PAD_BIT],
        transfer & ~issue_payload[5 * TAGGED_WIDTH + TAGGED_PAD_BIT],
        transfer & ~issue_payload[4 * TAGGED_WIDTH + TAGGED_PAD_BIT],
        transfer & ~issue_payload[3 * TAGGED_WIDTH + TAGGED_PAD_BIT],
        transfer & ~issue_payload[2 * TAGGED_WIDTH + TAGGED_PAD_BIT],
        transfer & ~issue_payload[1 * TAGGED_WIDTH + TAGGED_PAD_BIT],
        transfer & ~issue_payload[0 * TAGGED_WIDTH + TAGGED_PAD_BIT]
    };
    wire [7:0] done_pop_lane = {
        pop_lane[7] & issue_payload[7 * TAGGED_WIDTH],
        pop_lane[6] & issue_payload[6 * TAGGED_WIDTH],
        pop_lane[5] & issue_payload[5 * TAGGED_WIDTH],
        pop_lane[4] & issue_payload[4 * TAGGED_WIDTH],
        pop_lane[3] & issue_payload[3 * TAGGED_WIDTH],
        pop_lane[2] & issue_payload[2 * TAGGED_WIDTH],
        pop_lane[1] & issue_payload[1 * TAGGED_WIDTH],
        pop_lane[0] & issue_payload[0 * TAGGED_WIDTH]
    };
    wire round_done = ((done_seen | done_pop_lane) == 8'hff);
    wire all_rounds_done = round_done &&
                           ((done_count + 32'd1) >= iteration_time);

    assign Owner_Lane_Stream_0_s_read = fill_lane[0];
    assign Owner_Lane_Stream_1_s_read = fill_lane[1];
    assign Owner_Lane_Stream_2_s_read = fill_lane[2];
    assign Owner_Lane_Stream_3_s_read = fill_lane[3];
    assign Owner_Lane_Stream_4_s_read = fill_lane[4];
    assign Owner_Lane_Stream_5_s_read = fill_lane[5];
    assign Owner_Lane_Stream_6_s_read = fill_lane[6];
    assign Owner_Lane_Stream_7_s_read = fill_lane[7];

    assign Owner_Lane_Stream_0_peek_read = 1'b0;
    assign Owner_Lane_Stream_1_peek_read = 1'b0;
    assign Owner_Lane_Stream_2_peek_read = 1'b0;
    assign Owner_Lane_Stream_3_peek_read = 1'b0;
    assign Owner_Lane_Stream_4_peek_read = 1'b0;
    assign Owner_Lane_Stream_5_peek_read = 1'b0;
    assign Owner_Lane_Stream_6_peek_read = 1'b0;
    assign Owner_Lane_Stream_7_peek_read = 1'b0;

    assign Scheduled_Owner_Stream_s_din = {1'b0, issue_payload};
    assign Scheduled_Owner_Stream_s_write = transfer;

    assign ap_done = done_pulse;
    assign ap_ready = done_pulse;
    assign ap_idle = (~active) & (~done_pulse);

    always @(posedge ap_clk) begin
        if (rst) begin
            active <= 1'b0;
            done_pulse <= 1'b0;
            done_count <= 32'd0;
            done_seen <= 8'd0;
            head_valid_reg <= 8'd0;
            head_payload_reg <= {(8 * TAGGED_WIDTH){1'b0}};
        end else begin
            done_pulse <= 1'b0;

            if (!active) begin
                if (ap_start) begin
                    active <= 1'b1;
                    done_count <= 32'd0;
                    done_seen <= 8'd0;
                    head_valid_reg <= 8'd0;
                    head_payload_reg <= {(8 * TAGGED_WIDTH){1'b0}};
                end
            end else begin
                if (pop_lane[0]) begin
                    head_valid_reg[0] <= 1'b0;
                end else if (fill_lane[0]) begin
                    head_valid_reg[0] <= 1'b1;
                    head_payload_reg[0 * TAGGED_WIDTH +: TAGGED_WIDTH] <=
                        input_payload[0 * TAGGED_WIDTH +: TAGGED_WIDTH];
                end

                if (pop_lane[1]) begin
                    head_valid_reg[1] <= 1'b0;
                end else if (fill_lane[1]) begin
                    head_valid_reg[1] <= 1'b1;
                    head_payload_reg[1 * TAGGED_WIDTH +: TAGGED_WIDTH] <=
                        input_payload[1 * TAGGED_WIDTH +: TAGGED_WIDTH];
                end

                if (pop_lane[2]) begin
                    head_valid_reg[2] <= 1'b0;
                end else if (fill_lane[2]) begin
                    head_valid_reg[2] <= 1'b1;
                    head_payload_reg[2 * TAGGED_WIDTH +: TAGGED_WIDTH] <=
                        input_payload[2 * TAGGED_WIDTH +: TAGGED_WIDTH];
                end

                if (pop_lane[3]) begin
                    head_valid_reg[3] <= 1'b0;
                end else if (fill_lane[3]) begin
                    head_valid_reg[3] <= 1'b1;
                    head_payload_reg[3 * TAGGED_WIDTH +: TAGGED_WIDTH] <=
                        input_payload[3 * TAGGED_WIDTH +: TAGGED_WIDTH];
                end

                if (pop_lane[4]) begin
                    head_valid_reg[4] <= 1'b0;
                end else if (fill_lane[4]) begin
                    head_valid_reg[4] <= 1'b1;
                    head_payload_reg[4 * TAGGED_WIDTH +: TAGGED_WIDTH] <=
                        input_payload[4 * TAGGED_WIDTH +: TAGGED_WIDTH];
                end

                if (pop_lane[5]) begin
                    head_valid_reg[5] <= 1'b0;
                end else if (fill_lane[5]) begin
                    head_valid_reg[5] <= 1'b1;
                    head_payload_reg[5 * TAGGED_WIDTH +: TAGGED_WIDTH] <=
                        input_payload[5 * TAGGED_WIDTH +: TAGGED_WIDTH];
                end

                if (pop_lane[6]) begin
                    head_valid_reg[6] <= 1'b0;
                end else if (fill_lane[6]) begin
                    head_valid_reg[6] <= 1'b1;
                    head_payload_reg[6 * TAGGED_WIDTH +: TAGGED_WIDTH] <=
                        input_payload[6 * TAGGED_WIDTH +: TAGGED_WIDTH];
                end

                if (pop_lane[7]) begin
                    head_valid_reg[7] <= 1'b0;
                end else if (fill_lane[7]) begin
                    head_valid_reg[7] <= 1'b1;
                    head_payload_reg[7 * TAGGED_WIDTH +: TAGGED_WIDTH] <=
                        input_payload[7 * TAGGED_WIDTH +: TAGGED_WIDTH];
                end

                if (transfer) begin
                    done_seen <= done_seen | done_pop_lane;
                end

                if (round_done) begin
                    done_seen <= 8'd0;
                    done_count <= done_count + 32'd1;
                end

                if (all_rounds_done) begin
                    active <= 1'b0;
                    done_count <= 32'd0;
                    done_seen <= 8'd0;
                    head_valid_reg <= 8'd0;
                    head_payload_reg <= {(8 * TAGGED_WIDTH){1'b0}};
                    done_pulse <= 1'b1;
                end
            end
        end
    end

    wire unused_owner = |Owner_id;
    wire unused_input_padding = Owner_Lane_Stream_0_s_dout[129] ^
                                Owner_Lane_Stream_1_s_dout[129] ^
                                Owner_Lane_Stream_2_s_dout[129] ^
                                Owner_Lane_Stream_3_s_dout[129] ^
                                Owner_Lane_Stream_4_s_dout[129] ^
                                Owner_Lane_Stream_5_s_dout[129] ^
                                Owner_Lane_Stream_6_s_dout[129] ^
                                Owner_Lane_Stream_7_s_dout[129];
    wire unused_peek = (|Owner_Lane_Stream_0_peek_dout) ^
                       Owner_Lane_Stream_0_peek_empty_n ^
                       (|Owner_Lane_Stream_1_peek_dout) ^
                       Owner_Lane_Stream_1_peek_empty_n ^
                       (|Owner_Lane_Stream_2_peek_dout) ^
                       Owner_Lane_Stream_2_peek_empty_n ^
                       (|Owner_Lane_Stream_3_peek_dout) ^
                       Owner_Lane_Stream_3_peek_empty_n ^
                       (|Owner_Lane_Stream_4_peek_dout) ^
                       Owner_Lane_Stream_4_peek_empty_n ^
                       (|Owner_Lane_Stream_5_peek_dout) ^
                       Owner_Lane_Stream_5_peek_empty_n ^
                       (|Owner_Lane_Stream_6_peek_dout) ^
                       Owner_Lane_Stream_6_peek_empty_n ^
                       (|Owner_Lane_Stream_7_peek_dout) ^
                       Owner_Lane_Stream_7_peek_empty_n ^
                       (|Scheduled_Owner_Stream_peek) ^
                       (|lane_hazard) ^
                       scoreboard_empty ^
                       unused_input_padding ^
                       unused_owner;
endmodule
