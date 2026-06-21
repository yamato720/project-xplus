`timescale 1ns/1ps

`ifndef TAPA_SCATTER_MODULE
`define TAPA_SCATTER_MODULE CuperSpmvOnly_TaggedScatterWriterOoo_PipelineScatterModel
`endif

module tb_tapa_scatter_pipeline;
    localparam integer EXPECTED_PAIRS = 8;
    localparam integer EXPECTED_SCALARS = 16;

    reg clk = 1'b0;
    reg rst = 1'b1;
    reg ap_start = 1'b0;
    wire ap_done;
    wire ap_idle;
    wire ap_ready;

    reg [128:0] stream0_mem [0:EXPECTED_PAIRS-1];
    integer stream0_idx = 0;
    wire [128:0] stream0_dout =
        (stream0_idx < EXPECTED_PAIRS) ? stream0_mem[stream0_idx] : 129'd0;
    wire stream0_empty_n = (stream0_idx < EXPECTED_PAIRS);
    wire stream0_read;

    wire [63:0] y_addr;
    wire [32:0] y_data;
    wire y_addr_write;
    wire y_data_write;
    reg y_addr_full_n = 1'b1;
    reg y_data_full_n = 1'b1;
    reg [8:0] y_resp_dout = 9'd0;
    reg y_resp_empty_n = 1'b0;
    wire y_resp_read;

    reg [31:0] y_mem [0:EXPECTED_SCALARS-1];
    reg y_seen [0:EXPECTED_SCALARS-1];
    integer cycle = 0;
    integer y_write_count = 0;
    integer pending_responses = 0;
    integer next_pending_responses = 0;
    integer i;
    integer errors = 0;

    function automatic [128:0] pack_tagged_pair;
        input [31:0] packet_idx;
        input [31:0] pair_lane;
        input [31:0] value0;
        input [31:0] value1;
        begin
            pack_tagged_pair = {1'b0, value1, value0, pair_lane, packet_idx};
        end
    endfunction

    function automatic [31:0] fbits;
        input shortreal value;
        begin
            fbits = $shortrealtobits(value);
        end
    endfunction

    function automatic [31:0] expected_value;
        input integer idx;
        begin
            if ((idx & 1) == 0) begin
                expected_value = fbits(10.0 + (idx >> 1));
            end else begin
                expected_value = fbits(100.0 + (idx >> 1));
            end
        end
    endfunction

    `TAPA_SCATTER_MODULE dut (
        .ap_clk(clk),
        .ap_rst(rst),
        .ap_start(ap_start),
        .ap_done(ap_done),
        .ap_idle(ap_idle),
        .ap_ready(ap_ready),
        .scalar_writes_total(32'd16),
        .Y_out_write_resp_s_dout(y_resp_dout),
        .Y_out_write_resp_s_empty_n(y_resp_empty_n),
        .Y_out_write_resp_s_read(y_resp_read),
        .tagged_pairs_total(32'd8),
        .Vector_Y_Tagged_Stream_0_dout(stream0_dout),
        .Vector_Y_Tagged_Stream_0_empty_n(stream0_empty_n),
        .Vector_Y_Tagged_Stream_0_read(stream0_read),
        .Vector_Y_Tagged_Stream_1_dout(129'd0),
        .Vector_Y_Tagged_Stream_1_empty_n(1'b0),
        .Vector_Y_Tagged_Stream_1_read(),
        .Vector_Y_Tagged_Stream_2_dout(129'd0),
        .Vector_Y_Tagged_Stream_2_empty_n(1'b0),
        .Vector_Y_Tagged_Stream_2_read(),
        .Vector_Y_Tagged_Stream_3_dout(129'd0),
        .Vector_Y_Tagged_Stream_3_empty_n(1'b0),
        .Vector_Y_Tagged_Stream_3_read(),
        .Vector_Y_Tagged_Stream_4_dout(129'd0),
        .Vector_Y_Tagged_Stream_4_empty_n(1'b0),
        .Vector_Y_Tagged_Stream_4_read(),
        .Vector_Y_Tagged_Stream_5_dout(129'd0),
        .Vector_Y_Tagged_Stream_5_empty_n(1'b0),
        .Vector_Y_Tagged_Stream_5_read(),
        .Vector_Y_Tagged_Stream_6_dout(129'd0),
        .Vector_Y_Tagged_Stream_6_empty_n(1'b0),
        .Vector_Y_Tagged_Stream_6_read(),
        .Vector_Y_Tagged_Stream_7_dout(129'd0),
        .Vector_Y_Tagged_Stream_7_empty_n(1'b0),
        .Vector_Y_Tagged_Stream_7_read(),
        .Vector_Y_Tagged_Stream_8_dout(129'd0),
        .Vector_Y_Tagged_Stream_8_empty_n(1'b0),
        .Vector_Y_Tagged_Stream_8_read(),
        .Vector_Y_Tagged_Stream_9_dout(129'd0),
        .Vector_Y_Tagged_Stream_9_empty_n(1'b0),
        .Vector_Y_Tagged_Stream_9_read(),
        .Vector_Y_Tagged_Stream_10_dout(129'd0),
        .Vector_Y_Tagged_Stream_10_empty_n(1'b0),
        .Vector_Y_Tagged_Stream_10_read(),
        .Vector_Y_Tagged_Stream_11_dout(129'd0),
        .Vector_Y_Tagged_Stream_11_empty_n(1'b0),
        .Vector_Y_Tagged_Stream_11_read(),
        .Vector_Y_Tagged_Stream_12_dout(129'd0),
        .Vector_Y_Tagged_Stream_12_empty_n(1'b0),
        .Vector_Y_Tagged_Stream_12_read(),
        .Vector_Y_Tagged_Stream_13_dout(129'd0),
        .Vector_Y_Tagged_Stream_13_empty_n(1'b0),
        .Vector_Y_Tagged_Stream_13_read(),
        .Vector_Y_Tagged_Stream_14_dout(129'd0),
        .Vector_Y_Tagged_Stream_14_empty_n(1'b0),
        .Vector_Y_Tagged_Stream_14_read(),
        .Vector_Y_Tagged_Stream_15_dout(129'd0),
        .Vector_Y_Tagged_Stream_15_empty_n(1'b0),
        .Vector_Y_Tagged_Stream_15_read(),
        .Y_out_write_addr_s_din(y_addr),
        .Y_out_write_addr_s_full_n(y_addr_full_n),
        .Y_out_write_addr_s_write(y_addr_write),
        .Y_out_write_data_s_din(y_data),
        .Y_out_write_data_s_full_n(y_data_full_n),
        .Y_out_write_data_s_write(y_data_write),
        .Y_out_write_addr_offset_load(64'd0)
    );

    always #5 clk = ~clk;

    initial begin
        for (i = 0; i < EXPECTED_PAIRS; i = i + 1) begin
            stream0_mem[i] = pack_tagged_pair(32'd0,
                                              i[31:0],
                                              fbits(10.0 + i),
                                              fbits(100.0 + i));
        end
        for (i = 0; i < EXPECTED_SCALARS; i = i + 1) begin
            y_mem[i] = 32'd0;
            y_seen[i] = 1'b0;
        end
    end

    always @(posedge clk) begin
        cycle <= cycle + 1;
        next_pending_responses = pending_responses;

        if (cycle < 5) begin
            rst <= 1'b1;
            ap_start <= 1'b0;
        end else if (cycle == 5) begin
            rst <= 1'b0;
            ap_start <= 1'b1;
        end else begin
            ap_start <= 1'b0;
        end

        if (!rst) begin
            y_resp_empty_n <= (pending_responses > 0);
            if (stream0_read) begin
                stream0_idx <= stream0_idx + 1;
            end
            if (y_addr_write || y_data_write) begin
                if (!(y_addr_write && y_data_write)) begin
                    $fatal(1, "FAIL: address/data write split");
                end
                if (y_addr[63:2] >= EXPECTED_SCALARS) begin
                    $fatal(1, "FAIL: bad byte address=%0d", y_addr);
                end
                y_mem[y_addr[5:2]] <= y_data[31:0];
                y_seen[y_addr[5:2]] <= 1'b1;
                y_write_count <= y_write_count + 1;
                next_pending_responses = next_pending_responses + 1;
            end
            if (y_resp_read) begin
                if (pending_responses <= 0) begin
                    $fatal(1, "FAIL: response read on empty stream");
                end
                next_pending_responses = next_pending_responses - 1;
            end
            pending_responses <= next_pending_responses;
        end

        if (!rst && ap_done) begin
            if (stream0_idx != EXPECTED_PAIRS) begin
                $fatal(1, "FAIL: stream0_idx=%0d expect=%0d",
                       stream0_idx, EXPECTED_PAIRS);
            end
            if (y_write_count != EXPECTED_SCALARS) begin
                $fatal(1, "FAIL: y_write_count=%0d expect=%0d",
                       y_write_count, EXPECTED_SCALARS);
            end
            for (i = 0; i < EXPECTED_SCALARS; i = i + 1) begin
                if (!y_seen[i] || y_mem[i] !== expected_value(i)) begin
                    $display("FAIL: y[%0d] seen=%0b got=%08x expect=%08x",
                             i, y_seen[i], y_mem[i], expected_value(i));
                    errors = errors + 1;
                end
            end
            if (errors != 0) begin
                $fatal(1, "FAIL: errors=%0d", errors);
            end
            $display("PASS: scatter pipeline cycles=%0d writes=%0d",
                     cycle, y_write_count);
            $finish;
        end

        if (cycle > 1000) begin
            $fatal(1, "FAIL: timeout idx=%0d writes=%0d pending=%0d done=%0b",
                   stream0_idx, y_write_count, pending_responses, ap_done);
        end
    end
endmodule
