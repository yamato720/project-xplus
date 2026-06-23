`timescale 1ns/1ps

module tb_tapa_issue_scoreboard8;
    localparam integer ADDR_WIDTH = 6;
    localparam integer SCOREBOARD_DEPTH = 4;

    reg clk = 1'b0;
    reg rst = 1'b1;
    reg [7:0] head_valid = 8'd0;
    reg [(8 * ADDR_WIDTH)-1:0] head_addr = {(8 * ADDR_WIDTH){1'b0}};
    reg [7:0] head_is_pong = 8'd0;
    reg [7:0] head_done = 8'd0;
    reg issue_ready = 1'b1;
    reg pipe_advance = 1'b1;

    wire issue_valid;
    wire [2:0] issue_lane;
    wire issue_is_done;
    wire [7:0] pop_lane;
    wire [7:0] lane_hazard;
    wire [7:0] lane_eligible;
    wire scoreboard_empty;

    integer cycle = 0;

    CuperSpmvOnly_RtlIssueScoreboard8 #(
        .ADDR_WIDTH(ADDR_WIDTH),
        .SCOREBOARD_DEPTH(SCOREBOARD_DEPTH)
    ) dut (
        .clk(clk),
        .rst(rst),
        .head_valid(head_valid),
        .head_addr(head_addr),
        .head_is_pong(head_is_pong),
        .head_done(head_done),
        .issue_ready(issue_ready),
        .pipe_advance(pipe_advance),
        .issue_valid(issue_valid),
        .issue_lane(issue_lane),
        .issue_is_done(issue_is_done),
        .pop_lane(pop_lane),
        .lane_hazard(lane_hazard),
        .lane_eligible(lane_eligible),
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
        end
    endtask

    task automatic expect_issue;
        input integer lane;
        input bit done;
        reg [7:0] expected_pop;
        begin
            #1;
            expected_pop = issue_ready ? (8'b0000_0001 << lane[2:0]) : 8'd0;
            if (!issue_valid) begin
                $fatal(1, "FAIL cycle=%0d expected issue lane=%0d", cycle, lane);
            end
            if (issue_lane !== lane[2:0]) begin
                $fatal(1, "FAIL cycle=%0d issue_lane=%0d expected=%0d",
                       cycle, issue_lane, lane);
            end
            if (issue_is_done !== done) begin
                $fatal(1, "FAIL cycle=%0d issue_is_done=%0b expected=%0b",
                       cycle, issue_is_done, done);
            end
            if (pop_lane !== expected_pop) begin
                $fatal(1, "FAIL cycle=%0d pop_lane=%b expected=%b",
                       cycle, pop_lane, expected_pop);
            end
        end
    endtask

    task automatic expect_no_issue;
        begin
            #1;
            if (issue_valid) begin
                $fatal(1, "FAIL cycle=%0d unexpected issue lane=%0d pop=%b",
                       cycle, issue_lane, pop_lane);
            end
            if (pop_lane !== 8'd0) begin
                $fatal(1, "FAIL cycle=%0d unexpected pop=%b", cycle, pop_lane);
            end
        end
    endtask

    initial begin
        clear_heads();
        issue_ready = 1'b1;
        pipe_advance = 1'b1;
        repeat (2) tick();
        rst = 1'b0;
        #1;

        if (!scoreboard_empty) begin
            $fatal(1, "FAIL scoreboard should be empty after reset");
        end

        clear_heads();
        set_lane(0, 1'b1, 3, 1'b0, 1'b0);
        expect_issue(0, 1'b0);
        tick();

        clear_heads();
        set_lane(1, 1'b1, 3, 1'b0, 1'b0);
        set_lane(2, 1'b1, 4, 1'b0, 1'b0);
        #1;
        if (lane_hazard[1] || lane_hazard[2]) begin
            $fatal(1, "FAIL cross-lane same tag must not hazard, hazard=%b",
                   lane_hazard);
        end
        expect_issue(1, 1'b0);
        tick();

        clear_heads();
        set_lane(1, 1'b1, 3, 1'b0, 1'b0);
        set_lane(3, 1'b1, 3, 1'b0, 1'b0);
        #1;
        if (!lane_hazard[1] || lane_hazard[3]) begin
            $fatal(1, "FAIL expected same-lane hazard and cross-lane bypass, hazard=%b",
                   lane_hazard);
        end
        expect_issue(3, 1'b0);
        tick();

        clear_heads();
        set_lane(1, 1'b1, 3, 1'b1, 1'b0);
        #1;
        if (lane_hazard[1]) begin
            $fatal(1, "FAIL same lane different ping/pong should bypass, hazard=%b",
                   lane_hazard);
        end
        expect_issue(1, 1'b0);
        tick();

        clear_heads();
        set_lane(1, 1'b1, 3, 1'b0, 1'b1);
        #1;
        if (lane_hazard[1]) begin
            $fatal(1, "FAIL done lane should not be marked hazardous, hazard=%b",
                   lane_hazard);
        end
        expect_issue(1, 1'b1);
        tick();

        clear_heads();
        repeat (SCOREBOARD_DEPTH + 1) tick();
        if (!scoreboard_empty) begin
            $fatal(1, "FAIL scoreboard did not drain before same-lane recurrence test");
        end

        clear_heads();
        set_lane(6, 1'b1, 30, 1'b0, 1'b0);
        expect_issue(6, 1'b0);
        tick();

        clear_heads();
        set_lane(6, 1'b1, 30, 1'b0, 1'b0);
        expect_no_issue();
        tick();
        clear_heads();
        set_lane(6, 1'b1, 30, 1'b0, 1'b0);
        expect_no_issue();
        tick();
        clear_heads();
        set_lane(6, 1'b1, 30, 1'b0, 1'b0);
        expect_no_issue();
        tick();
        clear_heads();
        set_lane(6, 1'b1, 30, 1'b0, 1'b0);
        expect_no_issue();
        tick();
        expect_issue(6, 1'b0);
        tick();

        clear_heads();
        set_lane(0, 1'b1, 10, 1'b0, 1'b0);
        set_lane(2, 1'b1, 11, 1'b0, 1'b0);
        set_lane(7, 1'b1, 12, 1'b0, 1'b0);
        expect_issue(7, 1'b0);
        tick();

        clear_heads();
        set_lane(0, 1'b1, 10, 1'b0, 1'b0);
        set_lane(2, 1'b1, 11, 1'b0, 1'b0);
        expect_issue(0, 1'b0);
        tick();

        clear_heads();
        set_lane(2, 1'b1, 11, 1'b0, 1'b0);
        expect_issue(2, 1'b0);
        tick();

        clear_heads();
        set_lane(0, 1'b1, 20, 1'b0, 1'b0);
        issue_ready = 1'b0;
        expect_issue(0, 1'b0);
        tick();
        issue_ready = 1'b1;
        expect_issue(0, 1'b0);
        tick();

        clear_heads();
        pipe_advance = 1'b0;
        set_lane(1, 1'b1, 21, 1'b0, 1'b1);
        set_lane(2, 1'b1, 22, 1'b0, 1'b0);
        expect_issue(1, 1'b1);
        tick();

        clear_heads();
        set_lane(2, 1'b1, 22, 1'b0, 1'b0);
        expect_no_issue();
        pipe_advance = 1'b1;
        expect_issue(2, 1'b0);
        tick();

        clear_heads();
        repeat (SCOREBOARD_DEPTH + 1) tick();
        if (!scoreboard_empty) begin
            $fatal(1, "FAIL scoreboard did not drain");
        end

        $display("PASS: issue scoreboard8 cycles=%0d depth=%0d", cycle, SCOREBOARD_DEPTH);
        $finish;
    end
endmodule
