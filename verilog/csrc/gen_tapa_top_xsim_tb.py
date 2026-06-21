#!/usr/bin/env python3
import re
import sys
from pathlib import Path


def parse_ports(rtl_path: Path):
    text = rtl_path.read_text()
    match = re.search(r"module\s+CuperSpmvServiceOnly\s*\((.*?)\);\s", text, re.S)
    if not match:
        raise SystemExit(f"cannot find CuperSpmvServiceOnly port list in {rtl_path}")
    names = []
    for raw in match.group(1).split(","):
        name = raw.strip()
        if name:
            names.append(name)

    decls = {}
    for line in text.splitlines():
        stripped = re.sub(r"\(\*.*?\*\)\s*", "", line).strip()
        m = re.match(r"(input|output)\s+(?:wire\s+)?(?:reg\s+)?(\[[^]]+\]\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*;", stripped)
        if not m:
            continue
        direction, width, name = m.groups()
        if name in names:
            decls[name] = (direction, (width or "").strip())

    missing = [name for name in names if name not in decls]
    if missing:
        raise SystemExit(f"missing declarations for ports: {missing[:10]}")
    return names, decls


def normalize_width(width):
    replacements = {
        "[C_S_AXI_CONTROL_ADDR_WIDTH - 1:0]": "[8:0]",
        "[C_S_AXI_CONTROL_DATA_WIDTH - 1:0]": "[31:0]",
        "[C_S_AXI_CONTROL_WSTRB_WIDTH - 1:0]": "[3:0]",
    }
    return replacements.get(width, width)


def signal_decl(direction, width, name):
    sv_type = "wire" if direction == "output" else "logic"
    width = normalize_width(width)
    if width:
        return f"{sv_type} {width} {name};"
    return f"{sv_type} {name};"


def axi_instance(prefix, data_width, mem_name):
    init = '.INIT_FILE("")'
    return f"""
    axi_mem_model #(
        .DATA_WIDTH({data_width}),
        .ADDR_WIDTH(64),
        .MEM_WORDS(1 << 20),
        {init}
    ) {mem_name} (
        .clk(ap_clk),
        .rst_n(ap_rst_n),
        .araddr(m_axi_{prefix}_ARADDR),
        .arlen(m_axi_{prefix}_ARLEN),
        .arsize(m_axi_{prefix}_ARSIZE),
        .arvalid(m_axi_{prefix}_ARVALID),
        .arready(m_axi_{prefix}_ARREADY),
        .rdata(m_axi_{prefix}_RDATA),
        .rresp(m_axi_{prefix}_RRESP),
        .rid(m_axi_{prefix}_RID),
        .rlast(m_axi_{prefix}_RLAST),
        .rvalid(m_axi_{prefix}_RVALID),
        .rready(m_axi_{prefix}_RREADY),
        .awaddr(m_axi_{prefix}_AWADDR),
        .awlen(m_axi_{prefix}_AWLEN),
        .awsize(m_axi_{prefix}_AWSIZE),
        .awvalid(m_axi_{prefix}_AWVALID),
        .awready(m_axi_{prefix}_AWREADY),
        .wdata(m_axi_{prefix}_WDATA),
        .wstrb(m_axi_{prefix}_WSTRB),
        .wlast(m_axi_{prefix}_WLAST),
        .wvalid(m_axi_{prefix}_WVALID),
        .wready(m_axi_{prefix}_WREADY),
        .bresp(m_axi_{prefix}_BRESP),
        .bid(m_axi_{prefix}_BID),
        .bvalid(m_axi_{prefix}_BVALID),
        .bready(m_axi_{prefix}_BREADY)
    );
"""


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: gen_tapa_top_xsim_tb.py TOP_RTL OUT_SV")
    rtl_path = Path(sys.argv[1])
    out_path = Path(sys.argv[2])
    names, decls = parse_ports(rtl_path)

    lines = []
    lines.append("`timescale 1ns/1ps\n")
    lines.append("module tb_cuper_spmv_service_only_top_xsim;")
    lines.append("    localparam integer HBM_CHANNELS = 16;")
    lines.append("    localparam integer MAX_ROWS = 2097152;")
    lines.append("    logic ap_clk = 1'b0;")
    lines.append("    logic ap_rst_n = 1'b0;")
    lines.append("")
    for name in names:
        if name in {"ap_clk", "ap_rst_n"}:
            continue
        direction, width = decls[name]
        lines.append("    " + signal_decl(direction, width, name))

    lines.append("")
    lines.append("    always #5 ap_clk = ~ap_clk;")
    lines.append("")
    lines.append("    CuperSpmvServiceOnly dut (")
    conns = [f"        .{name}({name})" for name in names]
    lines.append(",\n".join(conns))
    lines.append("    );")
    lines.append("")

    for ch in range(16):
        lines.append(axi_instance(f"Matrix_data_{ch}", 512, f"matrix_mem_{ch}"))
    lines.append(axi_instance("SpElement_list_ptr", 32, "ptr_mem"))
    lines.append(axi_instance("X", 512, "x_mem"))
    lines.append(axi_instance("Y_out", 32, "y_mem"))
    lines.append(axi_instance("Status", 32, "status_mem"))
    lines.append(axi_instance("Metrics", 64, "metrics_mem"))

    lines.append(r"""
    assign s_axi_control_ARVALID = 1'b0;
    assign s_axi_control_ARADDR = 9'd0;
    assign s_axi_control_RREADY = 1'b1;
    assign s_axi_control_BREADY = 1'b1;

    logic [31:0] rows = 0;
    logic [31:0] cols = 0;
    logic [31:0] batch_num = 0;
    logic [31:0] matrix_len = 0;
    logic [31:0] iteration_num = 1;
    integer timeout_cycles = 200000;
    string data_dir;
    string expected_path;
    integer cycle = 0;
    integer errors = 0;
    integer idx = 0;
    integer y_write_count = 0;
    integer y_aw_count = 0;
    integer y_b_count = 0;
    reg [31:0] expected_y [0:MAX_ROWS-1];
    bit plusarg_found;

    task automatic axi_lite_write(input [8:0] addr, input [31:0] data);
        bit aw_done;
        bit w_done;
    begin
        aw_done = 1'b0;
        w_done = 1'b0;
        @(posedge ap_clk);
        s_axi_control_AWADDR <= addr;
        s_axi_control_WDATA <= data;
        s_axi_control_WSTRB <= 4'hf;
        s_axi_control_AWVALID <= 1'b1;
        s_axi_control_WVALID <= 1'b1;
        while (!(aw_done && w_done)) begin
            @(posedge ap_clk);
            if (!aw_done && s_axi_control_AWVALID && s_axi_control_AWREADY) begin
                s_axi_control_AWVALID <= 1'b0;
                aw_done = 1'b1;
            end
            if (!w_done && s_axi_control_WVALID && s_axi_control_WREADY) begin
                s_axi_control_WVALID <= 1'b0;
                w_done = 1'b1;
            end
        end
        wait (s_axi_control_BVALID);
        @(posedge ap_clk);
    end
    endtask

    initial begin
        s_axi_control_AWVALID = 1'b0;
        s_axi_control_AWADDR = 9'd0;
        s_axi_control_WVALID = 1'b0;
        s_axi_control_WDATA = 32'd0;
        s_axi_control_WSTRB = 4'h0;

        if (!$value$plusargs("DATA_DIR=%s", data_dir)) begin
            data_dir = "build/top_xsim_vectors";
        end
        plusarg_found = $value$plusargs("ROWS=%d", rows);
        plusarg_found = $value$plusargs("COLS=%d", cols);
        plusarg_found = $value$plusargs("BATCH_NUM=%d", batch_num);
        plusarg_found = $value$plusargs("MATRIX_LEN=%d", matrix_len);
        plusarg_found = $value$plusargs("ITERATION_NUM=%d", iteration_num);
        plusarg_found = $value$plusargs("TIMEOUT_CYCLES=%d", timeout_cycles);
        if (rows == 0 || cols == 0 || batch_num == 0) begin
            $fatal(1, "missing ROWS/COLS/BATCH_NUM plusargs");
        end
        if (rows > MAX_ROWS) begin
            $fatal(1, "ROWS exceeds MAX_ROWS");
        end
        expected_path = {data_dir, "/expected_y.mem"};
        $readmemh(expected_path, expected_y);
        matrix_mem_0.load_mem({data_dir, "/matrix00.mem"});
        matrix_mem_1.load_mem({data_dir, "/matrix01.mem"});
        matrix_mem_2.load_mem({data_dir, "/matrix02.mem"});
        matrix_mem_3.load_mem({data_dir, "/matrix03.mem"});
        matrix_mem_4.load_mem({data_dir, "/matrix04.mem"});
        matrix_mem_5.load_mem({data_dir, "/matrix05.mem"});
        matrix_mem_6.load_mem({data_dir, "/matrix06.mem"});
        matrix_mem_7.load_mem({data_dir, "/matrix07.mem"});
        matrix_mem_8.load_mem({data_dir, "/matrix08.mem"});
        matrix_mem_9.load_mem({data_dir, "/matrix09.mem"});
        matrix_mem_10.load_mem({data_dir, "/matrix10.mem"});
        matrix_mem_11.load_mem({data_dir, "/matrix11.mem"});
        matrix_mem_12.load_mem({data_dir, "/matrix12.mem"});
        matrix_mem_13.load_mem({data_dir, "/matrix13.mem"});
        matrix_mem_14.load_mem({data_dir, "/matrix14.mem"});
        matrix_mem_15.load_mem({data_dir, "/matrix15.mem"});
        ptr_mem.load_mem({data_dir, "/ptr.mem"});
        x_mem.load_mem({data_dir, "/x.mem"});
        status_mem.load_mem({data_dir, "/status_init.mem"});
        metrics_mem.load_mem({data_dir, "/metrics_init.mem"});

        repeat (10) @(posedge ap_clk);
        ap_rst_n <= 1'b1;
        repeat (10) @(posedge ap_clk);

        axi_lite_write(9'h010, 32'd0);   // SpElement_list_ptr
        axi_lite_write(9'h014, 32'd0);
        axi_lite_write(9'h01c, 32'd0);   // Matrix_data_0 base
        axi_lite_write(9'h020, 32'd0);
        axi_lite_write(9'h028, 32'd0);
        axi_lite_write(9'h02c, 32'd0);
        axi_lite_write(9'h034, 32'd0);
        axi_lite_write(9'h038, 32'd0);
        axi_lite_write(9'h040, 32'd0);
        axi_lite_write(9'h044, 32'd0);
        axi_lite_write(9'h04c, 32'd0);
        axi_lite_write(9'h050, 32'd0);
        axi_lite_write(9'h058, 32'd0);
        axi_lite_write(9'h05c, 32'd0);
        axi_lite_write(9'h064, 32'd0);
        axi_lite_write(9'h068, 32'd0);
        axi_lite_write(9'h070, 32'd0);
        axi_lite_write(9'h074, 32'd0);
        axi_lite_write(9'h07c, 32'd0);
        axi_lite_write(9'h080, 32'd0);
        axi_lite_write(9'h088, 32'd0);
        axi_lite_write(9'h08c, 32'd0);
        axi_lite_write(9'h094, 32'd0);
        axi_lite_write(9'h098, 32'd0);
        axi_lite_write(9'h0a0, 32'd0);
        axi_lite_write(9'h0a4, 32'd0);
        axi_lite_write(9'h0ac, 32'd0);
        axi_lite_write(9'h0b0, 32'd0);
        axi_lite_write(9'h0b8, 32'd0);
        axi_lite_write(9'h0bc, 32'd0);
        axi_lite_write(9'h0c4, 32'd0);
        axi_lite_write(9'h0c8, 32'd0);
        axi_lite_write(9'h0d0, 32'd0);
        axi_lite_write(9'h0d4, 32'd0);
        axi_lite_write(9'h0dc, 32'd0);   // X
        axi_lite_write(9'h0e0, 32'd0);
        axi_lite_write(9'h0e8, 32'd0);   // Y_out
        axi_lite_write(9'h0ec, 32'd0);
        axi_lite_write(9'h0f4, 32'd0);   // Status
        axi_lite_write(9'h0f8, 32'd0);
        axi_lite_write(9'h100, 32'd0);   // Metrics
        axi_lite_write(9'h104, 32'd0);
        axi_lite_write(9'h10c, batch_num);
        axi_lite_write(9'h114, matrix_len);
        axi_lite_write(9'h11c, rows);
        axi_lite_write(9'h124, cols);
        axi_lite_write(9'h12c, iteration_num);

        $display("INFO: start top xsim rows=%0d cols=%0d batch=%0d matrix_len=%0d iter=%0d",
                 rows, cols, batch_num, matrix_len, iteration_num);
        axi_lite_write(9'h000, 32'h1);
    end

    always @(posedge ap_clk) begin
        if (ap_rst_n) begin
            cycle <= cycle + 1;
            if (m_axi_Y_out_WVALID && m_axi_Y_out_WREADY) begin
                y_write_count <= y_write_count + 1;
            end
            if (m_axi_Y_out_AWVALID && m_axi_Y_out_AWREADY) begin
                y_aw_count <= y_aw_count + 1;
            end
            if (m_axi_Y_out_BVALID && m_axi_Y_out_BREADY) begin
                y_b_count <= y_b_count + 1;
            end
            if ((cycle % 20000) == 0 && cycle != 0) begin
                $display("PROGRESS cycle=%0d y_aw=%0d y_w=%0d y_b=%0d y_resp_empty_n=%0b top_state=%0d core=%0d split=%0d acc=%0d scatter=%0d ptr=%0d vec=%0d scatter_sub_done=%0b status_resp_empty_n=%0b metrics_resp_empty_n=%0b",
                         cycle,
                         y_aw_count,
                         y_write_count,
                         y_b_count,
                         dut.Y_out_write_resp__empty_n,
                         dut.__tapa_fsm_unit.tapa_state,
                         dut.__tapa_fsm_unit.CuperSpmvOnly_CoreStrip_0__state,
                         dut.__tapa_fsm_unit.CuperSpmvOnly_SourceLaneSplitterOoo_0__state,
                         dut.__tapa_fsm_unit.CuperSpmvOnly_RtlOwnerBankAccumulatorOoo_0__state,
                         dut.__tapa_fsm_unit.CuperSpmvOnly_TaggedScatterWriterOoo_0__state,
                         dut.__tapa_fsm_unit.CuperSpmvOnly_StripPtrLoader_0__state,
                         dut.__tapa_fsm_unit.Vector_Loader_0__state,
                         dut.CuperSpmvOnly_TaggedScatterWriterOoo_0.grp_CuperSpmvOnly_TaggedScatterWriterOoo_Pipeline_scatter_fu_736_ap_done,
                         dut.Status_write_resp__empty_n,
                         dut.Metrics_write_resp__empty_n);
            end

            if (dut.__tapa_fsm_unit.tapa_state == 2'b10) begin
                $display("INFO: top done cycle=%0d y_aw=%0d y_w=%0d y_b=%0d status0=%08x status1=%08x status2=%08x",
                         cycle,
                         y_aw_count,
                         y_write_count,
                         y_b_count,
                         status_mem.mem[0][31:0],
                         status_mem.mem[1][31:0],
                         status_mem.mem[2][31:0]);
                for (idx = 0; idx < rows; idx = idx + 1) begin
                    if (y_mem.mem[idx][31:0] !== expected_y[idx]) begin
                        if (errors < 16) begin
                            $display("MISMATCH row=%0d got=%08x expected=%08x",
                                     idx, y_mem.mem[idx][31:0], expected_y[idx]);
                        end
                        errors = errors + 1;
                    end
                end
                if (errors != 0) begin
                    $fatal(1, "FAIL: mismatches=%0d", errors);
                end
                $display("PASS: top xsim rows=%0d cycles=%0d y_writes=%0d", rows, cycle, y_write_count);
                $finish;
            end

            if (cycle > timeout_cycles) begin
                $display("TIMEOUT cycle=%0d y_aw=%0d y_w=%0d y_b=%0d y_resp_empty_n=%0b top_state=%0d core=%0d split=%0d acc=%0d scatter=%0d ptr=%0d vec=%0d scatter_sub_done=%0b status_resp_empty_n=%0b metrics_resp_empty_n=%0b",
                         cycle,
                         y_aw_count,
                         y_write_count,
                         y_b_count,
                         dut.Y_out_write_resp__empty_n,
                         dut.__tapa_fsm_unit.tapa_state,
                         dut.__tapa_fsm_unit.CuperSpmvOnly_CoreStrip_0__state,
                         dut.__tapa_fsm_unit.CuperSpmvOnly_SourceLaneSplitterOoo_0__state,
                         dut.__tapa_fsm_unit.CuperSpmvOnly_RtlOwnerBankAccumulatorOoo_0__state,
                         dut.__tapa_fsm_unit.CuperSpmvOnly_TaggedScatterWriterOoo_0__state,
                         dut.__tapa_fsm_unit.CuperSpmvOnly_StripPtrLoader_0__state,
                         dut.__tapa_fsm_unit.Vector_Loader_0__state,
                         dut.CuperSpmvOnly_TaggedScatterWriterOoo_0.grp_CuperSpmvOnly_TaggedScatterWriterOoo_Pipeline_scatter_fu_736_ap_done,
                         dut.Status_write_resp__empty_n,
                         dut.Metrics_write_resp__empty_n);
                $fatal(1, "FAIL: timeout");
            end
        end
    end
endmodule
""")

    out_path.write_text("\n".join(lines))


if __name__ == "__main__":
    main()
