`timescale 1ns/1ps

`ifndef TAPA_SCATTER_MODULE
`define TAPA_SCATTER_MODULE CuperSpmvOnly_TaggedScatterWriterOoo_PipelineScatterModel
`endif

module tb_tapa_owner_bank_to_scatter;
    localparam integer OWNER_ID = 0;
    localparam integer ROW_NUM = 16;
    localparam integer LANES = 8;
    localparam integer INPUT_PER_LANE = 3;
    localparam integer STREAMS = 16;
    localparam integer EXPECTED_PAIRS = 8;
    localparam integer EXPECTED_SCALARS = 16;

    reg clk = 1'b0;
    reg rst_n = 1'b0;
    reg bank_start = 1'b0;
    reg scatter_start = 1'b0;

    wire bank_done;
    wire bank_idle;
    wire bank_ready;
    wire scatter_done;
    wire scatter_idle;
    wire scatter_ready;

    reg [31:0] iteration_num = 32'd1;
    reg [31:0] row_num = ROW_NUM;
    reg [31:0] owner_id = OWNER_ID;

    reg [129:0] input_words [0:LANES-1][0:INPUT_PER_LANE-1];
    integer input_idx [0:LANES-1];
    integer lane_init;
    integer cycle = 0;
    integer errors = 0;
    integer bank_output_count = 0;
    integer scatter_read_count = 0;
    integer y_write_count = 0;
    integer loop_i;
    integer next_pending_responses;

    wire [129:0] lane_dout [0:LANES-1];
    wire lane_empty_n [0:LANES-1];
    wire lane_read [0:LANES-1];

    wire [128:0] bank_out_din;
    wire bank_out_write;
    wire bank_out_full_n;

    wire [128:0] stream0_dout;
    wire stream0_empty_n;
    wire stream0_read;
    wire stream0_full_n;
    wire unused_stream_read_1;
    wire unused_stream_read_2;
    wire unused_stream_read_3;
    wire unused_stream_read_4;
    wire unused_stream_read_5;
    wire unused_stream_read_6;
    wire unused_stream_read_7;
    wire unused_stream_read_8;
    wire unused_stream_read_9;
    wire unused_stream_read_10;
    wire unused_stream_read_11;
    wire unused_stream_read_12;
    wire unused_stream_read_13;
    wire unused_stream_read_14;
    wire unused_stream_read_15;

    wire [63:0] y_addr;
    wire [32:0] y_data;
    wire y_addr_write;
    wire y_data_write;
    reg y_addr_full_n = 1'b1;
    reg y_data_full_n = 1'b1;
    reg [8:0] y_resp_dout = 9'd0;
    reg y_resp_empty_n = 1'b0;
    wire y_resp_read;
    integer pending_responses = 0;

    reg [31:0] y_mem [0:EXPECTED_SCALARS-1];
    reg y_seen [0:EXPECTED_SCALARS-1];
    integer y_init;

    function automatic [129:0] pack_scalar;
        input bit done;
        input [31:0] packet_idx;
        input [31:0] pair_lane;
        input [31:0] scalar_lane;
        input [31:0] value;
        begin
            pack_scalar = {1'b0, value, scalar_lane, pair_lane, packet_idx, done};
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

    genvar lane;
    generate
        for (lane = 0; lane < LANES; lane = lane + 1) begin : lane_inputs
            assign lane_dout[lane] =
                (input_idx[lane] < INPUT_PER_LANE) ?
                input_words[lane][input_idx[lane]] : 130'd0;
            assign lane_empty_n[lane] = (input_idx[lane] < INPUT_PER_LANE);
        end
    endgenerate

    tapa_stream_fifo #(
        .DATA_WIDTH(129),
        .DEPTH(64)
    ) stream0_fifo_u (
        .clk(clk),
        .rst_n(rst_n),
        .s_din(bank_out_din),
        .s_full_n(stream0_full_n),
        .s_write(bank_out_write),
        .m_dout(stream0_dout),
        .m_empty_n(stream0_empty_n),
        .m_read(stream0_read)
    );
    assign bank_out_full_n = stream0_full_n;

    CuperSpmvOnly_RtlOwnerBankAccumulatorOoo bank_u (
        .ap_clk(clk),
        .ap_rst_n(rst_n),
        .ap_start(bank_start),
        .ap_done(bank_done),
        .ap_idle(bank_idle),
        .ap_ready(bank_ready),
        .Iteration_num(iteration_num),
        .Row_num(row_num),
        .Owner_Lane_Stream_0_s_dout(lane_dout[0]),
        .Owner_Lane_Stream_0_s_empty_n(lane_empty_n[0]),
        .Owner_Lane_Stream_0_s_read(lane_read[0]),
        .Owner_Lane_Stream_0_peek_dout(130'd0),
        .Owner_Lane_Stream_0_peek_empty_n(1'b0),
        .Owner_Lane_Stream_0_peek_read(),
        .Owner_Lane_Stream_1_s_dout(lane_dout[1]),
        .Owner_Lane_Stream_1_s_empty_n(lane_empty_n[1]),
        .Owner_Lane_Stream_1_s_read(lane_read[1]),
        .Owner_Lane_Stream_1_peek_dout(130'd0),
        .Owner_Lane_Stream_1_peek_empty_n(1'b0),
        .Owner_Lane_Stream_1_peek_read(),
        .Owner_Lane_Stream_2_s_dout(lane_dout[2]),
        .Owner_Lane_Stream_2_s_empty_n(lane_empty_n[2]),
        .Owner_Lane_Stream_2_s_read(lane_read[2]),
        .Owner_Lane_Stream_2_peek_dout(130'd0),
        .Owner_Lane_Stream_2_peek_empty_n(1'b0),
        .Owner_Lane_Stream_2_peek_read(),
        .Owner_Lane_Stream_3_s_dout(lane_dout[3]),
        .Owner_Lane_Stream_3_s_empty_n(lane_empty_n[3]),
        .Owner_Lane_Stream_3_s_read(lane_read[3]),
        .Owner_Lane_Stream_3_peek_dout(130'd0),
        .Owner_Lane_Stream_3_peek_empty_n(1'b0),
        .Owner_Lane_Stream_3_peek_read(),
        .Owner_Lane_Stream_4_s_dout(lane_dout[4]),
        .Owner_Lane_Stream_4_s_empty_n(lane_empty_n[4]),
        .Owner_Lane_Stream_4_s_read(lane_read[4]),
        .Owner_Lane_Stream_4_peek_dout(130'd0),
        .Owner_Lane_Stream_4_peek_empty_n(1'b0),
        .Owner_Lane_Stream_4_peek_read(),
        .Owner_Lane_Stream_5_s_dout(lane_dout[5]),
        .Owner_Lane_Stream_5_s_empty_n(lane_empty_n[5]),
        .Owner_Lane_Stream_5_s_read(lane_read[5]),
        .Owner_Lane_Stream_5_peek_dout(130'd0),
        .Owner_Lane_Stream_5_peek_empty_n(1'b0),
        .Owner_Lane_Stream_5_peek_read(),
        .Owner_Lane_Stream_6_s_dout(lane_dout[6]),
        .Owner_Lane_Stream_6_s_empty_n(lane_empty_n[6]),
        .Owner_Lane_Stream_6_s_read(lane_read[6]),
        .Owner_Lane_Stream_6_peek_dout(130'd0),
        .Owner_Lane_Stream_6_peek_empty_n(1'b0),
        .Owner_Lane_Stream_6_peek_read(),
        .Owner_Lane_Stream_7_s_dout(lane_dout[7]),
        .Owner_Lane_Stream_7_s_empty_n(lane_empty_n[7]),
        .Owner_Lane_Stream_7_s_read(lane_read[7]),
        .Owner_Lane_Stream_7_peek_dout(130'd0),
        .Owner_Lane_Stream_7_peek_empty_n(1'b0),
        .Owner_Lane_Stream_7_peek_read(),
        .Vector_Y_Tagged_Stream_s_din(bank_out_din),
        .Vector_Y_Tagged_Stream_s_full_n(bank_out_full_n),
        .Vector_Y_Tagged_Stream_s_write(bank_out_write),
        .Vector_Y_Tagged_Stream_peek(129'd0),
        .Owner_id(owner_id)
    );

    `TAPA_SCATTER_MODULE scatter_u (
        .ap_clk(clk),
        .ap_rst(~rst_n),
        .ap_start(scatter_start),
        .ap_done(scatter_done),
        .ap_idle(scatter_idle),
        .ap_ready(scatter_ready),
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
        .Vector_Y_Tagged_Stream_1_read(unused_stream_read_1),
        .Vector_Y_Tagged_Stream_2_dout(129'd0),
        .Vector_Y_Tagged_Stream_2_empty_n(1'b0),
        .Vector_Y_Tagged_Stream_2_read(unused_stream_read_2),
        .Vector_Y_Tagged_Stream_3_dout(129'd0),
        .Vector_Y_Tagged_Stream_3_empty_n(1'b0),
        .Vector_Y_Tagged_Stream_3_read(unused_stream_read_3),
        .Vector_Y_Tagged_Stream_4_dout(129'd0),
        .Vector_Y_Tagged_Stream_4_empty_n(1'b0),
        .Vector_Y_Tagged_Stream_4_read(unused_stream_read_4),
        .Vector_Y_Tagged_Stream_5_dout(129'd0),
        .Vector_Y_Tagged_Stream_5_empty_n(1'b0),
        .Vector_Y_Tagged_Stream_5_read(unused_stream_read_5),
        .Vector_Y_Tagged_Stream_6_dout(129'd0),
        .Vector_Y_Tagged_Stream_6_empty_n(1'b0),
        .Vector_Y_Tagged_Stream_6_read(unused_stream_read_6),
        .Vector_Y_Tagged_Stream_7_dout(129'd0),
        .Vector_Y_Tagged_Stream_7_empty_n(1'b0),
        .Vector_Y_Tagged_Stream_7_read(unused_stream_read_7),
        .Vector_Y_Tagged_Stream_8_dout(129'd0),
        .Vector_Y_Tagged_Stream_8_empty_n(1'b0),
        .Vector_Y_Tagged_Stream_8_read(unused_stream_read_8),
        .Vector_Y_Tagged_Stream_9_dout(129'd0),
        .Vector_Y_Tagged_Stream_9_empty_n(1'b0),
        .Vector_Y_Tagged_Stream_9_read(unused_stream_read_9),
        .Vector_Y_Tagged_Stream_10_dout(129'd0),
        .Vector_Y_Tagged_Stream_10_empty_n(1'b0),
        .Vector_Y_Tagged_Stream_10_read(unused_stream_read_10),
        .Vector_Y_Tagged_Stream_11_dout(129'd0),
        .Vector_Y_Tagged_Stream_11_empty_n(1'b0),
        .Vector_Y_Tagged_Stream_11_read(unused_stream_read_11),
        .Vector_Y_Tagged_Stream_12_dout(129'd0),
        .Vector_Y_Tagged_Stream_12_empty_n(1'b0),
        .Vector_Y_Tagged_Stream_12_read(unused_stream_read_12),
        .Vector_Y_Tagged_Stream_13_dout(129'd0),
        .Vector_Y_Tagged_Stream_13_empty_n(1'b0),
        .Vector_Y_Tagged_Stream_13_read(unused_stream_read_13),
        .Vector_Y_Tagged_Stream_14_dout(129'd0),
        .Vector_Y_Tagged_Stream_14_empty_n(1'b0),
        .Vector_Y_Tagged_Stream_14_read(unused_stream_read_14),
        .Vector_Y_Tagged_Stream_15_dout(129'd0),
        .Vector_Y_Tagged_Stream_15_empty_n(1'b0),
        .Vector_Y_Tagged_Stream_15_read(unused_stream_read_15),
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
        for (lane_init = 0; lane_init < LANES; lane_init = lane_init + 1) begin
            input_idx[lane_init] = 0;
            input_words[lane_init][0] =
                pack_scalar(1'b0, 32'd0, lane_init, 32'd0, fbits(10.0 + lane_init));
            input_words[lane_init][1] =
                pack_scalar(1'b0, 32'd0, lane_init, 32'd1, fbits(100.0 + lane_init));
            input_words[lane_init][2] =
                pack_scalar(1'b1, 32'd0, lane_init, 32'd0, 32'd0);
        end
        for (y_init = 0; y_init < EXPECTED_SCALARS; y_init = y_init + 1) begin
            y_mem[y_init] = 32'd0;
            y_seen[y_init] = 1'b0;
        end
    end

    always @(posedge clk) begin
        cycle <= cycle + 1;
        next_pending_responses = pending_responses;

        if (cycle < 5) begin
            rst_n <= 1'b0;
            bank_start <= 1'b0;
            scatter_start <= 1'b0;
        end else if (cycle == 5) begin
            rst_n <= 1'b1;
            bank_start <= 1'b1;
            scatter_start <= 1'b1;
        end else begin
            bank_start <= 1'b0;
            scatter_start <= 1'b0;
        end

        if (rst_n) begin
            for (loop_i = 0; loop_i < LANES; loop_i = loop_i + 1) begin
                if (lane_read[loop_i]) begin
                    input_idx[loop_i] <= input_idx[loop_i] + 1;
                end
            end
            if (bank_out_write) begin
                bank_output_count <= bank_output_count + 1;
            end
            if (stream0_read) begin
                scatter_read_count <= scatter_read_count + 1;
            end

            y_resp_empty_n <= (pending_responses > 0);
            if (y_addr_write || y_data_write) begin
                if (!(y_addr_write && y_data_write)) begin
                    $fatal(1, "FAIL: address/data write split addr=%0b data=%0b",
                           y_addr_write, y_data_write);
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

        if (rst_n && scatter_done) begin
            for (loop_i = 0; loop_i < LANES; loop_i = loop_i + 1) begin
                if (input_idx[loop_i] != INPUT_PER_LANE) begin
                    $fatal(1, "FAIL: lane %0d input_idx=%0d expect=%0d",
                           loop_i, input_idx[loop_i], INPUT_PER_LANE);
                end
            end
            if (bank_output_count != EXPECTED_PAIRS) begin
                $fatal(1, "FAIL: bank_output_count=%0d expect=%0d",
                       bank_output_count, EXPECTED_PAIRS);
            end
            if (scatter_read_count != EXPECTED_PAIRS) begin
                $fatal(1, "FAIL: scatter_read_count=%0d expect=%0d",
                       scatter_read_count, EXPECTED_PAIRS);
            end
            if (y_write_count != EXPECTED_SCALARS) begin
                $fatal(1, "FAIL: y_write_count=%0d expect=%0d",
                       y_write_count, EXPECTED_SCALARS);
            end
            for (loop_i = 0; loop_i < EXPECTED_SCALARS; loop_i = loop_i + 1) begin
                if (!y_seen[loop_i] || y_mem[loop_i] !== expected_value(loop_i)) begin
                    $display("FAIL: y[%0d] seen=%0b got=%08x expect=%08x",
                             loop_i,
                             y_seen[loop_i],
                             y_mem[loop_i],
                             expected_value(loop_i));
                    errors = errors + 1;
                end
            end
            if (errors != 0) begin
                $fatal(1, "FAIL: errors=%0d", errors);
            end
            $display("PASS: owner-bank RTL to generated scatter cycles=%0d bank_pairs=%0d y_writes=%0d",
                     cycle, bank_output_count, y_write_count);
            $finish;
        end

        if (cycle > 4000) begin
            $fatal(1, "FAIL: timeout bank_pairs=%0d scatter_reads=%0d y_writes=%0d pending_resp=%0d bank_done=%0b scatter_done=%0b",
                   bank_output_count,
                   scatter_read_count,
                   y_write_count,
                   pending_responses,
                   bank_done,
                   scatter_done);
        end
    end
endmodule

module tapa_stream_fifo #(
    parameter integer DATA_WIDTH = 129,
    parameter integer DEPTH = 64
) (
    input wire clk,
    input wire rst_n,
    input wire [DATA_WIDTH-1:0] s_din,
    output wire s_full_n,
    input wire s_write,
    output wire [DATA_WIDTH-1:0] m_dout,
    output wire m_empty_n,
    input wire m_read
);
    localparam integer ADDR_WIDTH = 6;

    reg [DATA_WIDTH-1:0] mem [0:DEPTH-1];
    reg [ADDR_WIDTH-1:0] rd_ptr = {ADDR_WIDTH{1'b0}};
    reg [ADDR_WIDTH-1:0] wr_ptr = {ADDR_WIDTH{1'b0}};
    integer count = 0;

    wire do_write = s_write && s_full_n;
    wire do_read = m_read && m_empty_n;

    assign s_full_n = (count < DEPTH);
    assign m_empty_n = (count > 0);
    assign m_dout = mem[rd_ptr];

    always @(posedge clk) begin
        if (!rst_n) begin
            rd_ptr <= {ADDR_WIDTH{1'b0}};
            wr_ptr <= {ADDR_WIDTH{1'b0}};
            count <= 0;
        end else begin
            if (do_write) begin
                mem[wr_ptr] <= s_din;
                wr_ptr <= wr_ptr + {{(ADDR_WIDTH-1){1'b0}}, 1'b1};
            end
            if (do_read) begin
                rd_ptr <= rd_ptr + {{(ADDR_WIDTH-1){1'b0}}, 1'b1};
            end
            count <= count + (do_write ? 1 : 0) - (do_read ? 1 : 0);
        end
    end
endmodule
