set script_dir [file dirname [file normalize [info script]]]
set verilog_dir [file dirname $script_dir]

if {![info exists ::env(TAPA_RTL_DIR)]} {
  error "TAPA_RTL_DIR is not set"
}
if {![info exists ::env(XSIM_BUILD_DIR)]} {
  error "XSIM_BUILD_DIR is not set"
}
if {[info exists ::env(VIVADO_PART)]} {
  set part $::env(VIVADO_PART)
} else {
  set part "xcu55c-fsvh2892-2L-e"
}

set rtl_dir [file normalize $::env(TAPA_RTL_DIR)]
set build_dir [file normalize $::env(XSIM_BUILD_DIR)]
file mkdir $build_dir

create_project -force corestrip_xsim [file join $build_dir project] -part $part

set ip_tcl [file join $rtl_dir CuperSpmvOnly_CoreStrip_fmul_32ns_32ns_32_8_max_dsp_1_ip.tcl]
if {![file exists $ip_tcl]} {
  error "missing CoreStrip floating-point IP Tcl: $ip_tcl"
}
source $ip_tcl

set rtl_files [list \
  CuperSpmvOnly_CoreStrip_flow_control_loop_pipe_sequential_init.v \
  CuperSpmvOnly_CoreStrip_mux_83_32_1_1.v \
  CuperSpmvOnly_CoreStrip_local_X_RAM_AUTO_1R1W.v \
  CuperSpmvOnly_CoreStrip_fmul_32ns_32ns_32_8_max_dsp_1.v \
  CuperSpmvOnly_CoreStrip_CuperSpmvOnly_CoreStrip_Pipeline_read_boundary_group.v \
  CuperSpmvOnly_CoreStrip_CuperSpmvOnly_CoreStrip_Pipeline_read_boundary_group1.v \
  CuperSpmvOnly_CoreStrip_CuperSpmvOnly_CoreStrip_Pipeline_load_vector.v \
  CuperSpmvOnly_CoreStrip_CuperSpmvOnly_CoreStrip_Pipeline_decode_matrix.v \
  CuperSpmvOnly_CoreStrip.v \
]

foreach rtl_file $rtl_files {
  set path [file join $rtl_dir $rtl_file]
  if {![file exists $path]} {
    error "missing generated RTL: $path"
  }
  add_files -norecurse $path
}

add_files -fileset sim_1 -norecurse [file join $verilog_dir vsrc tb_tapa_corestrip_xsim.sv]
set_property include_dirs [list $rtl_dir [file join $verilog_dir vsrc]] [get_filesets sim_1]
set_property top tb_tapa_corestrip_xsim [get_filesets sim_1]
set_property xsim.simulate.runtime all [get_filesets sim_1]
update_compile_order -fileset sources_1
update_compile_order -fileset sim_1

launch_simulation -simset sim_1 -mode behavioral
close_sim
close_project
