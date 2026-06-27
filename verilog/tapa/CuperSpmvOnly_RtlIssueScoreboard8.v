`timescale 1 ns / 1 ps

// Eight-wide issue scoreboard for dynamic-padding owner-bank scheduling.
//
// This replaces the earlier scalar 8-to-1 issue primitive.  The module examines
// one head from each of eight owner lanes every cycle and issues a full vector
// beat.  Non-conflicting lanes are copied into the output packet; hazardous or
// absent lanes become padding slots in the same beat.  If all available heads
// are blocked by the scoreboard, the module emits an all-padding beat so the
// downstream accumulator pipeline can advance and the hazard window can age.
//
// Per-lane output packets use the existing CuperSpmvOnly_TaggedScalar packing:
//   [129]    padding/empty slot marker for this vector protocol
//   [128:97] value bits
//   [96:65]  scalar_lane
//   [64:33]  pair_lane
//   [32:1]   packet_idx
//   [0]      done
//
// A scoreboard entry protects one {lane, addr, ping/pong} key.  Cross-lane
// same-address updates are allowed because the vector accumulator keeps
// lane-local storage banks partitioned by lane.
module CuperSpmvOnly_RtlIssueScoreboard8
#(
    parameter integer ADDR_WIDTH = 13,
    parameter integer SCOREBOARD_DEPTH = 12,
    parameter integer TAGGED_WIDTH = 130,
    parameter integer TAGGED_PAD_BIT = 129
)
(
    input wire clk,
    input wire rst,

    input wire [7:0] head_valid,
    input wire [(8 * ADDR_WIDTH)-1:0] head_addr,
    input wire [7:0] head_is_pong,
    input wire [7:0] head_done,
    input wire [(8 * TAGGED_WIDTH)-1:0] head_payload,

    input wire issue_ready,

    output wire issue_valid,
    output wire [(8 * TAGGED_WIDTH)-1:0] issue_payload,
    output wire [7:0] lane_hazard,
    output wire scoreboard_empty
);
    reg [7:0] sb_valid [0:SCOREBOARD_DEPTH-1];
    reg [ADDR_WIDTH-1:0] sb_addr [0:SCOREBOARD_DEPTH-1][0:7];
    reg sb_is_pong [0:SCOREBOARD_DEPTH-1][0:7];

    reg [7:0] lane_hazard_r;
    reg [7:0] lane_eligible_r;
    reg [(8 * TAGGED_WIDTH)-1:0] issue_payload_r;

    integer lane_i;
    integer sb_i;
    integer stage_i;

    function [ADDR_WIDTH-1:0] lane_addr;
        input [(8 * ADDR_WIDTH)-1:0] all_addr;
        input [2:0] lane;
        begin
            lane_addr = all_addr[lane * ADDR_WIDTH +: ADDR_WIDTH];
        end
    endfunction

    always @(*) begin
        lane_hazard_r = 8'd0;

        for (lane_i = 0; lane_i < 8; lane_i = lane_i + 1) begin
            if (head_valid[lane_i] && !head_done[lane_i]) begin
                for (sb_i = 0; sb_i < SCOREBOARD_DEPTH; sb_i = sb_i + 1) begin
                    if (sb_valid[sb_i][lane_i] &&
                        sb_addr[sb_i][lane_i] ==
                            lane_addr(head_addr, lane_i[2:0]) &&
                        sb_is_pong[sb_i][lane_i] ==
                            head_is_pong[lane_i]) begin
                        lane_hazard_r[lane_i] = 1'b1;
                    end
                end
            end
        end
    end

    always @(*) begin
        for (lane_i = 0; lane_i < 8; lane_i = lane_i + 1) begin
            lane_eligible_r[lane_i] =
                head_valid[lane_i] &&
                (head_done[lane_i] || !lane_hazard_r[lane_i]);
        end
    end

    always @(*) begin
        issue_payload_r = {(8 * TAGGED_WIDTH){1'b0}};
        for (lane_i = 0; lane_i < 8; lane_i = lane_i + 1) begin
            issue_payload_r[lane_i * TAGGED_WIDTH + TAGGED_PAD_BIT] = 1'b1;
            if (lane_eligible_r[lane_i]) begin
                issue_payload_r[lane_i * TAGGED_WIDTH +: TAGGED_WIDTH] =
                    head_payload[lane_i * TAGGED_WIDTH +: TAGGED_WIDTH];
                issue_payload_r[lane_i * TAGGED_WIDTH + TAGGED_PAD_BIT] = 1'b0;
            end
        end
    end

    assign lane_hazard = lane_hazard_r;
    assign issue_valid = (lane_eligible_r != 8'd0) ||
                         ((head_valid != 8'd0) && (lane_eligible_r == 8'd0));
    assign issue_payload = issue_payload_r;

    reg scoreboard_empty_r;
    always @(*) begin
        scoreboard_empty_r = 1'b1;
        for (sb_i = 0; sb_i < SCOREBOARD_DEPTH; sb_i = sb_i + 1) begin
            if (sb_valid[sb_i] != 8'd0) begin
                scoreboard_empty_r = 1'b0;
            end
        end
    end
    assign scoreboard_empty = scoreboard_empty_r;

    always @(posedge clk) begin
        if (rst) begin
            for (stage_i = 0; stage_i < SCOREBOARD_DEPTH; stage_i = stage_i + 1) begin
                sb_valid[stage_i] <= 8'd0;
                for (lane_i = 0; lane_i < 8; lane_i = lane_i + 1) begin
                    sb_addr[stage_i][lane_i] <= {ADDR_WIDTH{1'b0}};
                    sb_is_pong[stage_i][lane_i] <= 1'b0;
                end
            end
        end else if (issue_valid && issue_ready) begin
            for (stage_i = SCOREBOARD_DEPTH - 1; stage_i > 0; stage_i = stage_i - 1) begin
                sb_valid[stage_i] <= sb_valid[stage_i - 1];
                for (lane_i = 0; lane_i < 8; lane_i = lane_i + 1) begin
                    sb_addr[stage_i][lane_i] <= sb_addr[stage_i - 1][lane_i];
                    sb_is_pong[stage_i][lane_i] <= sb_is_pong[stage_i - 1][lane_i];
                end
            end

            for (lane_i = 0; lane_i < 8; lane_i = lane_i + 1) begin
                sb_valid[0][lane_i] <= lane_eligible_r[lane_i] &&
                                       !head_done[lane_i];
                sb_addr[0][lane_i] <= lane_addr(head_addr, lane_i[2:0]);
                sb_is_pong[0][lane_i] <= head_is_pong[lane_i];
            end
        end
    end
endmodule
