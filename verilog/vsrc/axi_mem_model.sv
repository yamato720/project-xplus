`timescale 1ns/1ps

module axi_mem_model #(
    parameter integer DATA_WIDTH = 32,
    parameter integer ADDR_WIDTH = 64,
    parameter integer MEM_WORDS = 1048576,
    parameter string INIT_FILE = ""
) (
    input  wire                         clk,
    input  wire                         rst_n,

    input  wire [ADDR_WIDTH-1:0]        araddr,
    input  wire [7:0]                   arlen,
    input  wire [2:0]                   arsize,
    input  wire                         arvalid,
    output wire                         arready,
    output reg  [DATA_WIDTH-1:0]        rdata,
    output wire [1:0]                   rresp,
    output wire [0:0]                   rid,
    output reg                          rlast,
    output reg                          rvalid,
    input  wire                         rready,

    input  wire [ADDR_WIDTH-1:0]        awaddr,
    input  wire [7:0]                   awlen,
    input  wire [2:0]                   awsize,
    input  wire                         awvalid,
    output wire                         awready,
    input  wire [DATA_WIDTH-1:0]        wdata,
    input  wire [(DATA_WIDTH/8)-1:0]    wstrb,
    input  wire                         wlast,
    input  wire                         wvalid,
    output wire                         wready,
    output wire [1:0]                   bresp,
    output wire [0:0]                   bid,
    output reg                          bvalid,
    input  wire                         bready
);
    localparam integer BYTES_PER_WORD = DATA_WIDTH / 8;
    localparam integer ADDR_SHIFT =
        (BYTES_PER_WORD == 64) ? 6 :
        (BYTES_PER_WORD == 32) ? 5 :
        (BYTES_PER_WORD == 16) ? 4 :
        (BYTES_PER_WORD == 8)  ? 3 :
        (BYTES_PER_WORD == 4)  ? 2 :
        (BYTES_PER_WORD == 2)  ? 1 : 0;

    reg [DATA_WIDTH-1:0] mem [0:MEM_WORDS-1];
    reg [ADDR_WIDTH-1:0] read_addr = 0;
    reg [8:0] read_remaining = 0;
    reg read_active = 1'b0;
    reg [ADDR_WIDTH-1:0] write_addr = 0;
    reg [8:0] write_remaining = 0;
    reg write_active = 1'b0;
    integer arready_delay = 0;
    integer awready_delay = 0;
    integer rvalid_delay = 0;
    integer rstall_period = 0;
    integer rstall_cycles = 0;
    integer wstall_period = 0;
    integer wstall_cycles = 0;
    integer bvalid_delay = 0;
    integer arready_count = 0;
    integer awready_count = 0;
    integer rvalid_count = 0;
    integer rbeat_count = 0;
    integer wbeat_count = 0;
    integer bvalid_count = 0;
    integer pending_b_count = 0;
    integer cycle_count = 0;
    integer i;
    bit plusarg_found;

    wire r_stalled =
        (rstall_period > 0) &&
        (rstall_cycles > 0) &&
        ((cycle_count % rstall_period) < rstall_cycles);
    wire w_stalled =
        (wstall_period > 0) &&
        (wstall_cycles > 0) &&
        ((cycle_count % wstall_period) < wstall_cycles);
    wire write_last_beat = wvalid && wready && (write_remaining <= 9'd1 || wlast);
    wire b_response_issue =
        (pending_b_count > 0) && !bvalid && (bvalid_count >= bvalid_delay);

    assign arready = !read_active && (arready_count >= arready_delay);
    assign awready = !write_active && (awready_count >= awready_delay);
    assign wready = write_active && !w_stalled;
    assign rresp = 2'b00;
    assign rid = 1'b0;
    assign bresp = 2'b00;
    assign bid = 1'b0;

    task automatic load_mem(input string path);
    begin
        $readmemh(path, mem);
    end
    endtask

    initial begin
        for (i = 0; i < MEM_WORDS; i = i + 1) begin
            mem[i] = '0;
        end
        if (INIT_FILE != "") begin
            $readmemh(INIT_FILE, mem);
        end
        plusarg_found = $value$plusargs("AXI_ARREADY_DELAY=%d", arready_delay);
        plusarg_found = $value$plusargs("AXI_AWREADY_DELAY=%d", awready_delay);
        plusarg_found = $value$plusargs("AXI_RVALID_DELAY=%d", rvalid_delay);
        plusarg_found = $value$plusargs("AXI_R_STALL_PERIOD=%d", rstall_period);
        plusarg_found = $value$plusargs("AXI_R_STALL_CYCLES=%d", rstall_cycles);
        plusarg_found = $value$plusargs("AXI_W_STALL_PERIOD=%d", wstall_period);
        plusarg_found = $value$plusargs("AXI_W_STALL_CYCLES=%d", wstall_cycles);
        plusarg_found = $value$plusargs("AXI_BVALID_DELAY=%d", bvalid_delay);
    end

    always @(posedge clk) begin
        if (!rst_n) begin
            read_addr <= 0;
            read_remaining <= 0;
            read_active <= 1'b0;
            rvalid <= 1'b0;
            rlast <= 1'b0;
            rdata <= '0;
            write_addr <= 0;
            write_remaining <= 0;
            write_active <= 1'b0;
            bvalid <= 1'b0;
            pending_b_count <= 0;
            arready_count <= 0;
            awready_count <= 0;
            rvalid_count <= 0;
            rbeat_count <= 0;
            wbeat_count <= 0;
            bvalid_count <= 0;
            cycle_count <= 0;
        end else begin
            cycle_count <= cycle_count + 1;

            if (!read_active) begin
                if (arready_count < arready_delay) begin
                    arready_count <= arready_count + 1;
                end
            end else begin
                arready_count <= 0;
            end

            if (!write_active) begin
                if (awready_count < awready_delay) begin
                    awready_count <= awready_count + 1;
                end
            end else begin
                awready_count <= 0;
            end

            if (arvalid && arready) begin
                read_addr <= araddr;
                read_remaining <= {1'b0, arlen} + 9'd1;
                read_active <= 1'b1;
                rvalid_count <= 0;
                rbeat_count <= 0;
            end

            if (!rvalid && read_active && !r_stalled &&
                rvalid_count >= rvalid_delay) begin
                rdata <= mem[read_addr >> ADDR_SHIFT];
                rlast <= (read_remaining == 9'd1);
                rvalid <= 1'b1;
            end else if (!rvalid && read_active && !r_stalled) begin
                rvalid_count <= rvalid_count + 1;
            end else if (rvalid && rready) begin
                rvalid <= 1'b0;
                rlast <= 1'b0;
                read_addr <= read_addr + BYTES_PER_WORD;
                rvalid_count <= 0;
                rbeat_count <= rbeat_count + 1;
                if (read_remaining <= 9'd1) begin
                    read_remaining <= 0;
                    read_active <= 1'b0;
                end else begin
                    read_remaining <= read_remaining - 9'd1;
                end
            end

            if (awvalid && awready) begin
                write_addr <= awaddr;
                write_remaining <= {1'b0, awlen} + 9'd1;
                write_active <= 1'b1;
                wbeat_count <= 0;
            end

            if (wvalid && wready) begin
                for (i = 0; i < BYTES_PER_WORD; i = i + 1) begin
                    if (wstrb[i]) begin
                        mem[write_addr >> ADDR_SHIFT][i * 8 +: 8] <=
                            wdata[i * 8 +: 8];
                    end
                end
                write_addr <= write_addr + BYTES_PER_WORD;
                wbeat_count <= wbeat_count + 1;
                if (write_remaining <= 9'd1 || wlast) begin
                    write_remaining <= 0;
                    write_active <= 1'b0;
                end else begin
                    write_remaining <= write_remaining - 9'd1;
                end
            end

            if ((pending_b_count > 0) && !bvalid) begin
                if (bvalid_count >= bvalid_delay) begin
                    bvalid <= 1'b1;
                    bvalid_count <= 0;
                end else begin
                    bvalid_count <= bvalid_count + 1;
                end
            end

            pending_b_count <= pending_b_count +
                               (write_last_beat ? 1 : 0) -
                               (b_response_issue ? 1 : 0);

            if (bvalid && bready) begin
                bvalid <= 1'b0;
            end
        end
    end

    wire [2:0] unused_arsize = arsize;
    wire [2:0] unused_awsize = awsize;
endmodule
