`timescale 1 ns / 1 ps

// TAPA custom RTL smoke task for the SpMV owner-lane path.
//
// This module intentionally keeps the payload unchanged.  It proves the
// non_synthesizable/custom-rtl boundary and the TAPA FIFO handshake before the
// real FP32 out-of-order accumulator replaces the body.
// The owner-lane payload is 130 bits in the generated Vitis HLS interface:
// {padding, value[31:0], scalar_lane[31:0], pair_lane[31:0], packet_idx[31:0],
//  done[0]}.
module CuperSpmvOnly_RtlOwnerLanePassThrough (
    ap_clk,
    ap_rst_n,
    ap_start,
    ap_done,
    ap_idle,
    ap_ready,
    Iteration_num,
    Owner_Lane_Rtl_In_Stream_s_dout,
    Owner_Lane_Rtl_In_Stream_s_empty_n,
    Owner_Lane_Rtl_In_Stream_s_read,
    Owner_Lane_Rtl_In_Stream_peek_dout,
    Owner_Lane_Rtl_In_Stream_peek_empty_n,
    Owner_Lane_Rtl_In_Stream_peek_read,
    Owner_Lane_Rtl_Out_Stream_s_din,
    Owner_Lane_Rtl_Out_Stream_s_full_n,
    Owner_Lane_Rtl_Out_Stream_s_write,
    Owner_Lane_Rtl_Out_Stream_peek
);
    input ap_clk;
    input ap_rst_n;
    input ap_start;
    output ap_done;
    output ap_idle;
    output ap_ready;
    input [31:0] Iteration_num;

    input [129:0] Owner_Lane_Rtl_In_Stream_s_dout;
    input Owner_Lane_Rtl_In_Stream_s_empty_n;
    output Owner_Lane_Rtl_In_Stream_s_read;
    input [129:0] Owner_Lane_Rtl_In_Stream_peek_dout;
    input Owner_Lane_Rtl_In_Stream_peek_empty_n;
    output Owner_Lane_Rtl_In_Stream_peek_read;

    output [129:0] Owner_Lane_Rtl_Out_Stream_s_din;
    input Owner_Lane_Rtl_Out_Stream_s_full_n;
    output Owner_Lane_Rtl_Out_Stream_s_write;
    input [129:0] Owner_Lane_Rtl_Out_Stream_peek;

    reg active;
    reg done_pulse;
    reg [31:0] done_count;

    wire rst = ~ap_rst_n;
    wire [31:0] iteration_time = (Iteration_num == 32'd0) ? 32'd1 : Iteration_num;
    wire transfer = active &
                    Owner_Lane_Rtl_In_Stream_s_empty_n &
                    Owner_Lane_Rtl_Out_Stream_s_full_n;
    wire token_done = Owner_Lane_Rtl_In_Stream_s_dout[0];
    wire last_done_token = token_done && ((done_count + 32'd1) >= iteration_time);

    assign Owner_Lane_Rtl_In_Stream_s_read = transfer;
    assign Owner_Lane_Rtl_In_Stream_peek_read = 1'b0;
    assign Owner_Lane_Rtl_Out_Stream_s_din = Owner_Lane_Rtl_In_Stream_s_dout;
    assign Owner_Lane_Rtl_Out_Stream_s_write = transfer;

    assign ap_done = done_pulse;
    assign ap_ready = done_pulse;
    assign ap_idle = (~active) & (~done_pulse);

    always @(posedge ap_clk) begin
        if (rst) begin
            active <= 1'b0;
            done_pulse <= 1'b0;
            done_count <= 32'd0;
        end else begin
            done_pulse <= 1'b0;

            if (!active) begin
                if (ap_start) begin
                    active <= 1'b1;
                    done_count <= 32'd0;
                end
            end else if (transfer && last_done_token) begin
                active <= 1'b0;
                done_count <= 32'd0;
                done_pulse <= 1'b1;
            end else if (transfer && token_done) begin
                done_count <= done_count + 32'd1;
            end
        end
    end

    // Keep unused peek signals visible to lint without changing hardware.
    wire unused_peek = Owner_Lane_Rtl_In_Stream_peek_empty_n ^
                       (|Owner_Lane_Rtl_In_Stream_peek_dout) ^
                       (|Owner_Lane_Rtl_Out_Stream_peek);
endmodule
