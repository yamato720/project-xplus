`timescale 1ns/1ps

module tb_tapa_owner_scoreboard_ooo;
    localparam integer HBM_CHANNEL_NUM = 8;
    localparam integer ADDR_WIDTH = 6;
    localparam integer SCOREBOARD_DEPTH = 4;
    localparam integer TAGGED_WIDTH = 130;
    localparam integer TAGGED_PAD_BIT = 129;

    reg ap_clk = 1'b0;
    reg ap_rst_n = 1'b0;
    reg ap_start = 1'b0;
    reg [31:0] Iteration_num = 32'd1;
    reg [31:0] Owner_id = 32'd0;

    reg [129:0] lane_dout [0:7];
    reg [7:0] lane_empty_n = 8'd0;
    wire [7:0] lane_read;

    wire [(8 * TAGGED_WIDTH):0] scheduled_din;
    reg scheduled_full_n = 1'b1;
    wire scheduled_write;
    wire ap_done;
    wire ap_idle;
    wire ap_ready;

    integer cycle = 0;
    integer lane;
    integer nonpad_count = 0;
    integer hazard_bubbles = 0;
    reg [7:0] done_mask = 8'd0;

    CuperSpmvOnly_RtlOwnerScoreboardOoo #(
        .HBM_CHANNEL_NUM(HBM_CHANNEL_NUM),
        .ADDR_WIDTH(ADDR_WIDTH),
        .SCOREBOARD_DEPTH(SCOREBOARD_DEPTH),
        .TAGGED_WIDTH(TAGGED_WIDTH),
        .TAGGED_PAD_BIT(TAGGED_PAD_BIT)
    ) dut (
        .ap_clk(ap_clk),
        .ap_rst_n(ap_rst_n),
        .ap_start(ap_start),
        .ap_done(ap_done),
        .ap_idle(ap_idle),
        .ap_ready(ap_ready),
        .Iteration_num(Iteration_num),
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
        .Scheduled_Owner_Stream_s_din(scheduled_din),
        .Scheduled_Owner_Stream_s_full_n(scheduled_full_n),
        .Scheduled_Owner_Stream_s_write(scheduled_write),
        .Scheduled_Owner_Stream_peek({(8 * TAGGED_WIDTH + 1){1'b0}}),
        .Owner_id(Owner_id)
    );

    always #5 ap_clk = ~ap_clk;

    task automatic tick;
        begin
            @(posedge ap_clk);
            #1;
            cycle = cycle + 1;
        end
    endtask

    function automatic [129:0] make_token;
        input [31:0] packet_idx;
        input [31:0] pair_lane;
        input [31:0] scalar_lane;
        input bit done;
        begin
            make_token = 130'd0;
            make_token[0] = done;
            make_token[32:1] = packet_idx;
            make_token[64:33] = pair_lane;
            make_token[96:65] = scalar_lane;
            make_token[128:97] = packet_idx + pair_lane + scalar_lane;
        end
    endfunction

    function automatic bit scheduled_lane_padding;
        input integer lane_id;
        begin
            scheduled_lane_padding =
                scheduled_din[lane_id * TAGGED_WIDTH + TAGGED_PAD_BIT];
        end
    endfunction

    function automatic bit scheduled_lane_done;
        input integer lane_id;
        begin
            scheduled_lane_done = scheduled_din[lane_id * TAGGED_WIDTH];
        end
    endfunction

    function automatic [31:0] scheduled_lane_packet;
        input integer lane_id;
        begin
            scheduled_lane_packet =
                scheduled_din[lane_id * TAGGED_WIDTH + 1 +: 32];
        end
    endfunction

    initial begin
        for (lane = 0; lane < 8; lane = lane + 1) begin
            lane_dout[lane] = 130'd0;
        end

        repeat (2) tick();
        ap_rst_n = 1'b1;
        ap_start = 1'b1;
        tick();
        ap_start = 1'b0;

        lane_dout[0] = make_token(32'd0, 32'd0, 32'd0, 1'b0);
        lane_dout[1] = make_token(32'd1, 32'd1, 32'd0, 1'b0);
        lane_dout[2] = make_token(32'd2, 32'd2, 32'd0, 1'b0);
        lane_dout[3] = make_token(32'd3, 32'd3, 32'd0, 1'b0);
        lane_dout[4] = make_token(32'd4, 32'd4, 32'd0, 1'b0);
        lane_dout[5] = make_token(32'd5, 32'd5, 32'd0, 1'b0);
        lane_dout[6] = make_token(32'd6, 32'd6, 32'd0, 1'b0);
        lane_dout[7] = make_token(32'd7, 32'd7, 32'd0, 1'b0);
        lane_empty_n = 8'hff;
        #1;
        if (lane_read !== 8'hff) begin
            $fatal(1, "FAIL cycle=%0d expected first fill all lanes read=%b",
                   cycle, lane_read);
        end
        tick();
        lane_empty_n = 8'd0;
        scheduled_full_n = 1'b0;
        #1;

        repeat (3) begin
            if (scheduled_write) begin
                $fatal(1, "FAIL cycle=%0d wrote while downstream full", cycle);
            end
            if (lane_read !== 8'd0) begin
                $fatal(1, "FAIL cycle=%0d reread while heads cached read=%b",
                       cycle, lane_read);
            end
            tick();
        end

        scheduled_full_n = 1'b1;
        #1;
        if (!scheduled_write) begin
            $fatal(1, "FAIL cycle=%0d cached heads did not issue", cycle);
        end
        for (lane = 0; lane < 8; lane = lane + 1) begin
            if (scheduled_lane_padding(lane)) begin
                $fatal(1, "FAIL lane %0d unexpectedly padded on cached issue", lane);
            end
            if (scheduled_lane_packet(lane) !== lane[31:0]) begin
                $fatal(1, "FAIL lane %0d packet=%0d", lane,
                       scheduled_lane_packet(lane));
            end
            nonpad_count = nonpad_count + 1;
        end
        tick();

        lane_dout[0] = make_token(32'd0, 32'd0, 32'd0, 1'b0);
        lane_empty_n = 8'b0000_0001;
        #1;
        if (lane_read !== 8'b0000_0001) begin
            $fatal(1, "FAIL cycle=%0d expected hazard-head fill read=%b",
                   cycle, lane_read);
        end
        tick();
        lane_empty_n = 8'd0;
        #1;

        hazard_bubbles = 0;
        while (scheduled_write && scheduled_lane_padding(0)) begin
            if (lane_read[0]) begin
                $fatal(1, "FAIL cycle=%0d reread while hazard head cached",
                       cycle);
            end
            hazard_bubbles = hazard_bubbles + 1;
            tick();
            #1;
            if (hazard_bubbles > SCOREBOARD_DEPTH + 2) begin
                $fatal(1, "FAIL hazard head did not age out");
            end
        end
        if (!scheduled_write || scheduled_lane_padding(0)) begin
            $fatal(1, "FAIL cached hazard head did not issue");
        end
        if (scheduled_lane_packet(0) !== 32'd0) begin
            $fatal(1, "FAIL cached hazard packet=%0d",
                   scheduled_lane_packet(0));
        end
        nonpad_count = nonpad_count + 1;
        tick();

        for (lane = 0; lane < 8; lane = lane + 1) begin
            lane_dout[lane] = make_token(32'd100 + lane[31:0],
                                         lane[31:0], 32'd0, 1'b1);
        end
        lane_empty_n = 8'hff;
        #1;
        if (lane_read !== 8'hff) begin
            $fatal(1, "FAIL cycle=%0d expected done fill all lanes read=%b",
                   cycle, lane_read);
        end
        tick();
        lane_empty_n = 8'd0;
        #1;

        #1;
        if (!scheduled_write) begin
            $fatal(1, "FAIL cycle=%0d done tokens did not issue", cycle);
        end
        for (lane = 0; lane < 8; lane = lane + 1) begin
            if (scheduled_lane_padding(lane)) begin
                $fatal(1, "FAIL done lane %0d padded", lane);
            end
            if (!scheduled_lane_done(lane)) begin
                $fatal(1, "FAIL lane %0d missing done", lane);
            end
            done_mask[lane] = 1'b1;
        end
        tick();

        if (done_mask !== 8'hff) begin
            $fatal(1, "FAIL done mask=%b", done_mask);
        end
        if (!ap_done || !ap_ready) begin
            $fatal(1, "FAIL expected ap_done/ap_ready after all lane done");
        end

        $display("PASS: owner scoreboard wrapper cycles=%0d nonpad=%0d",
                 cycle, nonpad_count);
        $finish;
    end
endmodule
