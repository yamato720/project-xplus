`timescale 1 ns / 1 ps

// Eight-head issue scoreboard for owner-bank accumulator experiments.
//
// The module chooses one of eight FIFO heads each cycle.  A non-done head is
// eligible only when its {lane, addr, ping/pong} tag is absent from the
// in-flight scoreboard.  Done heads are eligible regardless of the scoreboard
// and do not allocate a scoreboard entry.
//
// This is intentionally a scheduling primitive.  It does not perform the
// accumulator read/modify/write itself; integration code should pop the selected
// lane and feed the selected payload into the downstream accumulator pipeline.
module CuperSpmvOnly_RtlIssueScoreboard8
#(
    parameter integer ADDR_WIDTH = 13,
    parameter integer SCOREBOARD_DEPTH = 12
)
(
    input wire clk,
    input wire rst,

    input wire [7:0] head_valid,
    input wire [(8 * ADDR_WIDTH)-1:0] head_addr,
    input wire [7:0] head_is_pong,
    input wire [7:0] head_done,

    // issue_ready is the downstream handshake.  pipe_advance ages the in-flight
    // scoreboard and must be asserted when a non-done issue enters the pipeline.
    input wire issue_ready,
    input wire pipe_advance,

    output wire issue_valid,
    output wire [2:0] issue_lane,
    output wire issue_is_done,
    output wire [7:0] pop_lane,
    output wire [7:0] lane_hazard,
    output wire [7:0] lane_eligible,
    output wire scoreboard_empty
);
    reg [2:0] rr_ptr;
    reg [SCOREBOARD_DEPTH-1:0] sb_valid;
    reg [2:0] sb_lane [0:SCOREBOARD_DEPTH-1];
    reg [ADDR_WIDTH-1:0] sb_addr [0:SCOREBOARD_DEPTH-1];
    reg sb_is_pong [0:SCOREBOARD_DEPTH-1];

    reg [7:0] lane_hazard_r;
    reg [7:0] lane_eligible_r;
    reg choose_valid;
    reg [2:0] choose_lane;

    integer lane_i;
    integer sb_i;
    integer choose_i;
    integer seq_i;
    reg [2:0] candidate_lane;

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
                    if (sb_valid[sb_i] &&
                        sb_lane[sb_i] == lane_i[2:0] &&
                        sb_addr[sb_i] == lane_addr(head_addr, lane_i[2:0]) &&
                        sb_is_pong[sb_i] == head_is_pong[lane_i]) begin
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
                (head_done[lane_i] ||
                 (pipe_advance && !lane_hazard_r[lane_i]));
        end
    end

    always @(*) begin
        choose_valid = 1'b0;
        choose_lane = rr_ptr;

        for (choose_i = 0; choose_i < 8; choose_i = choose_i + 1) begin
            candidate_lane = rr_ptr + choose_i[2:0];
            if (!choose_valid && lane_eligible_r[candidate_lane]) begin
                choose_valid = 1'b1;
                choose_lane = candidate_lane;
            end
        end
    end

    assign issue_valid = choose_valid;
    assign issue_lane = choose_lane;
    assign issue_is_done = choose_valid && head_done[choose_lane];
    assign pop_lane = (choose_valid && issue_ready) ? (8'b0000_0001 << choose_lane) : 8'd0;
    assign lane_hazard = lane_hazard_r;
    assign lane_eligible = lane_eligible_r;
    assign scoreboard_empty = ~(|sb_valid);

    always @(posedge clk) begin
        if (rst) begin
            rr_ptr <= 3'd0;
            sb_valid <= {SCOREBOARD_DEPTH{1'b0}};
            for (seq_i = 0; seq_i < SCOREBOARD_DEPTH; seq_i = seq_i + 1) begin
                sb_lane[seq_i] <= 3'd0;
                sb_addr[seq_i] <= {ADDR_WIDTH{1'b0}};
                sb_is_pong[seq_i] <= 1'b0;
            end
        end else begin
            if (pipe_advance) begin
                for (seq_i = SCOREBOARD_DEPTH - 1; seq_i > 0; seq_i = seq_i - 1) begin
                    sb_valid[seq_i] <= sb_valid[seq_i - 1];
                    sb_lane[seq_i] <= sb_lane[seq_i - 1];
                    sb_addr[seq_i] <= sb_addr[seq_i - 1];
                    sb_is_pong[seq_i] <= sb_is_pong[seq_i - 1];
                end

                sb_valid[0] <= choose_valid && issue_ready && !head_done[choose_lane];
                sb_lane[0] <= choose_lane;
                sb_addr[0] <= lane_addr(head_addr, choose_lane);
                sb_is_pong[0] <= head_is_pong[choose_lane];
            end

            if (choose_valid && issue_ready) begin
                rr_ptr <= choose_lane + 3'd1;
            end
        end
    end
endmodule
