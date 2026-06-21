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
if {![info exists ::env(XSIM_ROWS)]} {
  error "XSIM_ROWS is not set"
}
if {![info exists ::env(XSIM_ITERATION_NUM)]} {
  error "XSIM_ITERATION_NUM is not set"
}
if {![info exists ::env(XSIM_TAGGED_PAIRS_TOTAL)]} {
  error "XSIM_TAGGED_PAIRS_TOTAL is not set"
}
if {![info exists ::env(XSIM_SCALAR_WRITES_TOTAL)]} {
  error "XSIM_SCALAR_WRITES_TOTAL is not set"
}
if {[info exists ::env(XSIM_TIMEOUT_CYCLES)]} {
  set timeout_cycles $::env(XSIM_TIMEOUT_CYCLES)
} else {
  set timeout_cycles 300000
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

create_project -force backend_dataset_xsim [file join $build_dir project] -part $part
set_property XPM_LIBRARIES {XPM_MEMORY} [current_project]

set ip_tcl [file join $rtl_dir CuperSpmvOnly_RtlOwnerBankAccumulatorOoo_fadd_32ns_32ns_32_13_full_dsp_1_ip.tcl]
if {![file exists $ip_tcl]} {
  error "missing owner-bank floating-point IP Tcl: $ip_tcl"
}
source $ip_tcl

set generated_rtl_files [list \
  CuperSpmvOnly_TaggedScatterWriterOoo_flow_control_loop_pipe_sequential_init.v \
  CuperSpmvOnly_TaggedScatterWriterOoo_CuperSpmvOnly_TaggedScatterWriterOoo_Pipeline_scatter.v \
]

foreach rtl_file $generated_rtl_files {
  set path [file join $rtl_dir $rtl_file]
  if {![file exists $path]} {
    error "missing generated RTL: $path"
  }
  add_files -norecurse $path
  add_files -fileset sim_1 -norecurse $path
}

set ip_sim_file [file join $build_dir project backend_dataset_xsim.gen sources_1 ip CuperSpmvOnly_RtlOwnerBankAccumulatorOoo_fadd_32ns_32ns_32_13_full_dsp_1_ip sim CuperSpmvOnly_RtlOwnerBankAccumulatorOoo_fadd_32ns_32ns_32_13_full_dsp_1_ip.v]
if {[file exists $ip_sim_file]} {
  add_files -fileset sim_1 -norecurse $ip_sim_file
}
set ip_sim_dir [file dirname $ip_sim_file]

add_files -norecurse [file join $verilog_dir tapa CuperSpmvOnly_RtlOwnerBankAccumulatorOoo.v]
add_files -fileset sim_1 -norecurse [file join $verilog_dir tapa CuperSpmvOnly_RtlOwnerBankAccumulatorOoo.v]
add_files -fileset sim_1 -norecurse [file join $verilog_dir tapa CuperSpmvOnly_RtlOwnerBankAccumulatorOoo_fadd_32ns_32ns_32_13_full_dsp_1.v]
add_files -fileset sim_1 -norecurse [file join $verilog_dir vsrc tb_tapa_backend_dataset_xsim.sv]

set_property include_dirs [list \
  [file join $verilog_dir tapa] \
  $ip_sim_dir \
  $rtl_dir \
  [file join $verilog_dir vsrc] \
] [get_filesets sources_1]
set_property include_dirs [list \
  [file join $verilog_dir tapa] \
  $ip_sim_dir \
  $rtl_dir \
  [file join $verilog_dir vsrc] \
] [get_filesets sim_1]

set_property top tb_tapa_backend_dataset_xsim [get_filesets sim_1]
set_property xsim.simulate.runtime all [get_filesets sim_1]
set_property -name xsim.simulate.xsim.more_options \
  -value "-testplusarg DATA_DIR=$data_dir -testplusarg ROWS=$::env(XSIM_ROWS) -testplusarg ITERATION_NUM=$::env(XSIM_ITERATION_NUM) -testplusarg TAGGED_PAIRS_TOTAL=$::env(XSIM_TAGGED_PAIRS_TOTAL) -testplusarg SCALAR_WRITES_TOTAL=$::env(XSIM_SCALAR_WRITES_TOTAL) -testplusarg TIMEOUT_CYCLES=$timeout_cycles" \
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
