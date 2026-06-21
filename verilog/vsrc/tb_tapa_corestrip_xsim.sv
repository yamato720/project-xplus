`timescale 1ns/1ps

module tb_tapa_corestrip_xsim;
    localparam integer HBM_CHANNELS = 16;
    localparam integer PARAM_COUNT = 4 + HBM_CHANNELS + HBM_CHANNELS;
    localparam integer X_COUNT = 1;
    localparam integer MATRIX_COUNT = 2;
    localparam integer VECTOR_Y_PARAM_COUNT = 5;
    localparam integer PE_PARAM_OUT_COUNT = 4 + (HBM_CHANNELS - 1) * 2;
    localparam integer ROWS_PER_PACKET = 8;
    localparam [31:0] FP_ONE = 32'h3f800000;

    reg clk = 1'b0;
    reg rst_n = 1'b0;
    reg ap_start = 1'b0;
    wire ap_done;
    wire ap_idle;
    wire ap_ready;

    reg [32:0] param_mem [0:PARAM_COUNT-1];
    reg [512:0] x_mem [0:X_COUNT-1];
    reg [512:0] matrix_mem [0:MATRIX_COUNT-1];
    integer param_idx = 0;
    integer x_idx = 0;
    integer matrix_idx = 0;
    integer vector_y_param_count = 0;
    integer pe_param_out_count = 0;
    integer x_out_count = 0;
    integer matrix_out_count = 0;
    integer cycle = 0;
    integer errors = 0;
    integer i;

    wire [32:0] pe_param_in_dout =
        (param_idx < PARAM_COUNT) ? param_mem[param_idx] : 33'd0;
    wire pe_param_in_empty_n = (param_idx < PARAM_COUNT);
    wire pe_param_in_read;

    wire [512:0] matrix_in_dout =
        (matrix_idx < MATRIX_COUNT) ? matrix_mem[matrix_idx] : 513'd0;
    wire matrix_in_empty_n = (matrix_idx < MATRIX_COUNT);
    wire matrix_in_read;

    wire [512:0] x_in_dout =
        (x_idx < X_COUNT) ? x_mem[x_idx] : 513'd0;
    wire x_in_empty_n = (x_idx < X_COUNT);
    wire x_in_read;

    wire [32:0] pe_param_out_din;
    wire pe_param_out_write;
    wire [512:0] x_out_din;
    wire x_out_write;
    wire [32:0] vector_y_param_din;
    wire vector_y_param_write;
    wire [400:0] matrix_out_din;
    wire matrix_out_write;

    function automatic [31:0] fp_value;
        input integer packet_idx;
        input integer lane;
        begin
            case (packet_idx * ROWS_PER_PACKET + lane)
                0: fp_value = 32'h41200000;  // 10.0
                1: fp_value = 32'h41300000;  // 11.0
                2: fp_value = 32'h41400000;  // 12.0
                3: fp_value = 32'h41500000;  // 13.0
                4: fp_value = 32'h41600000;  // 14.0
                5: fp_value = 32'h41700000;  // 15.0
                6: fp_value = 32'h41800000;  // 16.0
                7: fp_value = 32'h41880000;  // 17.0
                8: fp_value = 32'h42c80000;  // 100.0
                9: fp_value = 32'h42ca0000;  // 101.0
                10: fp_value = 32'h42cc0000; // 102.0
                11: fp_value = 32'h42ce0000; // 103.0
                12: fp_value = 32'h42d00000; // 104.0
                13: fp_value = 32'h42d20000; // 105.0
                14: fp_value = 32'h42d40000; // 106.0
                default: fp_value = 32'h42d60000; // 107.0
            endcase
        end
    endfunction

    function automatic [512:0] pack_x_packet;
        integer lane;
        reg [512:0] word;
        begin
            word = 513'd0;
            for (lane = 0; lane < 16; lane = lane + 1) begin
                word[lane * 32 +: 32] = FP_ONE;
            end
            pack_x_packet = word;
        end
    endfunction

    function automatic [512:0] pack_matrix_packet;
        input integer packet_idx;
        integer lane;
        integer base;
        reg [512:0] word;
        reg [17:0] row;
        begin
            word = 513'd0;
            for (lane = 0; lane < ROWS_PER_PACKET; lane = lane + 1) begin
                base = lane * 64;
                row = packet_idx * ROWS_PER_PACKET + lane;
                word[base +: 32] = fp_value(packet_idx, lane);
                word[base + 32 +: 18] = row;
                word[base + 50 +: 14] = lane[13:0];
            end
            pack_matrix_packet = word;
        end
    endfunction

    function automatic [17:0] out_row;
        input [400:0] word;
        input integer lane;
        begin
            out_row = word[lane * 18 +: 18];
        end
    endfunction

    function automatic [31:0] out_val;
        input [400:0] word;
        input integer lane;
        begin
            out_val = word[144 + lane * 32 +: 32];
        end
    endfunction

    CuperSpmvOnly_CoreStrip dut (
        .ap_clk(clk),
        .ap_rst_n(rst_n),
        .ap_start(ap_start),
        .ap_done(ap_done),
        .ap_idle(ap_idle),
        .ap_ready(ap_ready),
        .PE_Param_in_s_dout(pe_param_in_dout),
        .PE_Param_in_s_empty_n(pe_param_in_empty_n),
        .PE_Param_in_s_read(pe_param_in_read),
        .PE_Param_in_peek_dout(33'd0),
        .PE_Param_in_peek_empty_n(1'b0),
        .PE_Param_in_peek_read(),
        .Matrix_A_Stream_s_dout(matrix_in_dout),
        .Matrix_A_Stream_s_empty_n(matrix_in_empty_n),
        .Matrix_A_Stream_s_read(matrix_in_read),
        .Matrix_A_Stream_peek_dout(513'd0),
        .Matrix_A_Stream_peek_empty_n(1'b0),
        .Matrix_A_Stream_peek_read(),
        .Vector_X_Stream_in_s_dout(x_in_dout),
        .Vector_X_Stream_in_s_empty_n(x_in_empty_n),
        .Vector_X_Stream_in_s_read(x_in_read),
        .Vector_X_Stream_in_peek_dout(513'd0),
        .Vector_X_Stream_in_peek_empty_n(1'b0),
        .Vector_X_Stream_in_peek_read(),
        .PE_Param_out_s_din(pe_param_out_din),
        .PE_Param_out_s_full_n(1'b1),
        .PE_Param_out_s_write(pe_param_out_write),
        .PE_Param_out_peek(33'd0),
        .Vector_X_Stream_out_s_din(x_out_din),
        .Vector_X_Stream_out_s_full_n(1'b1),
        .Vector_X_Stream_out_s_write(x_out_write),
        .Vector_X_Stream_out_peek(513'd0),
        .Vector_Y_Param_s_din(vector_y_param_din),
        .Vector_Y_Param_s_full_n(1'b1),
        .Vector_Y_Param_s_write(vector_y_param_write),
        .Vector_Y_Param_peek(33'd0),
        .Matrix_Mult_Vector_Stream_s_din(matrix_out_din),
        .Matrix_Mult_Vector_Stream_s_full_n(1'b1),
        .Matrix_Mult_Vector_Stream_s_write(matrix_out_write),
        .Matrix_Mult_Vector_Stream_peek(401'd0),
        .Core_id(32'd0)
    );

    always #5 clk = ~clk;

    initial begin
        param_mem[0] = 33'd1;
        param_mem[1] = 33'd16;
        param_mem[2] = 33'd1;
        param_mem[3] = 33'd16;
        for (i = 0; i < HBM_CHANNELS; i = i + 1) begin
            param_mem[4 + i] = 33'd0;
            param_mem[4 + HBM_CHANNELS + i] = 33'd2;
        end
        x_mem[0] = pack_x_packet();
        matrix_mem[0] = pack_matrix_packet(0);
        matrix_mem[1] = pack_matrix_packet(1);

        repeat (5) @(posedge clk);
        rst_n <= 1'b1;
        @(posedge clk);
        ap_start <= 1'b1;
        @(posedge clk);
        ap_start <= 1'b0;
    end

    always @(posedge clk) begin
        cycle <= cycle + 1;

        if (!rst_n) begin
            param_idx <= 0;
            x_idx <= 0;
            matrix_idx <= 0;
            vector_y_param_count <= 0;
            pe_param_out_count <= 0;
            x_out_count <= 0;
            matrix_out_count <= 0;
            errors <= 0;
        end else begin
            if (pe_param_in_read && pe_param_in_empty_n) begin
                param_idx <= param_idx + 1;
            end
            if (x_in_read && x_in_empty_n) begin
                x_idx <= x_idx + 1;
            end
            if (matrix_in_read && matrix_in_empty_n) begin
                matrix_idx <= matrix_idx + 1;
            end
            if (pe_param_out_write) begin
                pe_param_out_count <= pe_param_out_count + 1;
            end
            if (x_out_write) begin
                x_out_count <= x_out_count + 1;
            end
            if (vector_y_param_write) begin
                case (vector_y_param_count)
                    0: if (vector_y_param_din[31:0] !== 32'd1) begin
                           $display("FAIL: Vector_Y_Param[0]=%0d", vector_y_param_din[31:0]);
                           errors <= errors + 1;
                       end
                    1: if (vector_y_param_din[31:0] !== 32'd16) begin
                           $display("FAIL: Vector_Y_Param[1]=%0d", vector_y_param_din[31:0]);
                           errors <= errors + 1;
                       end
                    2: if (vector_y_param_din[31:0] !== 32'd1) begin
                           $display("FAIL: Vector_Y_Param[2]=%0d", vector_y_param_din[31:0]);
                           errors <= errors + 1;
                       end
                    3: if (vector_y_param_din[31:0] !== 32'd0) begin
                           $display("FAIL: Vector_Y_Param[3]=%0d", vector_y_param_din[31:0]);
                           errors <= errors + 1;
                       end
                    4: if (vector_y_param_din[31:0] !== 32'd2) begin
                           $display("FAIL: Vector_Y_Param[4]=%0d", vector_y_param_din[31:0]);
                           errors <= errors + 1;
                       end
                    default: begin
                        $display("FAIL: unexpected Vector_Y_Param[%0d]=%0d",
                                 vector_y_param_count, vector_y_param_din[31:0]);
                        errors <= errors + 1;
                    end
                endcase
                vector_y_param_count <= vector_y_param_count + 1;
            end
            if (matrix_out_write) begin
                for (i = 0; i < ROWS_PER_PACKET; i = i + 1) begin
                    if (out_row(matrix_out_din, i) !==
                        (matrix_out_count * ROWS_PER_PACKET + i)) begin
                        $display("FAIL: matrix_out[%0d].row[%0d]=%0d expect=%0d",
                                 matrix_out_count, i, out_row(matrix_out_din, i),
                                 matrix_out_count * ROWS_PER_PACKET + i);
                        errors <= errors + 1;
                    end
                    if (out_val(matrix_out_din, i) !==
                        fp_value(matrix_out_count, i)) begin
                        $display("FAIL: matrix_out[%0d].val[%0d]=%08x expect=%08x",
                                 matrix_out_count, i, out_val(matrix_out_din, i),
                                 fp_value(matrix_out_count, i));
                        errors <= errors + 1;
                    end
                end
                matrix_out_count <= matrix_out_count + 1;
            end

            if (ap_done) begin
                if (param_idx != PARAM_COUNT) begin
                    $display("FAIL: param_idx=%0d expect=%0d", param_idx, PARAM_COUNT);
                    errors <= errors + 1;
                end
                if (x_idx != X_COUNT) begin
                    $display("FAIL: x_idx=%0d expect=%0d", x_idx, X_COUNT);
                    errors <= errors + 1;
                end
                if (matrix_idx != MATRIX_COUNT) begin
                    $display("FAIL: matrix_idx=%0d expect=%0d", matrix_idx, MATRIX_COUNT);
                    errors <= errors + 1;
                end
                if (vector_y_param_count != VECTOR_Y_PARAM_COUNT) begin
                    $display("FAIL: vector_y_param_count=%0d expect=%0d",
                             vector_y_param_count, VECTOR_Y_PARAM_COUNT);
                    errors <= errors + 1;
                end
                if (pe_param_out_count != PE_PARAM_OUT_COUNT) begin
                    $display("FAIL: pe_param_out_count=%0d expect=%0d",
                             pe_param_out_count, PE_PARAM_OUT_COUNT);
                    errors <= errors + 1;
                end
                if (x_out_count != X_COUNT) begin
                    $display("FAIL: x_out_count=%0d expect=%0d", x_out_count, X_COUNT);
                    errors <= errors + 1;
                end
                if (matrix_out_count != MATRIX_COUNT) begin
                    $display("FAIL: matrix_out_count=%0d expect=%0d",
                             matrix_out_count, MATRIX_COUNT);
                    errors <= errors + 1;
                end
                if (errors == 0) begin
                    $display("PASS: CoreStrip xsim cycles=%0d matrix_packets=%0d",
                             cycle, matrix_out_count);
                    $finish;
                end else begin
                    $fatal(1, "FAIL: CoreStrip xsim errors=%0d", errors);
                end
            end
        end

        if (cycle > 20000) begin
            $fatal(1,
                   "FAIL: timeout param=%0d x=%0d matrix=%0d vy=%0d mout=%0d done=%0b",
                   param_idx, x_idx, matrix_idx, vector_y_param_count,
                   matrix_out_count, ap_done);
        end
    end
endmodule
