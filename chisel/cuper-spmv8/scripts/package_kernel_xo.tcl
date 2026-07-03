set build_dir [file normalize [lindex $argv 0]]
set rtl_file [file normalize [lindex $argv 1]]
set xo_path [file normalize [lindex $argv 2]]
set kernel_xml [file normalize [lindex $argv 3]]
set part_name [lindex $argv 4]

set kernel_name CuperSpmvChisel8
set project_dir [file join $build_dir vivado_package]
set ip_dir [file join $build_dir ip_repo projectx_rtl_CuperSpmvChisel8_1_0]

file mkdir $build_dir
file delete -force $project_dir
file delete -force $ip_dir

create_project -force package_cuper_spmv_chisel8 $project_dir -part $part_name
add_files -norecurse $rtl_file
set_property top $kernel_name [current_fileset]
update_compile_order -fileset sources_1

ipx::package_project -force -root_dir $ip_dir -vendor projectx -library rtl -name $kernel_name -version 1.0 -taxonomy /KernelIP -import_files -set_current true
set core [ipx::current_core]
set_property name $kernel_name $core
set_property display_name $kernel_name $core
set_property description {Project-XPlus standalone 8-HBM Chisel SpMV RTL entry-probe kernel} $core
set_property vendor_display_name {Project-XPlus} $core
set_property company_url {https://github.com} $core
set_property sdx_kernel true $core
set_property sdx_kernel_type rtl $core
set_property vitis_drc {ctrl_protocol ap_ctrl_hs} $core
set_property auto_family_support_level level_2 $core

ipx::infer_bus_interfaces $core

foreach bus [ipx::get_bus_interfaces -of_objects $core] {
  set bus_name [get_property NAME $bus]
  if {$bus_name eq "s_axi_control"} {
    set_property interface_mode slave $bus
  }
  if {[string match "m_axi_*" $bus_name]} {
    set_property interface_mode master $bus
  }
}

ipx::associate_bus_interfaces -busif s_axi_control -clock ap_clk $core
foreach bus [ipx::get_bus_interfaces -of_objects $core] {
  set bus_name [get_property NAME $bus]
  if {[string match "m_axi_*" $bus_name]} {
    ipx::associate_bus_interfaces -busif $bus_name -clock ap_clk $core
  }
}

ipx::save_core $core
close_project

file delete -force $xo_path
package_xo -force -xo_path $xo_path -kernel_name $kernel_name -ip_directory $ip_dir -kernel_xml $kernel_xml -ctrl_protocol ap_ctrl_hs
