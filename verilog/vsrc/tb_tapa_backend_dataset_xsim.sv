`timescale 1ns/1ps

module tb_tapa_backend_dataset_xsim;
    localparam integer HBM_CHANNELS = 16;
    localparam integer PAIR_LANES = 8;
    localparam integer MAX_ROWS = 65536;
    localparam integer MAX_STREAM_WORDS = 8192;
    localparam integer FIFO_DEPTH = 2048;
    localparam integer FIFO_ADDR_WIDTH = 11;

    reg clk = 1'b0;
    reg rst_n = 1'b0;
    reg bank_start = 1'b0;
    reg scatter_start = 1'b0;

    reg [31:0] row_num = 32'd0;
    reg [31:0] iteration_num = 32'd1;
    reg [31:0] tagged_pairs_total = 32'd0;
    reg [31:0] scalar_writes_total = 32'd0;
    reg [31:0] timeout_cycles = 32'd200000;
    real tolerance = 1.0e-3;
    string data_dir;
    string expected_path;

    wire [128:0] tagged_dout [0:HBM_CHANNELS-1];
    wire tagged_empty_n [0:HBM_CHANNELS-1];
    wire tagged_read [0:HBM_CHANNELS-1];
    wire bank_done [0:HBM_CHANNELS-1];
    wire [31:0] bank_consumed [0:HBM_CHANNELS-1];
    wire [31:0] bank_produced [0:HBM_CHANNELS-1];

    wire scatter_done;
    wire scatter_idle;
    wire scatter_ready;
    wire [63:0] y_addr;
    wire [32:0] y_data;
    wire y_addr_write;
    wire y_data_write;
    wire y_resp_read;

    reg [8:0] y_resp_dout = 9'd0;
    wire y_resp_empty_n;
    integer pending_responses = 0;
    integer cycle = 0;
    integer y_write_count = 0;
    integer scatter_read_count = 0;
    integer channel_read_count [0:HBM_CHANNELS-1];
    integer errors = 0;
    integer idx = 0;
    integer total_consumed = 0;
    integer total_produced = 0;
    integer next_pending_responses = 0;

    reg [31:0] y_mem [0:MAX_ROWS-1];
    reg [31:0] expected_y [0:MAX_ROWS-1];
    reg y_seen [0:MAX_ROWS-1];

    assign y_resp_empty_n = (pending_responses > 0);

    genvar owner;
    generate
        for (owner = 0; owner < HBM_CHANNELS; owner = owner + 1) begin : banks
            tb_tapa_backend_bank_driver #(
                .OWNER_ID(owner),
                .MAX_STREAM_WORDS(MAX_STREAM_WORDS),
                .FIFO_DEPTH(FIFO_DEPTH),
                .FIFO_ADDR_WIDTH(FIFO_ADDR_WIDTH)
            ) bank_driver_u (
                .clk(clk),
                .rst_n(rst_n),
                .ap_start(bank_start),
                .row_num(row_num),
                .iteration_num(iteration_num),
                .stream_dout(tagged_dout[owner]),
                .stream_empty_n(tagged_empty_n[owner]),
                .stream_read(tagged_read[owner]),
                .bank_done(bank_done[owner]),
                .consumed_words(bank_consumed[owner]),
                .produced_pairs(bank_produced[owner])
            );
        end
    endgenerate

    CuperSpmvOnly_TaggedScatterWriterOoo_CuperSpmvOnly_TaggedScatterWriterOoo_Pipeline_scatter scatter_u (
        .ap_clk(clk),
        .ap_rst(~rst_n),
        .ap_start(scatter_start),
        .ap_done(scatter_done),
        .ap_idle(scatter_idle),
        .ap_ready(scatter_ready),
        .scalar_writes_total(scalar_writes_total),
        .Y_out_write_resp_s_dout(y_resp_dout),
        .Y_out_write_resp_s_empty_n(y_resp_empty_n),
        .Y_out_write_resp_s_read(y_resp_read),
        .tagged_pairs_total(tagged_pairs_total),
        .Vector_Y_Tagged_Stream_0_dout(tagged_dout[0]),
        .Vector_Y_Tagged_Stream_0_empty_n(tagged_empty_n[0]),
        .Vector_Y_Tagged_Stream_0_read(tagged_read[0]),
        .Vector_Y_Tagged_Stream_1_dout(tagged_dout[1]),
        .Vector_Y_Tagged_Stream_1_empty_n(tagged_empty_n[1]),
        .Vector_Y_Tagged_Stream_1_read(tagged_read[1]),
        .Vector_Y_Tagged_Stream_2_dout(tagged_dout[2]),
        .Vector_Y_Tagged_Stream_2_empty_n(tagged_empty_n[2]),
        .Vector_Y_Tagged_Stream_2_read(tagged_read[2]),
        .Vector_Y_Tagged_Stream_3_dout(tagged_dout[3]),
        .Vector_Y_Tagged_Stream_3_empty_n(tagged_empty_n[3]),
        .Vector_Y_Tagged_Stream_3_read(tagged_read[3]),
        .Vector_Y_Tagged_Stream_4_dout(tagged_dout[4]),
        .Vector_Y_Tagged_Stream_4_empty_n(tagged_empty_n[4]),
        .Vector_Y_Tagged_Stream_4_read(tagged_read[4]),
        .Vector_Y_Tagged_Stream_5_dout(tagged_dout[5]),
        .Vector_Y_Tagged_Stream_5_empty_n(tagged_empty_n[5]),
        .Vector_Y_Tagged_Stream_5_read(tagged_read[5]),
        .Vector_Y_Tagged_Stream_6_dout(tagged_dout[6]),
        .Vector_Y_Tagged_Stream_6_empty_n(tagged_empty_n[6]),
        .Vector_Y_Tagged_Stream_6_read(tagged_read[6]),
        .Vector_Y_Tagged_Stream_7_dout(tagged_dout[7]),
        .Vector_Y_Tagged_Stream_7_empty_n(tagged_empty_n[7]),
        .Vector_Y_Tagged_Stream_7_read(tagged_read[7]),
        .Vector_Y_Tagged_Stream_8_dout(tagged_dout[8]),
        .Vector_Y_Tagged_Stream_8_empty_n(tagged_empty_n[8]),
        .Vector_Y_Tagged_Stream_8_read(tagged_read[8]),
        .Vector_Y_Tagged_Stream_9_dout(tagged_dout[9]),
        .Vector_Y_Tagged_Stream_9_empty_n(tagged_empty_n[9]),
        .Vector_Y_Tagged_Stream_9_read(tagged_read[9]),
        .Vector_Y_Tagged_Stream_10_dout(tagged_dout[10]),
        .Vector_Y_Tagged_Stream_10_empty_n(tagged_empty_n[10]),
        .Vector_Y_Tagged_Stream_10_read(tagged_read[10]),
        .Vector_Y_Tagged_Stream_11_dout(tagged_dout[11]),
        .Vector_Y_Tagged_Stream_11_empty_n(tagged_empty_n[11]),
        .Vector_Y_Tagged_Stream_11_read(tagged_read[11]),
        .Vector_Y_Tagged_Stream_12_dout(tagged_dout[12]),
        .Vector_Y_Tagged_Stream_12_empty_n(tagged_empty_n[12]),
        .Vector_Y_Tagged_Stream_12_read(tagged_read[12]),
        .Vector_Y_Tagged_Stream_13_dout(tagged_dout[13]),
        .Vector_Y_Tagged_Stream_13_empty_n(tagged_empty_n[13]),
        .Vector_Y_Tagged_Stream_13_read(tagged_read[13]),
        .Vector_Y_Tagged_Stream_14_dout(tagged_dout[14]),
        .Vector_Y_Tagged_Stream_14_empty_n(tagged_empty_n[14]),
        .Vector_Y_Tagged_Stream_14_read(tagged_read[14]),
        .Vector_Y_Tagged_Stream_15_dout(tagged_dout[15]),
        .Vector_Y_Tagged_Stream_15_empty_n(tagged_empty_n[15]),
        .Vector_Y_Tagged_Stream_15_read(tagged_read[15]),
        .Y_out_write_addr_s_din(y_addr),
        .Y_out_write_addr_s_full_n(1'b1),
        .Y_out_write_addr_s_write(y_addr_write),
        .Y_out_write_data_s_din(y_data),
        .Y_out_write_data_s_full_n(1'b1),
        .Y_out_write_data_s_write(y_data_write),
        .Y_out_write_addr_offset_load(64'd0)
    );

    always #5 clk = ~clk;

    initial begin
        if (!$value$plusargs("DATA_DIR=%s", data_dir)) begin
            data_dir = "build/backend_xsim_vectors";
        end
        void'($value$plusargs("ROWS=%d", row_num));
        void'($value$plusargs("ITERATION_NUM=%d", iteration_num));
        void'($value$plusargs("TAGGED_PAIRS_TOTAL=%d", tagged_pairs_total));
        void'($value$plusargs("SCALAR_WRITES_TOTAL=%d", scalar_writes_total));
        void'($value$plusargs("TIMEOUT_CYCLES=%d", timeout_cycles));
        void'($value$plusargs("TOL=%f", tolerance));

        if (row_num == 0 || tagged_pairs_total == 0 || scalar_writes_total == 0) begin
            $fatal(1,
                   "FAIL: ROWS/TAGGED_PAIRS_TOTAL/SCALAR_WRITES_TOTAL plusargs are required");
        end
        if (row_num > MAX_ROWS) begin
            $fatal(1, "FAIL: ROWS=%0d exceeds MAX_ROWS=%0d", row_num, MAX_ROWS);
        end

        for (idx = 0; idx < MAX_ROWS; idx = idx + 1) begin
            y_mem[idx] = 32'd0;
            expected_y[idx] = 32'd0;
            y_seen[idx] = 1'b0;
            if (idx < HBM_CHANNELS) begin
                channel_read_count[idx] = 0;
            end
        end

        expected_path = {data_dir, "/expected_y.mem"};
        $readmemh(expected_path, expected_y);
        $display("INFO: backend dataset xsim DATA_DIR=%s ROWS=%0d pairs=%0d scalars=%0d",
                 data_dir,
                 row_num,
                 tagged_pairs_total,
                 scalar_writes_total);

        repeat (5) @(posedge clk);
        rst_n <= 1'b1;
        bank_start <= 1'b1;
        scatter_start <= 1'b1;
        @(posedge clk);
        bank_start <= 1'b0;
    end

    always @(posedge clk) begin
        cycle <= cycle + 1;

        if (rst_n) begin
            next_pending_responses = pending_responses;

            for (idx = 0; idx < HBM_CHANNELS; idx = idx + 1) begin
                if (tagged_read[idx]) begin
                    scatter_read_count <= scatter_read_count + 1;
                    channel_read_count[idx] <= channel_read_count[idx] + 1;
                end
            end

            if (y_addr_write || y_data_write) begin
                if (!(y_addr_write && y_data_write)) begin
                    $fatal(1,
                           "FAIL: split write addr_write=%0b data_write=%0b",
                           y_addr_write,
                           y_data_write);
                end
                if (y_addr[63:2] >= row_num) begin
                    $fatal(1,
                           "FAIL: write address row=%0d outside ROWS=%0d raw_addr=%0d",
                           y_addr[63:2],
                           row_num,
                           y_addr);
                end
                y_mem[y_addr[17:2]] <= y_data[31:0];
                y_seen[y_addr[17:2]] <= 1'b1;
                y_write_count <= y_write_count + 1;
                next_pending_responses = next_pending_responses + 1;
            end

            if (y_resp_read) begin
                if (pending_responses <= 0) begin
                    $fatal(1, "FAIL: write response read while response FIFO empty");
                end
                next_pending_responses = next_pending_responses - 1;
            end

            pending_responses <= next_pending_responses;
        end

        if (rst_n && scatter_done) begin
            scatter_start <= 1'b0;
            total_consumed = 0;
            total_produced = 0;
            for (idx = 0; idx < HBM_CHANNELS; idx = idx + 1) begin
                total_consumed = total_consumed + bank_consumed[idx];
                total_produced = total_produced + bank_produced[idx];
                if (!bank_done[idx]) begin
                    $display("FAIL: bank %0d not done at scatter_done", idx);
                    errors = errors + 1;
                end
            end

            if (scatter_read_count != tagged_pairs_total) begin
                $display("FAIL: scatter_read_count=%0d expect=%0d",
                         scatter_read_count,
                         tagged_pairs_total);
                errors = errors + 1;
            end
            if (y_write_count != scalar_writes_total) begin
                $display("FAIL: y_write_count=%0d expect=%0d",
                         y_write_count,
                         scalar_writes_total);
                errors = errors + 1;
            end

            for (idx = 0; idx < row_num; idx = idx + 1) begin
                check_row(idx);
            end

            if (errors != 0) begin
                $fatal(1,
                       "FAIL: backend dataset errors=%0d cycles=%0d consumed=%0d produced=%0d writes=%0d reads=%0d",
                       errors,
                       cycle,
                       total_consumed,
                       total_produced,
                       y_write_count,
                       scatter_read_count);
            end

            $display("PASS: backend dataset ROWS=%0d cycles=%0d consumed=%0d produced=%0d writes=%0d reads=%0d",
                     row_num,
                     cycle,
                     total_consumed,
                     total_produced,
                     y_write_count,
                     scatter_read_count);
            $finish;
        end

        if (cycle > timeout_cycles) begin
            total_consumed = 0;
            total_produced = 0;
            for (idx = 0; idx < HBM_CHANNELS; idx = idx + 1) begin
                total_consumed = total_consumed + bank_consumed[idx];
                total_produced = total_produced + bank_produced[idx];
            end
            for (idx = 0; idx < HBM_CHANNELS; idx = idx + 1) begin
                $display("TRACE: ch%0d empty_n=%0b bank_done=%0b consumed=%0d produced=%0d read=%0d",
                         idx,
                         tagged_empty_n[idx],
                         bank_done[idx],
                         bank_consumed[idx],
                         bank_produced[idx],
                         channel_read_count[idx]);
            end
            $fatal(1,
                   "FAIL: timeout cycles=%0d consumed=%0d produced=%0d writes=%0d reads=%0d pending_resp=%0d scatter_done=%0b scatter_idle=%0b scatter_ready=%0b",
                   cycle,
                   total_consumed,
                   total_produced,
                   y_write_count,
                   scatter_read_count,
                   pending_responses,
                   scatter_done,
                   scatter_idle,
                   scatter_ready);
        end
    end

    task automatic check_row;
        input integer row;
        shortreal got_fp;
        shortreal expected_fp;
        real diff;
        begin
            got_fp = $bitstoshortreal(y_mem[row]);
            expected_fp = $bitstoshortreal(expected_y[row]);
            diff = got_fp - expected_fp;
            if (diff < 0.0) begin
                diff = -diff;
            end
            if (!y_seen[row] || diff > tolerance) begin
                if (errors < 32) begin
                    $display("FAIL: y[%0d] seen=%0b got=%08x/%f expect=%08x/%f diff=%e",
                             row,
                             y_seen[row],
                             y_mem[row],
                             got_fp,
                             expected_y[row],
                             expected_fp,
                             diff);
                end
                errors = errors + 1;
            end
        end
    endtask
endmodule

module tb_tapa_backend_bank_driver #(
    parameter integer OWNER_ID = 0,
    parameter integer MAX_STREAM_WORDS = 8192,
    parameter integer FIFO_DEPTH = 2048,
    parameter integer FIFO_ADDR_WIDTH = 11
) (
    input wire clk,
    input wire rst_n,
    input wire ap_start,
    input wire [31:0] row_num,
    input wire [31:0] iteration_num,
    output wire [128:0] stream_dout,
    output wire stream_empty_n,
    input wire stream_read,
    output wire bank_done,
    output reg [31:0] consumed_words,
    output reg [31:0] produced_pairs
);
    localparam integer PAIR_LANES = 8;

    wire bank_idle;
    wire bank_ready;
    wire bank_ap_done;
    wire [128:0] bank_out_din;
    wire bank_out_write;
    wire bank_out_full_n;
    reg bank_done_seen;

    reg [129:0] input_words [0:PAIR_LANES*MAX_STREAM_WORDS-1];
    reg [31:0] all_counts [0:127];
    reg [31:0] stream_count [0:PAIR_LANES-1];
    reg [31:0] input_idx [0:PAIR_LANES-1];
    wire [129:0] lane_dout [0:PAIR_LANES-1];
    wire lane_empty_n [0:PAIR_LANES-1];
    wire lane_read [0:PAIR_LANES-1];
    integer lane_init;
    integer lane_step;
    string data_dir;
    string path;
    integer start_index;
    integer end_index;

    assign bank_done = bank_done_seen;

    genvar lane;
    generate
        for (lane = 0; lane < PAIR_LANES; lane = lane + 1) begin : lane_inputs
            assign lane_dout[lane] =
                (input_idx[lane] < stream_count[lane]) ?
                input_words[lane * MAX_STREAM_WORDS + input_idx[lane]] :
                130'd0;
            assign lane_empty_n[lane] = (input_idx[lane] < stream_count[lane]);
        end
    endgenerate

    initial begin
        if (!$value$plusargs("DATA_DIR=%s", data_dir)) begin
            data_dir = "build/backend_xsim_vectors";
        end

        for (lane_init = 0; lane_init < PAIR_LANES * MAX_STREAM_WORDS; lane_init = lane_init + 1) begin
            input_words[lane_init] = 130'd0;
        end
        for (lane_init = 0; lane_init < 128; lane_init = lane_init + 1) begin
            all_counts[lane_init] = 32'd0;
        end

        path = {data_dir, "/counts.mem"};
        $readmemh(path, all_counts);

        for (lane_init = 0; lane_init < PAIR_LANES; lane_init = lane_init + 1) begin
            stream_count[lane_init] = all_counts[OWNER_ID * PAIR_LANES + lane_init];
            input_idx[lane_init] = 32'd0;
            if (stream_count[lane_init] > MAX_STREAM_WORDS) begin
                $fatal(1,
                       "FAIL: owner %0d lane %0d count=%0d exceeds MAX_STREAM_WORDS=%0d",
                       OWNER_ID,
                       lane_init,
                       stream_count[lane_init],
                       MAX_STREAM_WORDS);
            end
            if (stream_count[lane_init] != 0) begin
                start_index = lane_init * MAX_STREAM_WORDS;
                end_index = start_index + stream_count[lane_init] - 1;
                path = $sformatf("%s/owner%02d_lane%0d.mem",
                                 data_dir,
                                 OWNER_ID,
                                 lane_init);
                $readmemh(path, input_words, start_index, end_index);
            end
        end

        consumed_words = 32'd0;
        produced_pairs = 32'd0;
    end

    always @(posedge clk) begin
        if (!rst_n) begin
            for (lane_step = 0; lane_step < PAIR_LANES; lane_step = lane_step + 1) begin
                input_idx[lane_step] <= 32'd0;
            end
            consumed_words <= 32'd0;
            produced_pairs <= 32'd0;
            bank_done_seen <= 1'b0;
        end else begin
            if (bank_ap_done) begin
                bank_done_seen <= 1'b1;
            end
            for (lane_step = 0; lane_step < PAIR_LANES; lane_step = lane_step + 1) begin
                if (lane_read[lane_step]) begin
                    if (input_idx[lane_step] >= stream_count[lane_step]) begin
                        $fatal(1,
                               "FAIL: owner %0d lane %0d read past stream count",
                               OWNER_ID,
                               lane_step);
                    end
                    input_idx[lane_step] <= input_idx[lane_step] + 32'd1;
                    consumed_words <= consumed_words + 32'd1;
                end
            end
            if (bank_out_write && bank_out_full_n) begin
                produced_pairs <= produced_pairs + 32'd1;
            end
        end
    end

    tapa_backend_stream_fifo #(
        .DATA_WIDTH(129),
        .DEPTH(FIFO_DEPTH),
        .ADDR_WIDTH(FIFO_ADDR_WIDTH)
    ) out_fifo_u (
        .clk(clk),
        .rst_n(rst_n),
        .s_din(bank_out_din),
        .s_full_n(bank_out_full_n),
        .s_write(bank_out_write),
        .m_dout(stream_dout),
        .m_empty_n(stream_empty_n),
        .m_read(stream_read)
    );

    CuperSpmvOnly_RtlOwnerBankAccumulatorOoo bank_u (
        .ap_clk(clk),
        .ap_rst_n(rst_n),
        .ap_start(ap_start),
        .ap_done(bank_ap_done),
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
        .Owner_id(OWNER_ID[31:0])
    );
endmodule

module tapa_backend_stream_fifo #(
    parameter integer DATA_WIDTH = 129,
    parameter integer DEPTH = 2048,
    parameter integer ADDR_WIDTH = 11
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
