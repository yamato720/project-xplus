`timescale 1ns/1ps

module tb_tapa_owner_lane_accumulator;
    localparam integer OWNER_ID = 3;
    localparam integer PAIR_LANE = 5;
    localparam integer ROW_NUM = 512;
    localparam integer PACKETS = (ROW_NUM + 15) >> 4;
    localparam integer INPUT_COUNT = 7;
    localparam integer OUTPUT_COUNT = (PACKETS + 15) >> 4;

    reg clk = 1'b0;
    reg rst_n = 1'b0;
    reg ap_start = 1'b0;

    wire ap_done;
    wire ap_idle;
    wire ap_ready;

    reg [31:0] iteration_num = 32'd1;
    reg [31:0] row_num = ROW_NUM;

    reg [129:0] input_words [0:INPUT_COUNT-1];
    integer input_idx = 0;
    integer output_idx = 0;
    integer cycle = 0;
    integer errors = 0;

    wire [129:0] in_dout =
        (input_idx < INPUT_COUNT) ? input_words[input_idx] : 130'd0;
    wire in_empty_n = (input_idx < INPUT_COUNT);
    wire in_read;

    wire [128:0] out_din;
    reg out_full_n;
    wire out_write;

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

    function automatic [31:0] output_packet;
        input [128:0] word;
        begin
            output_packet = word[31:0];
        end
    endfunction

    function automatic [31:0] output_pair;
        input [128:0] word;
        begin
            output_pair = word[63:32];
        end
    endfunction

    function automatic [31:0] output_ping;
        input [128:0] word;
        begin
            output_ping = word[95:64];
        end
    endfunction

    function automatic [31:0] output_pong;
        input [128:0] word;
        begin
            output_pong = word[127:96];
        end
    endfunction

    function automatic [31:0] fbits;
        input shortreal value;
        begin
            fbits = $shortrealtobits(value);
        end
    endfunction

    task automatic check_output;
        input integer idx;
        input [128:0] got;
        reg [31:0] expect_packet;
        reg [31:0] expect_ping;
        reg [31:0] expect_pong;
        begin
            expect_packet = OWNER_ID + idx * 16;
            expect_ping = (idx == 0) ? fbits(17.0) : fbits(11.0);
            expect_pong = (idx == 0) ? fbits(100.0) : fbits(7.0);

            if (output_packet(got) !== expect_packet ||
                output_pair(got) !== PAIR_LANE ||
                output_ping(got) !== expect_ping ||
                output_pong(got) !== expect_pong) begin
                $display("FAIL output[%0d]: packet=%0d pair=%0d ping=%0d pong=%0d, expect packet=%0d pair=%0d ping=%0d pong=%0d",
                         idx,
                         output_packet(got),
                         output_pair(got),
                         output_ping(got),
                         output_pong(got),
                         expect_packet,
                         PAIR_LANE,
                         expect_ping,
                         expect_pong);
                errors = errors + 1;
            end
        end
    endtask

    CuperSpmvOnly_RtlOwnerLaneAccumulatorOoo dut (
        .ap_clk(clk),
        .ap_rst_n(rst_n),
        .ap_start(ap_start),
        .ap_done(ap_done),
        .ap_idle(ap_idle),
        .ap_ready(ap_ready),
        .Iteration_num(iteration_num),
        .Row_num(row_num),
        .Owner_Lane_Stream_s_dout(in_dout),
        .Owner_Lane_Stream_s_empty_n(in_empty_n),
        .Owner_Lane_Stream_s_read(in_read),
        .Owner_Lane_Stream_peek_dout(130'd0),
        .Owner_Lane_Stream_peek_empty_n(1'b0),
        .Owner_Lane_Stream_peek_read(),
        .Vector_Y_Tagged_Stream_s_din(out_din),
        .Vector_Y_Tagged_Stream_s_full_n(out_full_n),
        .Vector_Y_Tagged_Stream_s_write(out_write),
        .Vector_Y_Tagged_Stream_peek(129'd0),
        .Owner_id(OWNER_ID),
        .Pair_lane(PAIR_LANE)
    );

    always #5 clk = ~clk;

    initial begin
        input_words[0] = pack_scalar(1'b0, OWNER_ID, PAIR_LANE, 32'd0, fbits(10.0));
        input_words[1] = pack_scalar(1'b0, OWNER_ID, PAIR_LANE, 32'd0, fbits(7.0));
        input_words[2] = pack_scalar(1'b0, OWNER_ID, PAIR_LANE, 32'd1, fbits(100.0));
        input_words[3] = pack_scalar(1'b0, OWNER_ID + 16, PAIR_LANE, 32'd0, fbits(11.0));
        input_words[4] = pack_scalar(1'b0, OWNER_ID + 16, PAIR_LANE, 32'd1, fbits(3.0));
        input_words[5] = pack_scalar(1'b0, OWNER_ID + 16, PAIR_LANE, 32'd1, fbits(4.0));
        input_words[6] = pack_scalar(1'b1, OWNER_ID, PAIR_LANE, 32'd0, 32'd0);
    end

    always @(posedge clk) begin
        cycle <= cycle + 1;
        out_full_n <= (cycle[2:0] != 3'd5);

        if (cycle < 5) begin
            rst_n <= 1'b0;
            ap_start <= 1'b0;
            out_full_n <= 1'b1;
        end else if (cycle == 5) begin
            rst_n <= 1'b1;
            ap_start <= 1'b1;
        end else begin
            ap_start <= 1'b0;
        end

        if (rst_n && in_read) begin
            input_idx <= input_idx + 1;
        end

        if (rst_n && out_write) begin
            check_output(output_idx, out_din);
            output_idx <= output_idx + 1;
        end

        if (rst_n && ap_done) begin
            if (input_idx != INPUT_COUNT) begin
                $fatal(1, "FAIL: ap_done before all input was read");
            end
            if (output_idx != OUTPUT_COUNT) begin
                $fatal(1, "FAIL: output_count=%0d expect=%0d", output_idx, OUTPUT_COUNT);
            end
            if (errors != 0) begin
                $fatal(1, "FAIL: errors=%0d", errors);
            end
            $display("PASS: TAPA owner-lane RTL accumulator cycles=%0d", cycle);
            $finish;
        end

        if (cycle > 1000) begin
            $fatal(1, "FAIL: timeout input_idx=%0d output_idx=%0d", input_idx, output_idx);
        end
    end
endmodule
