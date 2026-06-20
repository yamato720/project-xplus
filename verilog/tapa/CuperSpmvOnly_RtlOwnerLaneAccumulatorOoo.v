`timescale 1 ns / 1 ps

// TAPA custom RTL owner-lane accumulator for CuperSpmvServiceOnly.
//
// Protocol:
//   input  CuperSpmvOnly_TaggedScalar, 130 bits:
//          {pad[129], value[128:97], scalar_lane[96:65],
//           pair_lane[64:33], packet_idx[32:1], done[0]}
//   output CuperSpmvOnly_TaggedFloatV2, 129 bits:
//          {pad[128], value1/pong[127:96], value0/ping[95:64],
//           pair_lane[63:32], packet_idx[31:0]}
//
// Performance version:
//   * one FP32 adder pipeline is kept moving with valid-tag sideband state;
//   * a small scoreboard blocks only true RAW hazards on the same
//     owner_group and ping/pong lane;
//   * independent rows can enter every cycle, and bubbles are injected only
//     when the input stream is empty or a true hazard is pending.
module CuperSpmvOnly_RtlOwnerLaneAccumulatorOoo (
    ap_clk,
    ap_rst_n,
    ap_start,
    ap_done,
    ap_idle,
    ap_ready,
    Iteration_num,
    Row_num,
    Owner_Lane_Stream_s_dout,
    Owner_Lane_Stream_s_empty_n,
    Owner_Lane_Stream_s_read,
    Owner_Lane_Stream_peek_dout,
    Owner_Lane_Stream_peek_empty_n,
    Owner_Lane_Stream_peek_read,
    Vector_Y_Tagged_Stream_s_din,
    Vector_Y_Tagged_Stream_s_full_n,
    Vector_Y_Tagged_Stream_s_write,
    Vector_Y_Tagged_Stream_peek,
    Owner_id,
    Pair_lane
);
    parameter integer HBM_CHANNEL_NUM = 16;
    parameter integer MEM_DEPTH = 8192;
    parameter integer ADDR_WIDTH = 13;
    parameter integer FADD_PIPE_LATENCY = 14;

    input ap_clk;
    input ap_rst_n;
    input ap_start;
    output ap_done;
    output ap_idle;
    output ap_ready;
    input [31:0] Iteration_num;
    input [31:0] Row_num;

    input [129:0] Owner_Lane_Stream_s_dout;
    input Owner_Lane_Stream_s_empty_n;
    output Owner_Lane_Stream_s_read;
    input [129:0] Owner_Lane_Stream_peek_dout;
    input Owner_Lane_Stream_peek_empty_n;
    output Owner_Lane_Stream_peek_read;

    output [128:0] Vector_Y_Tagged_Stream_s_din;
    input Vector_Y_Tagged_Stream_s_full_n;
    output Vector_Y_Tagged_Stream_s_write;
    input [128:0] Vector_Y_Tagged_Stream_peek;
    input [31:0] Owner_id;
    input [31:0] Pair_lane;

    localparam [2:0] ST_IDLE      = 3'd0;
    localparam [2:0] ST_INIT      = 3'd1;
    localparam [2:0] ST_CONSUME   = 3'd2;
    localparam [2:0] ST_DRAIN_ADD = 3'd3;
    localparam [2:0] ST_WRITE     = 3'd4;
    localparam [2:0] ST_NEXT_ITER = 3'd5;
    localparam [2:0] ST_DONE      = 3'd6;

    wire ping_rd_en;
    wire pong_rd_en;
    wire [ADDR_WIDTH-1:0] ping_rd_addr;
    wire [ADDR_WIDTH-1:0] pong_rd_addr;
    wire [31:0] ping_rd_data;
    wire [31:0] pong_rd_data;
    wire ping_wr_en;
    wire pong_wr_en;
    wire [ADDR_WIDTH-1:0] ping_wr_addr;
    wire [ADDR_WIDTH-1:0] pong_wr_addr;
    wire [31:0] ping_wr_data;
    wire [31:0] pong_wr_data;

    reg [2:0] state;
    reg done_pulse;
    reg [31:0] iter_idx;
    reg [31:0] iteration_time;
    reg [31:0] num_out_packets;
    reg [31:0] num_owner_groups;

    reg [ADDR_WIDTH-1:0] init_addr;
    reg [ADDR_WIDTH-1:0] owner_group;
    reg [31:0] owner_id;
    reg [31:0] pair_lane;
    reg meta_valid;

    reg rd_valid;
    reg [ADDR_WIDTH-1:0] rd_addr;
    reg rd_is_pong;
    reg [31:0] rd_lhs;
    reg [31:0] rd_rhs;

    reg rd_pending_valid;
    reg [ADDR_WIDTH-1:0] rd_pending_addr;
    reg rd_pending_is_pong;
    reg [31:0] rd_pending_rhs;

    reg [FADD_PIPE_LATENCY-1:0] pipe_valid;
    reg [ADDR_WIDTH-1:0] pipe_addr [0:FADD_PIPE_LATENCY-1];
    reg pipe_is_pong [0:FADD_PIPE_LATENCY-1];

    wire rst = ~ap_rst_n;
    wire [31:0] input_packet_idx = Owner_Lane_Stream_s_dout[32:1];
    wire [31:0] input_pair_lane = Owner_Lane_Stream_s_dout[64:33];
    wire [31:0] input_scalar_lane = Owner_Lane_Stream_s_dout[96:65];
    wire [31:0] input_value = Owner_Lane_Stream_s_dout[128:97];
    wire input_done = Owner_Lane_Stream_s_dout[0];
    wire input_is_pong = (input_scalar_lane != 32'd0);
    wire [31:0] input_owner_group_full = input_packet_idx / HBM_CHANNEL_NUM;
    wire [ADDR_WIDTH-1:0] input_owner_group =
        input_owner_group_full[ADDR_WIDTH-1:0];

    reg input_hazard;
    integer hazard_i;
    always @(*) begin
        input_hazard = 1'b0;
        for (hazard_i = 0; hazard_i < FADD_PIPE_LATENCY; hazard_i = hazard_i + 1) begin
            if (pipe_valid[hazard_i] &&
                pipe_addr[hazard_i] == input_owner_group &&
                pipe_is_pong[hazard_i] == input_is_pong) begin
                input_hazard = 1'b1;
            end
        end
        if (rd_valid &&
            rd_addr == input_owner_group &&
            rd_is_pong == input_is_pong) begin
            input_hazard = 1'b1;
        end
        if (rd_pending_valid &&
            rd_pending_addr == input_owner_group &&
            rd_pending_is_pong == input_is_pong) begin
            input_hazard = 1'b1;
        end
    end

    wire issue_fire = rd_valid;
    wire pipe_active = |pipe_valid;
    wire fadd_active_state = (state == ST_CONSUME) || (state == ST_DRAIN_ADD);
    wire pending_to_rd = rd_pending_valid &&
                         (!rd_valid || (issue_fire && fadd_active_state));
    wire read_slot_available = !rd_pending_valid || pending_to_rd;
    wire can_accept_input = (state == ST_CONSUME) &&
                            Owner_Lane_Stream_s_empty_n &&
                            (input_done ||
                             (read_slot_available && !input_hazard));
    wire read_fire = can_accept_input && !input_done;
    wire fadd_advance = fadd_active_state && (pipe_active || issue_fire);
    wire [31:0] fadd_lhs = issue_fire ? rd_lhs : 32'd0;
    wire [31:0] fadd_rhs = issue_fire ? rd_rhs : 32'd0;
    wire [31:0] fadd_sum;

    reg [31:0] out_packet_idx;
    reg [31:0] out_ping;
    reg [31:0] out_pong;
    reg out_valid;
    reg write_read_pending;
    reg [31:0] write_pending_packet_idx;
    reg write_pending_last;

    wire output_fire = out_valid && Vector_Y_Tagged_Stream_s_full_n;
    wire output_can_load = !out_valid || Vector_Y_Tagged_Stream_s_full_n;
    wire [31:0] next_output_packet_idx = owner_group * HBM_CHANNEL_NUM + owner_id;
    wire owner_group_last =
        (owner_group + {{(ADDR_WIDTH-1){1'b0}}, 1'b1} >=
         num_owner_groups[ADDR_WIDTH-1:0]);
    wire write_read_issue = (state == ST_WRITE) &&
                            !write_read_pending &&
                            output_can_load;
    wire fadd_write = fadd_advance && pipe_valid[FADD_PIPE_LATENCY-1];

    assign ping_rd_en = (read_fire && !input_is_pong) || write_read_issue;
    assign pong_rd_en = (read_fire && input_is_pong) || write_read_issue;
    assign ping_rd_addr = write_read_issue ? owner_group : input_owner_group;
    assign pong_rd_addr = write_read_issue ? owner_group : input_owner_group;
    assign ping_wr_en = (state == ST_INIT) ||
                        (fadd_write && !pipe_is_pong[FADD_PIPE_LATENCY-1]);
    assign pong_wr_en = (state == ST_INIT) ||
                        (fadd_write && pipe_is_pong[FADD_PIPE_LATENCY-1]);
    assign ping_wr_addr = (state == ST_INIT) ? init_addr :
                          pipe_addr[FADD_PIPE_LATENCY-1];
    assign pong_wr_addr = (state == ST_INIT) ? init_addr :
                          pipe_addr[FADD_PIPE_LATENCY-1];
    assign ping_wr_data = (state == ST_INIT) ? 32'd0 : fadd_sum;
    assign pong_wr_data = (state == ST_INIT) ? 32'd0 : fadd_sum;

    assign Owner_Lane_Stream_s_read = can_accept_input;
    assign Owner_Lane_Stream_peek_read = 1'b0;
    assign Vector_Y_Tagged_Stream_s_din =
        {1'b0, out_pong, out_ping, pair_lane, out_packet_idx};
    assign Vector_Y_Tagged_Stream_s_write = output_fire;
    assign ap_done = done_pulse;
    assign ap_ready = done_pulse;
    assign ap_idle = (state == ST_IDLE) && !done_pulse;

    CuperSpmvOnly_RtlOwnerLaneAccumulatorOoo_fadd_32ns_32ns_32_13_full_dsp_1 #(
        .ID(1),
        .NUM_STAGE(13),
        .din0_WIDTH(32),
        .din1_WIDTH(32),
        .dout_WIDTH(32)
    ) fadd_u (
        .clk(ap_clk),
        .reset(rst),
        .ce(fadd_advance),
        .din0(fadd_lhs),
        .din1(fadd_rhs),
        .dout(fadd_sum)
    );

    CuperSpmvOnly_RtlOwnerLaneUram1R1W #(
        .DATA_WIDTH(32),
        .ADDR_WIDTH(ADDR_WIDTH),
        .MEM_DEPTH(MEM_DEPTH)
    ) ping_mem_u (
        .clk(ap_clk),
        .rd_en(ping_rd_en),
        .rd_addr(ping_rd_addr),
        .rd_data(ping_rd_data),
        .wr_en(ping_wr_en),
        .wr_addr(ping_wr_addr),
        .wr_data(ping_wr_data)
    );

    CuperSpmvOnly_RtlOwnerLaneUram1R1W #(
        .DATA_WIDTH(32),
        .ADDR_WIDTH(ADDR_WIDTH),
        .MEM_DEPTH(MEM_DEPTH)
    ) pong_mem_u (
        .clk(ap_clk),
        .rd_en(pong_rd_en),
        .rd_addr(pong_rd_addr),
        .rd_data(pong_rd_data),
        .wr_en(pong_wr_en),
        .wr_addr(pong_wr_addr),
        .wr_data(pong_wr_data)
    );

    integer i;
    always @(posedge ap_clk) begin
        if (rst) begin
            state <= ST_IDLE;
            done_pulse <= 1'b0;
            iter_idx <= 32'd0;
            iteration_time <= 32'd1;
            num_out_packets <= 32'd0;
            num_owner_groups <= 32'd0;
            init_addr <= {ADDR_WIDTH{1'b0}};
            owner_group <= {ADDR_WIDTH{1'b0}};
            owner_id <= 32'd0;
            pair_lane <= 32'd0;
            meta_valid <= 1'b0;
            rd_valid <= 1'b0;
            rd_addr <= {ADDR_WIDTH{1'b0}};
            rd_is_pong <= 1'b0;
            rd_lhs <= 32'd0;
            rd_rhs <= 32'd0;
            rd_pending_valid <= 1'b0;
            rd_pending_addr <= {ADDR_WIDTH{1'b0}};
            rd_pending_is_pong <= 1'b0;
            rd_pending_rhs <= 32'd0;
            pipe_valid <= {FADD_PIPE_LATENCY{1'b0}};
            for (i = 0; i < FADD_PIPE_LATENCY; i = i + 1) begin
                pipe_addr[i] <= {ADDR_WIDTH{1'b0}};
                pipe_is_pong[i] <= 1'b0;
            end
            out_packet_idx <= 32'd0;
            out_ping <= 32'd0;
            out_pong <= 32'd0;
            out_valid <= 1'b0;
            write_read_pending <= 1'b0;
            write_pending_packet_idx <= 32'd0;
            write_pending_last <= 1'b0;
        end else begin
            done_pulse <= 1'b0;

            if (output_fire) begin
                out_valid <= 1'b0;
            end

            if (fadd_advance) begin
                for (i = FADD_PIPE_LATENCY - 1; i > 0; i = i - 1) begin
                    pipe_valid[i] <= pipe_valid[i - 1];
                    pipe_addr[i] <= pipe_addr[i - 1];
                    pipe_is_pong[i] <= pipe_is_pong[i - 1];
                end
                pipe_valid[0] <= issue_fire;
                pipe_addr[0] <= rd_addr;
                pipe_is_pong[0] <= rd_is_pong;
            end

            if (pending_to_rd) begin
                rd_valid <= 1'b1;
                rd_addr <= rd_pending_addr;
                rd_is_pong <= rd_pending_is_pong;
                rd_lhs <= rd_pending_is_pong ? pong_rd_data : ping_rd_data;
                rd_rhs <= rd_pending_rhs;
            end else if (fadd_advance && issue_fire) begin
                rd_valid <= 1'b0;
            end

            if (read_fire) begin
                rd_pending_valid <= 1'b1;
                rd_pending_addr <= input_owner_group;
                rd_pending_is_pong <= input_is_pong;
                rd_pending_rhs <= input_value;
            end else if (pending_to_rd) begin
                rd_pending_valid <= 1'b0;
            end

            case (state)
                ST_IDLE: begin
                    if (ap_start) begin
                        iter_idx <= 32'd0;
                        iteration_time <= (Iteration_num == 32'd0) ? 32'd1 : Iteration_num;
                        num_out_packets <= (Row_num + 32'd15) >> 4;
                        num_owner_groups <= (((Row_num + 32'd15) >> 4) +
                                             HBM_CHANNEL_NUM - 1) / HBM_CHANNEL_NUM;
                        init_addr <= {ADDR_WIDTH{1'b0}};
                        owner_group <= {ADDR_WIDTH{1'b0}};
                        owner_id <= Owner_id;
                        pair_lane <= Pair_lane;
                        meta_valid <= 1'b0;
                        rd_valid <= 1'b0;
                        rd_pending_valid <= 1'b0;
                        pipe_valid <= {FADD_PIPE_LATENCY{1'b0}};
                        out_valid <= 1'b0;
                        write_read_pending <= 1'b0;
                        state <= ST_INIT;
                    end
                end

                ST_INIT: begin
                    if (init_addr + {{(ADDR_WIDTH-1){1'b0}}, 1'b1} >=
                        num_owner_groups[ADDR_WIDTH-1:0]) begin
                        state <= ST_CONSUME;
                    end else begin
                        init_addr <= init_addr + {{(ADDR_WIDTH-1){1'b0}}, 1'b1};
                    end
                end

                ST_CONSUME: begin
                    if (can_accept_input) begin
                        if (!meta_valid) begin
                            meta_valid <= 1'b1;
                        end

                        if (input_done) begin
                            state <= ST_DRAIN_ADD;
                        end
                    end
                end

                ST_DRAIN_ADD: begin
                    if (!pipe_active && !rd_valid && !rd_pending_valid) begin
                        owner_group <= {ADDR_WIDTH{1'b0}};
                        write_read_pending <= 1'b0;
                        state <= ST_WRITE;
                    end
                end

                ST_WRITE: begin
                    if (write_read_pending) begin
                        if (output_can_load) begin
                            out_packet_idx <= write_pending_packet_idx;
                            out_ping <= ping_rd_data;
                            out_pong <= pong_rd_data;
                            out_valid <= (write_pending_packet_idx < num_out_packets);
                            write_read_pending <= 1'b0;

                            if (write_pending_last) begin
                                state <= ST_NEXT_ITER;
                            end
                        end
                    end else if (output_can_load) begin
                        write_read_pending <= 1'b1;
                        write_pending_packet_idx <= next_output_packet_idx;
                        write_pending_last <= owner_group_last;

                        if (!owner_group_last) begin
                            owner_group <= owner_group + {{(ADDR_WIDTH-1){1'b0}}, 1'b1};
                        end else if (num_owner_groups == 32'd0) begin
                            state <= ST_NEXT_ITER;
                        end
                    end
                end

                ST_NEXT_ITER: begin
                    if (!out_valid) begin
                        if (iter_idx + 32'd1 >= iteration_time) begin
                            state <= ST_DONE;
                        end else begin
                            iter_idx <= iter_idx + 32'd1;
                            init_addr <= {ADDR_WIDTH{1'b0}};
                            owner_group <= {ADDR_WIDTH{1'b0}};
                            owner_id <= Owner_id;
                            pair_lane <= Pair_lane;
                            meta_valid <= 1'b0;
                            rd_valid <= 1'b0;
                            rd_pending_valid <= 1'b0;
                            pipe_valid <= {FADD_PIPE_LATENCY{1'b0}};
                            write_read_pending <= 1'b0;
                            state <= ST_INIT;
                        end
                    end
                end

                ST_DONE: begin
                    done_pulse <= 1'b1;
                    state <= ST_IDLE;
                end

                default: begin
                    state <= ST_IDLE;
                end
            endcase
        end
    end

    wire unused_peek = Owner_Lane_Stream_peek_empty_n ^
                       (|Owner_Lane_Stream_peek_dout) ^
                       (|Vector_Y_Tagged_Stream_peek);
endmodule

module CuperSpmvOnly_RtlOwnerLaneUram1R1W
#(parameter integer DATA_WIDTH = 32,
  parameter integer ADDR_WIDTH = 13,
  parameter integer MEM_DEPTH = 8192)
(
    input wire clk,
    input wire rd_en,
    input wire [ADDR_WIDTH-1:0] rd_addr,
    output wire [DATA_WIDTH-1:0] rd_data,
    input wire wr_en,
    input wire [ADDR_WIDTH-1:0] wr_addr,
    input wire [DATA_WIDTH-1:0] wr_data
);
`ifdef VERILATOR
    reg [DATA_WIDTH-1:0] mem [0:MEM_DEPTH-1];
    reg [DATA_WIDTH-1:0] rd_data_reg;
    assign rd_data = rd_data_reg;

    always @(posedge clk) begin
        if (rd_en) begin
            rd_data_reg <= mem[rd_addr];
        end
        if (wr_en) begin
            mem[wr_addr] <= wr_data;
        end
    end
`else
    wire [0:0] write_enable = {wr_en};

    xpm_memory_sdpram #(
        .ADDR_WIDTH_A(ADDR_WIDTH),
        .ADDR_WIDTH_B(ADDR_WIDTH),
        .AUTO_SLEEP_TIME(0),
        .BYTE_WRITE_WIDTH_A(DATA_WIDTH),
        .CASCADE_HEIGHT(0),
        .CLOCKING_MODE("common_clock"),
        .ECC_MODE("no_ecc"),
        .MEMORY_INIT_FILE("none"),
        .MEMORY_INIT_PARAM("0"),
        .MEMORY_OPTIMIZATION("true"),
        .MEMORY_PRIMITIVE("ultra"),
        .MEMORY_SIZE(DATA_WIDTH * MEM_DEPTH),
        .MESSAGE_CONTROL(0),
        .READ_DATA_WIDTH_B(DATA_WIDTH),
        .READ_LATENCY_B(1),
        .READ_RESET_VALUE_B("0"),
        .RST_MODE_A("SYNC"),
        .RST_MODE_B("SYNC"),
        .USE_EMBEDDED_CONSTRAINT(0),
        .USE_MEM_INIT(0),
        .WAKEUP_TIME("disable_sleep"),
        .WRITE_DATA_WIDTH_A(DATA_WIDTH),
        .WRITE_MODE_B("read_first")
    ) xpm_uram (
        .dbiterrb(),
        .doutb(rd_data),
        .sbiterrb(),
        .addra(wr_addr),
        .addrb(rd_addr),
        .clka(clk),
        .clkb(clk),
        .dina(wr_data),
        .ena(wr_en),
        .enb(rd_en),
        .injectdbiterra(1'b0),
        .injectsbiterra(1'b0),
        .regceb(1'b1),
        .rstb(1'b0),
        .sleep(1'b0),
        .wea(write_enable)
    );
`endif
endmodule

`ifdef VERILATOR
module CuperSpmvOnly_RtlOwnerLaneAccumulatorOoo_fadd_32ns_32ns_32_13_full_dsp_1
#(parameter ID = 1,
  parameter NUM_STAGE = 13,
  parameter din0_WIDTH = 32,
  parameter din1_WIDTH = 32,
  parameter dout_WIDTH = 32)
(
    input wire clk,
    input wire reset,
    input wire ce,
    input wire [din0_WIDTH-1:0] din0,
    input wire [din1_WIDTH-1:0] din1,
    output reg [dout_WIDTH-1:0] dout
);
    reg [dout_WIDTH-1:0] pipe [0:31];
    integer i;

    always @(posedge clk) begin
        if (reset) begin
            dout <= {dout_WIDTH{1'b0}};
            for (i = 0; i < 32; i = i + 1) begin
                pipe[i] <= {dout_WIDTH{1'b0}};
            end
        end else if (ce) begin
            pipe[0] <= $shortrealtobits($bitstoshortreal(din0) +
                                        $bitstoshortreal(din1));
            for (i = 1; i < 32; i = i + 1) begin
                if (i < NUM_STAGE) begin
                    pipe[i] <= pipe[i - 1];
                end
            end
            dout <= pipe[NUM_STAGE - 1];
        end
    end
endmodule
`endif
