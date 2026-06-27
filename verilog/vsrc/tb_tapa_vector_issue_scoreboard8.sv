`timescale 1ns/1ps

module tb_tapa_vector_issue_scoreboard8;
    localparam integer ADDR_WIDTH = 6;
    localparam integer SCOREBOARD_DEPTH = 4;
    localparam integer TAGGED_WIDTH = 130;
    localparam integer TAGGED_PAD_BIT = 129;

    reg clk = 1'b0;
    reg rst = 1'b1;
    reg [7:0] head_valid = 8'd0;
    reg [(8 * ADDR_WIDTH)-1:0] head_addr = {(8 * ADDR_WIDTH){1'b0}};
    reg [7:0] head_is_pong = 8'd0;
    reg [7:0] head_done = 8'd0;
    reg [(8 * TAGGED_WIDTH)-1:0] head_payload = {(8 * TAGGED_WIDTH){1'b0}};
    reg issue_ready = 1'b1;

    wire issue_valid;
    wire [(8 * TAGGED_WIDTH)-1:0] issue_payload;
    wire [7:0] lane_hazard;
    wire scoreboard_empty;

    integer cycle = 0;

    CuperSpmvOnly_RtlIssueScoreboard8 #(
        .ADDR_WIDTH(ADDR_WIDTH),
        .SCOREBOARD_DEPTH(SCOREBOARD_DEPTH),
        .TAGGED_WIDTH(TAGGED_WIDTH),
        .TAGGED_PAD_BIT(TAGGED_PAD_BIT)
    ) dut (
        .clk(clk),
        .rst(rst),
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

    always #5 clk = ~clk;

    task automatic tick;
        begin
            @(posedge clk);
            #1;
            cycle = cycle + 1;
        end
    endtask

    task automatic clear_heads;
        begin
            head_valid = 8'd0;
            head_addr = {(8 * ADDR_WIDTH){1'b0}};
            head_is_pong = 8'd0;
            head_done = 8'd0;
            head_payload = {(8 * TAGGED_WIDTH){1'b0}};
        end
    endtask

    task automatic set_lane;
        input integer lane;
        input bit valid;
        input integer addr;
        input bit is_pong;
        input bit done;
        begin
            head_valid[lane] = valid;
            head_addr[lane * ADDR_WIDTH +: ADDR_WIDTH] = addr[ADDR_WIDTH-1:0];
            head_is_pong[lane] = is_pong;
            head_done[lane] = done;
            head_payload[lane * TAGGED_WIDTH +: TAGGED_WIDTH] =
                {TAGGED_WIDTH{1'b0}};
            head_payload[lane * TAGGED_WIDTH + TAGGED_PAD_BIT] = !valid;
            head_payload[lane * TAGGED_WIDTH + 0] = done;
            head_payload[lane * TAGGED_WIDTH + 32 -: 32] = addr[31:0];
        end
    endtask

    function automatic [7:0] issue_real_mask;
        integer lane;
        begin
            issue_real_mask = 8'd0;
            for (lane = 0; lane < 8; lane = lane + 1) begin
                issue_real_mask[lane] =
                    !issue_payload[lane * TAGGED_WIDTH + TAGGED_PAD_BIT];
            end
        end
    endfunction

    task automatic set_all_lanes_unique;
        integer lane;
        begin
            clear_heads();
            for (lane = 0; lane < 8; lane = lane + 1) begin
                set_lane(lane, 1'b1, 10 + lane, 1'b0, 1'b0);
            end
        end
    endtask

    task automatic expect_vector;
        input [7:0] expected_mask;
        input bit expected_bubble;
        reg [7:0] actual_mask;
        begin
            #1;
            actual_mask = issue_real_mask();
            if (!issue_valid) begin
                $fatal(1, "FAIL cycle=%0d expected vector mask=%b",
                       cycle, expected_mask);
            end
            if (actual_mask !== expected_mask) begin
                $fatal(1, "FAIL cycle=%0d real_mask=%b expected=%b",
                       cycle, actual_mask, expected_mask);
            end
            if (((actual_mask == 8'd0) && issue_valid) !== expected_bubble) begin
                $fatal(1, "FAIL cycle=%0d bubble=%0b expected=%0b mask=%b",
                       cycle, (actual_mask == 8'd0) && issue_valid,
                       expected_bubble, actual_mask);
            end
        end
    endtask

    task automatic expect_no_vector;
        begin
            #1;
            if (issue_valid) begin
                $fatal(1, "FAIL cycle=%0d unexpected vector mask=%b",
                       cycle, issue_real_mask());
            end
        end
    endtask

    task automatic drain_single_lane_by_bubbles;
        input integer lane;
        input integer addr;
        input bit is_pong;
        integer guard;
        begin
            guard = 0;
            while (!scoreboard_empty) begin
                clear_heads();
                set_lane(lane, 1'b1, addr, is_pong, 1'b0);
                expect_vector(8'h00, 1'b1);
                tick();
                guard = guard + 1;
                if (guard > SCOREBOARD_DEPTH + 1) begin
                    $fatal(1, "FAIL cycle=%0d drain timed out lane=%0d addr=%0d",
                           cycle, lane, addr);
                end
            end
            clear_heads();
        end
    endtask

    initial begin
        clear_heads();
        issue_ready = 1'b1;
        repeat (2) tick();
        rst = 1'b0;
        #1;

        if (!scoreboard_empty) begin
            $fatal(1, "FAIL scoreboard should be empty after reset");
        end

        set_all_lanes_unique();
        expect_vector(8'hff, 1'b0);
        tick();

        clear_heads();
        set_lane(0, 1'b1, 10, 1'b0, 1'b0);
        set_lane(1, 1'b1, 31, 1'b0, 1'b0);
        set_lane(2, 1'b1, 12, 1'b0, 1'b1);
        set_lane(3, 1'b1, 13, 1'b1, 1'b0);
        set_lane(4, 1'b1, 14, 1'b0, 1'b0);
        set_lane(5, 1'b1, 45, 1'b0, 1'b0);
        #1;
        if (!lane_hazard[0] || lane_hazard[1] ||
            lane_hazard[2] || lane_hazard[3] ||
            !lane_hazard[4] || lane_hazard[5]) begin
            $fatal(1, "FAIL unexpected mixed hazard vector=%b", lane_hazard);
        end
        expect_vector(8'b0010_1110, 1'b0);
        tick();

        clear_heads();
        set_lane(6, 1'b1, 16, 1'b0, 1'b0);
        #1;
        if (!lane_hazard[6]) begin
            $fatal(1, "FAIL expected lane 6 hazard after first full vector");
        end
        expect_vector(8'h00, 1'b1);
        tick();

        clear_heads();
        set_lane(6, 1'b1, 16, 1'b0, 1'b0);
        expect_vector(8'h00, 1'b1);
        tick();

        clear_heads();
        set_lane(6, 1'b1, 16, 1'b0, 1'b0);
        expect_vector(8'h00, 1'b1);
        tick();

        clear_heads();
        set_lane(6, 1'b1, 16, 1'b0, 1'b0);
        expect_vector(8'b0100_0000, 1'b0);
        tick();

        clear_heads();
        drain_single_lane_by_bubbles(6, 16, 1'b0);
        if (!scoreboard_empty) begin
            $fatal(1, "FAIL scoreboard did not drain before no-head test");
        end

        clear_heads();
        set_lane(2, 1'b1, 22, 1'b0, 1'b0);
        expect_vector(8'b0000_0100, 1'b0);
        tick();

        clear_heads();
        repeat (3) begin
            expect_no_vector();
            tick();
        end

        set_lane(2, 1'b1, 22, 1'b0, 1'b0);
        #1;
        if (!lane_hazard[2]) begin
            $fatal(1, "FAIL no-head cycles must not age scoreboard");
        end
        expect_vector(8'h00, 1'b1);
        tick();

        drain_single_lane_by_bubbles(2, 22, 1'b0);
        if (!scoreboard_empty) begin
            $fatal(1, "FAIL scoreboard did not drain before ready test");
        end

        clear_heads();
        set_lane(0, 1'b1, 20, 1'b0, 1'b0);
        issue_ready = 1'b0;
        expect_vector(8'b0000_0001, 1'b0);
        tick();

        issue_ready = 1'b1;
        expect_vector(8'b0000_0001, 1'b0);
        tick();

        clear_heads();
        set_lane(0, 1'b1, 20, 1'b0, 1'b0);
        expect_vector(8'h00, 1'b1);
        tick();

        drain_single_lane_by_bubbles(0, 20, 1'b0);
        if (!scoreboard_empty) begin
            $fatal(1, "FAIL scoreboard did not drain before done test");
        end

        clear_heads();
        set_lane(3, 1'b1, 33, 1'b0, 1'b1);
        expect_vector(8'b0000_1000, 1'b0);
        tick();

        clear_heads();
        set_lane(3, 1'b1, 33, 1'b0, 1'b0);
        expect_vector(8'b0000_1000, 1'b0);
        tick();

        drain_single_lane_by_bubbles(3, 33, 1'b0);
        if (!scoreboard_empty) begin
            $fatal(1, "FAIL scoreboard did not drain");
        end

        $display("PASS: vector issue scoreboard8 cycles=%0d depth=%0d",
                 cycle, SCOREBOARD_DEPTH);
        $finish;
    end
endmodule
