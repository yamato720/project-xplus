set script_dir [file dirname [file normalize [info script]]]
set verilog_dir [file dirname $script_dir]

if {![info exists ::env(TAPA_RTL_DIR)]} {
  error "TAPA_RTL_DIR is not set"
}
if {![info exists ::env(XSIM_BUILD_DIR)]} {
  error "XSIM_BUILD_DIR is not set"
}
if {![info exists ::env(XSIM_DATA_DIR)]} {
  error "XSIM_DATA_DIR is not set"
}
foreach var {XSIM_ROWS XSIM_COLS XSIM_BATCH_NUM XSIM_MATRIX_LEN XSIM_ITERATION_NUM} {
  if {![info exists ::env($var)]} {
    error "$var is not set"
  }
}
if {[info exists ::env(XSIM_TIMEOUT_CYCLES)]} {
  set timeout_cycles $::env(XSIM_TIMEOUT_CYCLES)
} else {
  set timeout_cycles 300000
}
set axi_plusargs ""
foreach {env_name plusarg_name} {
  XSIM_AXI_ARREADY_DELAY AXI_ARREADY_DELAY
  XSIM_AXI_AWREADY_DELAY AXI_AWREADY_DELAY
  XSIM_AXI_RVALID_DELAY AXI_RVALID_DELAY
  XSIM_AXI_R_STALL_PERIOD AXI_R_STALL_PERIOD
  XSIM_AXI_R_STALL_CYCLES AXI_R_STALL_CYCLES
  XSIM_AXI_W_STALL_PERIOD AXI_W_STALL_PERIOD
  XSIM_AXI_W_STALL_CYCLES AXI_W_STALL_CYCLES
  XSIM_AXI_BVALID_DELAY AXI_BVALID_DELAY
} {
  if {[info exists ::env($env_name)] && $::env($env_name) ne ""} {
    append axi_plusargs " -testplusarg $plusarg_name=$::env($env_name)"
  }
}
if {[info exists ::env(VIVADO_PART)]} {
  set part $::env(VIVADO_PART)
} else {
  set part "xcu55c-fsvh2892-2L-e"
}

set rtl_dir [file normalize $::env(TAPA_RTL_DIR)]
set build_dir [file normalize $::env(XSIM_BUILD_DIR)]
set data_dir [file normalize $::env(XSIM_DATA_DIR)]
file mkdir $build_dir

create_project -force cuper_spmv_service_only_top_xsim [file join $build_dir project] -part $part
set_property XPM_LIBRARIES {XPM_MEMORY} [current_project]

foreach ip_tcl [list \
  CuperSpmvOnly_CoreStrip_fmul_32ns_32ns_32_8_max_dsp_1_ip.tcl \
  CuperSpmvOnly_RtlOwnerBankAccumulatorOoo_fadd_32ns_32ns_32_13_full_dsp_1_ip.tcl \
  CuperSpmvOnly_TaggedScatterWriterOoo_sitodp_32s_64_7_no_dsp_1_ip.tcl \
] {
  set path [file join $rtl_dir $ip_tcl]
  if {[file exists $path]} {
    source $path
  }
}

set rtl_files [glob -nocomplain [file join $rtl_dir *.v]]
foreach path $rtl_files {
  set tail [file tail $path]
  if {$tail eq "CuperSpmvOnly_RtlOwnerBankAccumulatorOoo.v"} {
    continue
  }
  add_files -norecurse $path
  add_files -fileset sim_1 -norecurse $path
}

add_files -norecurse [file join $verilog_dir tapa CuperSpmvOnly_RtlOwnerBankAccumulatorOoo.v]
add_files -fileset sim_1 -norecurse [file join $verilog_dir tapa CuperSpmvOnly_RtlOwnerBankAccumulatorOoo.v]
add_files -fileset sim_1 -norecurse [file join $verilog_dir tapa CuperSpmvOnly_RtlOwnerBankAccumulatorOoo_fadd_32ns_32ns_32_13_full_dsp_1.v]
add_files -fileset sim_1 -norecurse [file join $verilog_dir vsrc axi_mem_model.sv]
add_files -fileset sim_1 -norecurse [file join $build_dir tb_cuper_spmv_service_only_top_xsim.sv]

set ip_sim_dirs [glob -nocomplain -types d [file join $build_dir project cuper_spmv_service_only_top_xsim.gen sources_1 ip * sim]]
set include_dirs [concat [list \
  [file join $verilog_dir tapa] \
  $rtl_dir \
  [file join $verilog_dir vsrc] \
  $data_dir \
] $ip_sim_dirs]

set_property include_dirs $include_dirs [get_filesets sources_1]
set_property include_dirs $include_dirs [get_filesets sim_1]
set_property top tb_cuper_spmv_service_only_top_xsim [get_filesets sim_1]
set_property xsim.simulate.runtime all [get_filesets sim_1]
set_property -name xsim.simulate.xsim.more_options \
  -value "-testplusarg DATA_DIR=$data_dir -testplusarg ROWS=$::env(XSIM_ROWS) -testplusarg COLS=$::env(XSIM_COLS) -testplusarg BATCH_NUM=$::env(XSIM_BATCH_NUM) -testplusarg MATRIX_LEN=$::env(XSIM_MATRIX_LEN) -testplusarg ITERATION_NUM=$::env(XSIM_ITERATION_NUM) -testplusarg TIMEOUT_CYCLES=$timeout_cycles$axi_plusargs" \
  -objects [get_filesets sim_1]
set_property -name xsim.elaborate.xelab.more_options \
  -value {-L xbip_utils_v3_0_10 -L axi_utils_v2_0_6 -L xbip_pipe_v3_0_6 -L xbip_dsp48_wrapper_v3_0_4 -L xbip_dsp48_addsub_v3_0_6 -L xbip_dsp48_multadd_v3_0_6 -L xbip_bram18k_v3_0_6 -L mult_gen_v12_0_18 -L floating_point_v7_1_15} \
  -objects [get_filesets sim_1]
set_property verilog_define {CUPER_SPMV_ONLY_EXTERNAL_FADD_WRAPPER=1} [get_filesets sim_1]

update_compile_order -fileset sources_1
update_compile_order -fileset sim_1

launch_simulation -simset sim_1 -mode behavioral
close_sim
close_project
