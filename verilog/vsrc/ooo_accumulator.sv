`timescale 1ns/1ps

// Out-of-order row accumulator prototype.
//
// This module models the post-Core SpMV accumulator as a small CAM backed by
// a row-indexed partial-sum memory.  It accepts row/value contributions in any
// order, combines hits in the CAM, spills one victim entry when the CAM is
// full, and emits final row sums after the input stream sends an in_last token.
//
// The first prototype uses signed integer addition so that the scheduling and
// hazard behavior can be debugged without bringing in a floating-point IP.  The
// add sites are intentionally local and can later be replaced by FP32 add
// pipelines with an explicit pending/scoreboard queue.
module ooo_accumulator #(
    parameter integer ROW_WIDTH = 16,
    parameter integer VALUE_WIDTH = 32,
    parameter integer ROW_COUNT = 128,
    parameter integer ENTRY_NUM = 8
) (
    input  wire                         clk,
    input  wire                         rst,

    input  wire                         in_valid,
    output wire                         in_ready,
    input  wire                         in_last,
    input  wire [ROW_WIDTH-1:0]         in_row,
    input  wire signed [VALUE_WIDTH-1:0] in_value,

    output reg                          out_valid,
    input  wire                         out_ready,
    output reg                          out_last,
    output reg  [ROW_WIDTH-1:0]         out_row,
    output reg  signed [VALUE_WIDTH-1:0] out_value,

    output wire                         busy,
    output reg  [31:0]                  dbg_in_count,
    output reg  [31:0]                  dbg_hit_count,
    output reg  [31:0]                  dbg_miss_count,
    output reg  [31:0]                  dbg_evict_count,
    output reg  [31:0]                  dbg_stall_count,
    output reg  [31:0]                  dbg_out_count
);
    localparam integer ENTRY_IDX_WIDTH = (ENTRY_NUM <= 1) ? 1 : $clog2(ENTRY_NUM);
    localparam integer ROW_IDX_WIDTH = (ROW_COUNT <= 1) ? 1 : $clog2(ROW_COUNT);
    localparam integer ENTRY_COUNT_WIDTH = (ENTRY_NUM <= 1) ? 1 : $clog2(ENTRY_NUM + 1);
    localparam integer ROW_COUNT_WIDTH = (ROW_COUNT <= 1) ? 1 : $clog2(ROW_COUNT + 1);

    localparam [2:0] ST_INPUT          = 3'd0;
    localparam [2:0] ST_REPLACE_COMMIT = 3'd1;
    localparam [2:0] ST_DRAIN_ENTRIES  = 3'd2;
    localparam [2:0] ST_SCAN_ROWS      = 3'd3;
    localparam [2:0] ST_DONE           = 3'd4;

    reg [2:0] state;

    reg                            entry_valid [0:ENTRY_NUM-1];
    reg [ROW_WIDTH-1:0]            entry_row   [0:ENTRY_NUM-1];
    reg signed [VALUE_WIDTH-1:0]   entry_sum   [0:ENTRY_NUM-1];

    reg signed [VALUE_WIDTH-1:0]   mem_value   [0:ROW_COUNT-1];
    reg                            mem_touched [0:ROW_COUNT-1];

    reg [ENTRY_IDX_WIDTH-1:0] victim_idx;
    reg [ENTRY_COUNT_WIDTH-1:0] drain_idx;
    reg [ROW_COUNT_WIDTH-1:0]   scan_idx;

    reg [ROW_WIDTH-1:0]          held_row;
    reg signed [VALUE_WIDTH-1:0] held_value;

    reg                          hit_found;
    reg [ENTRY_IDX_WIDTH-1:0]    hit_idx;
    reg                          free_found;
    reg [ENTRY_IDX_WIDTH-1:0]    free_idx;

    integer i;

    always @(*) begin
        hit_found = 1'b0;
        hit_idx = {ENTRY_IDX_WIDTH{1'b0}};
        free_found = 1'b0;
        free_idx = {ENTRY_IDX_WIDTH{1'b0}};

        for (i = 0; i < ENTRY_NUM; i = i + 1) begin
            if (entry_valid[i] && entry_row[i] == in_row && !hit_found) begin
                hit_found = 1'b1;
                hit_idx = i[ENTRY_IDX_WIDTH-1:0];
            end
            if (!entry_valid[i] && !free_found) begin
                free_found = 1'b1;
                free_idx = i[ENTRY_IDX_WIDTH-1:0];
            end
        end
    end

    assign in_ready = (state == ST_INPUT);
    assign busy = (state != ST_DONE) || out_valid;

    always @(posedge clk) begin
        if (rst) begin
            state <= ST_INPUT;
            victim_idx <= {ENTRY_IDX_WIDTH{1'b0}};
            drain_idx <= {ENTRY_COUNT_WIDTH{1'b0}};
            scan_idx <= {ROW_COUNT_WIDTH{1'b0}};
            held_row <= {ROW_WIDTH{1'b0}};
            held_value <= {VALUE_WIDTH{1'b0}};

            out_valid <= 1'b0;
            out_last <= 1'b0;
            out_row <= {ROW_WIDTH{1'b0}};
            out_value <= {VALUE_WIDTH{1'b0}};

            dbg_in_count <= 32'd0;
            dbg_hit_count <= 32'd0;
            dbg_miss_count <= 32'd0;
            dbg_evict_count <= 32'd0;
            dbg_stall_count <= 32'd0;
            dbg_out_count <= 32'd0;

            for (i = 0; i < ENTRY_NUM; i = i + 1) begin
                entry_valid[i] <= 1'b0;
                entry_row[i] <= {ROW_WIDTH{1'b0}};
                entry_sum[i] <= {VALUE_WIDTH{1'b0}};
            end
            for (i = 0; i < ROW_COUNT; i = i + 1) begin
                mem_value[i] <= {VALUE_WIDTH{1'b0}};
                mem_touched[i] <= 1'b0;
            end
        end else begin
            case (state)
                ST_INPUT: begin
                    if (in_valid) begin
                        if (in_last) begin
                            drain_idx <= {ENTRY_COUNT_WIDTH{1'b0}};
                            state <= ST_DRAIN_ENTRIES;
                        end else begin
                            dbg_in_count <= dbg_in_count + 32'd1;
                            if (hit_found) begin
                                entry_sum[hit_idx] <= entry_sum[hit_idx] + in_value;
                                dbg_hit_count <= dbg_hit_count + 32'd1;
                            end else if (free_found) begin
                                entry_valid[free_idx] <= 1'b1;
                                entry_row[free_idx] <= in_row;
                                entry_sum[free_idx] <= mem_value[in_row[ROW_IDX_WIDTH-1:0]] + in_value;
                                mem_touched[in_row[ROW_IDX_WIDTH-1:0]] <= 1'b1;
                                dbg_miss_count <= dbg_miss_count + 32'd1;
                            end else begin
                                held_row <= in_row;
                                held_value <= in_value;
                                state <= ST_REPLACE_COMMIT;
                                dbg_miss_count <= dbg_miss_count + 32'd1;
                                dbg_stall_count <= dbg_stall_count + 32'd1;
                            end
                        end
                    end
                end

                ST_REPLACE_COMMIT: begin
                    mem_value[entry_row[victim_idx][ROW_IDX_WIDTH-1:0]] <= entry_sum[victim_idx];
                    mem_touched[entry_row[victim_idx][ROW_IDX_WIDTH-1:0]] <= 1'b1;

                    entry_valid[victim_idx] <= 1'b1;
                    entry_row[victim_idx] <= held_row;
                    entry_sum[victim_idx] <= mem_value[held_row[ROW_IDX_WIDTH-1:0]] + held_value;
                    mem_touched[held_row[ROW_IDX_WIDTH-1:0]] <= 1'b1;

                    victim_idx <= (victim_idx == ENTRY_NUM - 1) ?
                                  {ENTRY_IDX_WIDTH{1'b0}} :
                                  victim_idx + {{(ENTRY_IDX_WIDTH-1){1'b0}}, 1'b1};
                    dbg_evict_count <= dbg_evict_count + 32'd1;
                    state <= ST_INPUT;
                end

                ST_DRAIN_ENTRIES: begin
                    if (drain_idx < ENTRY_NUM) begin
                        if (entry_valid[drain_idx[ENTRY_IDX_WIDTH-1:0]]) begin
                            mem_value[entry_row[drain_idx[ENTRY_IDX_WIDTH-1:0]][ROW_IDX_WIDTH-1:0]] <= entry_sum[drain_idx[ENTRY_IDX_WIDTH-1:0]];
                            mem_touched[entry_row[drain_idx[ENTRY_IDX_WIDTH-1:0]][ROW_IDX_WIDTH-1:0]] <= 1'b1;
                            entry_valid[drain_idx[ENTRY_IDX_WIDTH-1:0]] <= 1'b0;
                        end
                        drain_idx <= drain_idx + {{(ENTRY_COUNT_WIDTH-1){1'b0}}, 1'b1};
                    end else begin
                        scan_idx <= {ROW_COUNT_WIDTH{1'b0}};
                        state <= ST_SCAN_ROWS;
                    end
                end

                ST_SCAN_ROWS: begin
                    if (!out_valid || out_ready) begin
                        out_valid <= 1'b0;
                        out_last <= 1'b0;
                        if (scan_idx < ROW_COUNT) begin
                            if (mem_touched[scan_idx[ROW_IDX_WIDTH-1:0]]) begin
                                out_valid <= 1'b1;
                                out_last <= 1'b0;
                                out_row <= scan_idx;
                                out_value <= mem_value[scan_idx[ROW_IDX_WIDTH-1:0]];
                                dbg_out_count <= dbg_out_count + 32'd1;
                            end
                            scan_idx <= scan_idx + {{(ROW_COUNT_WIDTH-1){1'b0}}, 1'b1};
                        end else begin
                            out_valid <= 1'b1;
                            out_last <= 1'b1;
                            out_row <= {ROW_WIDTH{1'b0}};
                            out_value <= {VALUE_WIDTH{1'b0}};
                            state <= ST_DONE;
                        end
                    end
                end

                ST_DONE: begin
                    if (out_valid && out_ready) begin
                        out_valid <= 1'b0;
                        out_last <= 1'b0;
                    end
                end

                default: begin
                    state <= ST_DONE;
                end
            endcase
        end
    end
endmodule
