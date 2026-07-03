`timescale 1 ns / 1 ps

// Lightweight simulation model for the Xilinx floating_point add IP used by
// Chisel/RTL Cuper SpMV data-path experiments.
module CuperSpmvOnly_RtlOwnerBankAccumulatorOoo_fadd_32ns_32ns_32_13_full_dsp_1_ip (
    input  wire        aclk,
    input  wire        aclken,
    input  wire        s_axis_a_tvalid,
    input  wire [31:0] s_axis_a_tdata,
    input  wire        s_axis_b_tvalid,
    input  wire [31:0] s_axis_b_tdata,
    output wire        m_axis_result_tvalid,
    output wire [31:0] m_axis_result_tdata
);
    localparam integer C_LATENCY = 11;

    reg [31:0] data_pipe [0:C_LATENCY-1];
    reg [C_LATENCY-1:0] valid_pipe;
    integer i;

    assign m_axis_result_tvalid = valid_pipe[C_LATENCY-1];
    assign m_axis_result_tdata = data_pipe[C_LATENCY-1];

    always @(posedge aclk) begin
        if (aclken) begin
            data_pipe[0] <= $shortrealtobits($bitstoshortreal(s_axis_a_tdata) +
                                             $bitstoshortreal(s_axis_b_tdata));
            valid_pipe[0] <= s_axis_a_tvalid & s_axis_b_tvalid;
            for (i = 1; i < C_LATENCY; i = i + 1) begin
                data_pipe[i] <= data_pipe[i - 1];
                valid_pipe[i] <= valid_pipe[i - 1];
            end
        end
    end
endmodule
