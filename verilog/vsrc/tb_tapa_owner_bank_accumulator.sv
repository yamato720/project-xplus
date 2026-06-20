`timescale 1ns/1ps

module tb_tapa_owner_bank_accumulator;
    localparam integer OWNER_ID = 3;
    localparam integer ROW_NUM = 512;
    localparam integer PACKETS = (ROW_NUM + 15) >> 4;
    localparam integer OWNER_GROUPS = (PACKETS + 15) >> 4;
    localparam integer LANES = 8;
    localparam integer INPUT_PER_LANE = 6;
    localparam integer REAL_EVENTS_PER_LANE = INPUT_PER_LANE - 1;
    localparam integer REAL_EVENTS = LANES * REAL_EVENTS_PER_LANE;
    localparam integer OUTPUT_COUNT = LANES * OWNER_GROUPS;

    reg clk = 1'b0;
    reg rst_n = 1'b0;
    reg ap_start = 1'b0;

    wire ap_done;
    wire ap_idle;
    wire ap_ready;

    reg [31:0] iteration_num = 32'd1;
    reg [31:0] row_num = ROW_NUM;
    reg [31:0] owner_id = OWNER_ID;

    reg [129:0] input_words [0:LANES-1][0:INPUT_PER_LANE-1];
    integer input_idx [0:LANES-1];
    integer output_idx = 0;
    integer cycle = 0;
    integer errors = 0;
    integer lane_init;
    integer seen [0:LANES-1][0:OWNER_GROUPS-1];

    wire [129:0] lane_dout [0:LANES-1];
    wire lane_empty_n [0:LANES-1];
    wire lane_read [0:LANES-1];

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

    function automatic [31:0] expect_ping;
        input integer pair_lane;
        input integer group;
        begin
            expect_ping = (group == 0) ? fbits(11.0 + pair_lane) :
                                         fbits(30.0 + pair_lane);
        end
    endfunction

    function automatic [31:0] expect_pong;
        input integer pair_lane;
        input integer group;
        begin
            expect_pong = (group == 0) ? fbits(20.0 + pair_lane) :
                                         fbits(42.0 + pair_lane);
        end
    endfunction

    task automatic check_output;
        input [128:0] got;
        integer pair_lane;
        integer group;
        reg [31:0] packet;
        begin
            packet = output_packet(got);
            pair_lane = output_pair(got);
            if (pair_lane < 0 || pair_lane >= LANES) begin
                $display("FAIL bad pair_lane=%0d packet=%0d", pair_lane, packet);
                errors = errors + 1;
            end else if (packet < OWNER_ID ||
                         ((packet - OWNER_ID) % 16) != 0) begin
                $display("FAIL bad packet=%0d pair=%0d", packet, pair_lane);
                errors = errors + 1;
            end else begin
                group = (packet - OWNER_ID) / 16;
                if (group < 0 || group >= OWNER_GROUPS) begin
                    $display("FAIL bad group=%0d packet=%0d pair=%0d",
                             group, packet, pair_lane);
                    errors = errors + 1;
                end else begin
                    if (seen[pair_lane][group] != 0) begin
                        $display("FAIL duplicate pair=%0d group=%0d",
                                 pair_lane, group);
                        errors = errors + 1;
                    end
                    seen[pair_lane][group] = 1;

                    if (output_ping(got) !== expect_ping(pair_lane, group) ||
                        output_pong(got) !== expect_pong(pair_lane, group)) begin
                        $display("FAIL value pair=%0d group=%0d ping=%0d pong=%0d expect ping=%0d pong=%0d",
                                 pair_lane,
                                 group,
                                 output_ping(got),
                                 output_pong(got),
                                 expect_ping(pair_lane, group),
                                 expect_pong(pair_lane, group));
                        errors = errors + 1;
                    end
                end
            end
        end
    endtask

    genvar lane;
    generate
        for (lane = 0; lane < LANES; lane = lane + 1) begin : lane_inputs
            assign lane_dout[lane] =
                (input_idx[lane] < INPUT_PER_LANE) ?
                input_words[lane][input_idx[lane]] : 130'd0;
            assign lane_empty_n[lane] = (input_idx[lane] < INPUT_PER_LANE);
        end
    endgenerate

    CuperSpmvOnly_RtlOwnerBankAccumulatorOoo dut (
        .ap_clk(clk),
        .ap_rst_n(rst_n),
        .ap_start(ap_start),
        .ap_done(ap_done),
        .ap_idle(ap_idle),
        .ap_ready(ap_ready),
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
        .Vector_Y_Tagged_Stream_s_din(out_din),
        .Vector_Y_Tagged_Stream_s_full_n(out_full_n),
        .Vector_Y_Tagged_Stream_s_write(out_write),
        .Vector_Y_Tagged_Stream_peek(129'd0),
        .Owner_id(owner_id)
    );

    always #5 clk = ~clk;

    initial begin
        for (lane_init = 0; lane_init < LANES; lane_init = lane_init + 1) begin
            input_idx[lane_init] = 0;
            seen[lane_init][0] = 0;
            seen[lane_init][1] = 0;

            input_words[lane_init][0] =
                pack_scalar(1'b0, OWNER_ID, lane_init, 32'd0, fbits(10.0 + lane_init));
            input_words[lane_init][1] =
                pack_scalar(1'b0, OWNER_ID, lane_init, 32'd0, fbits(1.0));
            input_words[lane_init][2] =
                pack_scalar(1'b0, OWNER_ID, lane_init, 32'd1, fbits(20.0 + lane_init));
            input_words[lane_init][3] =
                pack_scalar(1'b0, OWNER_ID + 16, lane_init, 32'd0, fbits(30.0 + lane_init));
            input_words[lane_init][4] =
                pack_scalar(1'b0, OWNER_ID + 16, lane_init, 32'd1, fbits(42.0 + lane_init));
            input_words[lane_init][5] =
                pack_scalar(1'b1, OWNER_ID, lane_init, 32'd0, 32'd0);
        end
    end

    always @(posedge clk) begin
        integer i;
        integer next_output_idx;
        cycle <= cycle + 1;
        out_full_n <= (cycle[3:0] != 4'd7);

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

        if (rst_n) begin
            for (i = 0; i < LANES; i = i + 1) begin
                if (lane_read[i]) begin
                    input_idx[i] <= input_idx[i] + 1;
                end
            end
        end

        next_output_idx = output_idx;
        if (rst_n && out_write) begin
            check_output(out_din);
            next_output_idx = output_idx + 1;
            output_idx <= output_idx + 1;
        end

        if (rst_n && ap_done) begin
            for (i = 0; i < LANES; i = i + 1) begin
                if (input_idx[i] != INPUT_PER_LANE) begin
                    $fatal(1, "FAIL lane %0d input_idx=%0d expect=%0d",
                           i, input_idx[i], INPUT_PER_LANE);
                end
                if (seen[i][0] == 0 || seen[i][1] == 0) begin
                    $fatal(1, "FAIL missing output for lane %0d seen0=%0d seen1=%0d",
                           i, seen[i][0], seen[i][1]);
                end
            end
            if (next_output_idx != OUTPUT_COUNT) begin
                $fatal(1, "FAIL output_count=%0d expect=%0d",
                       next_output_idx, OUTPUT_COUNT);
            end
            if (errors != 0) begin
                $fatal(1, "FAIL errors=%0d", errors);
            end
            $display("PASS: TAPA owner-bank RTL accumulator cycles=%0d real_events=%0d outputs=%0d cycles_per_event=%0.3f cycles_per_output=%0.3f",
                     cycle,
                     REAL_EVENTS,
                     OUTPUT_COUNT,
                     $itor(cycle) / $itor(REAL_EVENTS),
                     $itor(cycle) / $itor(OUTPUT_COUNT));
            $finish;
        end

        if (cycle > 2000) begin
            $fatal(1, "FAIL: timeout output_idx=%0d", output_idx);
        end
    end
endmodule
