`timescale 1ns/1ps

`include "ooo_accum_config.vh"

module tb_ooo_accumulator;
    localparam integer ROW_WIDTH = 16;
    localparam integer VALUE_WIDTH = 32;
    localparam integer ROW_COUNT = `OOO_ACC_ROW_COUNT;
    localparam integer ENTRY_NUM = `OOO_ACC_ENTRY_NUM;
    localparam integer INPUT_COUNT = `OOO_ACC_INPUT_COUNT;
    localparam integer EXPECT_COUNT = `OOO_ACC_EXPECT_COUNT;
    localparam integer PACK_WIDTH = 1 + ROW_WIDTH + VALUE_WIDTH;
    localparam integer EXPECT_PACK_WIDTH = ROW_WIDTH + VALUE_WIDTH;

    reg clk = 1'b0;
    reg rst = 1'b1;

    reg [PACK_WIDTH-1:0] input_mem [0:INPUT_COUNT-1];
    reg [EXPECT_PACK_WIDTH-1:0] expected_mem [0:EXPECT_COUNT-1];

    integer cycle;
    integer input_idx;
    integer expected_idx;
    integer errors;
    integer reset_count;

    string input_path;
    string expected_path;

    wire [PACK_WIDTH-1:0] current_event =
        (input_idx < INPUT_COUNT) ? input_mem[input_idx] : {PACK_WIDTH{1'b0}};
    wire [EXPECT_PACK_WIDTH-1:0] current_expected =
        (expected_idx < EXPECT_COUNT) ? expected_mem[expected_idx] : {EXPECT_PACK_WIDTH{1'b0}};

    wire src_pause = (cycle[2:0] == 3'd3);
    wire in_valid = !rst && (input_idx < INPUT_COUNT) && !src_pause;
    wire in_ready;
    wire in_last = current_event[PACK_WIDTH-1];
    wire [ROW_WIDTH-1:0] in_row = current_event[VALUE_WIDTH +: ROW_WIDTH];
    wire signed [VALUE_WIDTH-1:0] in_value = current_event[VALUE_WIDTH-1:0];

    wire out_valid;
    wire out_ready = !rst && (cycle[2:0] != 3'd5);
    wire out_last;
    wire [ROW_WIDTH-1:0] out_row;
    wire signed [VALUE_WIDTH-1:0] out_value;

    wire busy;
    wire [31:0] dbg_in_count;
    wire [31:0] dbg_hit_count;
    wire [31:0] dbg_miss_count;
    wire [31:0] dbg_evict_count;
    wire [31:0] dbg_stall_count;
    wire [31:0] dbg_out_count;

    ooo_accumulator #(
        .ROW_WIDTH(ROW_WIDTH),
        .VALUE_WIDTH(VALUE_WIDTH),
        .ROW_COUNT(ROW_COUNT),
        .ENTRY_NUM(ENTRY_NUM)
    ) dut (
        .clk(clk),
        .rst(rst),
        .in_valid(in_valid),
        .in_ready(in_ready),
        .in_last(in_last),
        .in_row(in_row),
        .in_value(in_value),
        .out_valid(out_valid),
        .out_ready(out_ready),
        .out_last(out_last),
        .out_row(out_row),
        .out_value(out_value),
        .busy(busy),
        .dbg_in_count(dbg_in_count),
        .dbg_hit_count(dbg_hit_count),
        .dbg_miss_count(dbg_miss_count),
        .dbg_evict_count(dbg_evict_count),
        .dbg_stall_count(dbg_stall_count),
        .dbg_out_count(dbg_out_count)
    );

    always #5 clk = ~clk;

    initial begin
        if (!$value$plusargs("INPUT=%s", input_path)) begin
            input_path = "build/input.mem";
        end
        if (!$value$plusargs("EXPECTED=%s", expected_path)) begin
            expected_path = "build/expected.mem";
        end

        $readmemh(input_path, input_mem);
        $readmemh(expected_path, expected_mem);

        cycle = 0;
        input_idx = 0;
        expected_idx = 0;
        errors = 0;
        reset_count = 0;
    end

    always @(posedge clk) begin
        cycle <= cycle + 1;

        if (reset_count < 5) begin
            reset_count <= reset_count + 1;
            rst <= 1'b1;
        end else begin
            rst <= 1'b0;
        end

        if (!rst && in_valid && in_ready) begin
            input_idx <= input_idx + 1;
        end

        if (!rst && out_valid && out_ready) begin
            if (out_last) begin
                if (expected_idx != EXPECT_COUNT) begin
                    $fatal(1,
                           "FAIL: output ended early, expected_idx=%0d expect_count=%0d",
                           expected_idx, EXPECT_COUNT);
                end

                if (errors == 0 && expected_idx == EXPECT_COUNT) begin
                    $display("PASS: rows=%0d events=%0d entries=%0d cycles=%0d",
                             ROW_COUNT, INPUT_COUNT - 1, ENTRY_NUM, cycle);
                    $display("      in=%0d hit=%0d miss=%0d evict=%0d stall=%0d out=%0d",
                             dbg_in_count, dbg_hit_count, dbg_miss_count,
                             dbg_evict_count, dbg_stall_count, dbg_out_count);
                    $finish;
                end else begin
                    $fatal(1,
                           "FAIL: errors=%0d expected_idx=%0d expect_count=%0d",
                           errors, expected_idx, EXPECT_COUNT);
                end
            end else begin
                if (expected_idx >= EXPECT_COUNT) begin
                    $fatal(1,
                           "FAIL: unexpected extra output row=%0d value=%0d",
                           out_row, out_value);
                end else begin
                    if (out_row !== current_expected[VALUE_WIDTH +: ROW_WIDTH] ||
                        out_value !== $signed(current_expected[VALUE_WIDTH-1:0])) begin
                        $fatal(1,
                               "FAIL: output[%0d] got row=%0d value=%0d, expected row=%0d value=%0d",
                               expected_idx,
                               out_row,
                               out_value,
                               current_expected[VALUE_WIDTH +: ROW_WIDTH],
                               $signed(current_expected[VALUE_WIDTH-1:0]));
                    end
                    expected_idx <= expected_idx + 1;
                end
            end
        end

        if (cycle > `OOO_ACC_TIMEOUT_CYCLES) begin
            $display("FAIL: timeout at cycle=%0d input_idx=%0d expected_idx=%0d busy=%0d",
                     cycle, input_idx, expected_idx, busy);
            $display("      in=%0d hit=%0d miss=%0d evict=%0d stall=%0d out=%0d",
                     dbg_in_count, dbg_hit_count, dbg_miss_count,
                     dbg_evict_count, dbg_stall_count, dbg_out_count);
            $fatal(1, "timeout");
        end
    end
endmodule
