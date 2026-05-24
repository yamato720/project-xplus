# Export optional Vivado analysis reports from an existing project/run or DCP.
#
# Usage:
#   vivado -mode batch -source scripts/export_vivado_analysis.tcl \
#     -tclargs <vpl-dir|project.xpr|checkpoint.dcp> <output-dir> [run] [reports]
#
# reports can be "all", "power", or a comma/space separated subset:
#   power utilization timing methodology drc route_status clock_utilization
#   clock_interaction cdc design_analysis qor_suggestions high_fanout
#   control_sets ram_utilization

proc usage {} {
  puts "usage: export_vivado_analysis.tcl <vpl-dir|project.xpr|checkpoint.dcp> <output-dir> \[run\] \[reports\]"
}

if {[llength $argv] < 2} {
  usage
  exit 2
}

set input_path [file normalize [lindex $argv 0]]
set output_dir [file normalize [lindex $argv 1]]
set run_name "impl_1"
set report_raw "all"
if {[llength $argv] >= 3 && [string trim [lindex $argv 2]] ne ""} {
  set run_name [lindex $argv 2]
}
if {[llength $argv] >= 4 && [string trim [lindex $argv 3]] ne ""} {
  set report_raw [lindex $argv 3]
}

file mkdir $output_dir
set manifest_path [file join $output_dir "analysis_manifest.txt"]
set manifest [open $manifest_path "w"]

proc log_line {msg} {
  global manifest
  puts $msg
  puts $manifest $msg
  flush $manifest
}

proc find_project_file {input_path} {
  if {[file isfile $input_path] && [file extension $input_path] eq ".xpr"} {
    return $input_path
  }
  if {[file isdirectory $input_path]} {
    set candidates {}
    foreach path [list \
      [file join $input_path "prj" "prj.xpr"] \
      [file join $input_path "prj.xpr"] \
    ] {
      if {[file isfile $path]} {
        lappend candidates [file normalize $path]
      }
    }
    foreach path [glob -nocomplain -directory $input_path -type f "*.xpr"] {
      lappend candidates [file normalize $path]
    }
    set candidates [lsort -unique $candidates]
    if {[llength $candidates] > 0} {
      return [lindex $candidates 0]
    }
  }
  return ""
}

proc parse_report_list {raw} {
  set raw [string trim $raw]
  set all_reports {
    power
    utilization
    timing
    methodology
    drc
    route_status
    clock_utilization
    clock_interaction
    cdc
    design_analysis
    qor_suggestions
    high_fanout
    control_sets
    ram_utilization
  }
  if {$raw eq "" || $raw eq "all"} {
    return $all_reports
  }
  set normalized [string map {"," " "} $raw]
  set reports {}
  foreach item $normalized {
    set item [string trim $item]
    if {$item ne ""} {
      lappend reports $item
    }
  }
  return $reports
}

proc wants_report {reports key} {
  return [expr {[lsearch -exact $reports $key] >= 0}]
}

proc try_report {key description command} {
  log_line ""
  log_line "== $key: $description"
  if {[catch {uplevel 1 $command} result options]} {
    log_line "FAIL $key: $result"
    return 0
  }
  log_line "OK $key"
  return 1
}

log_line "input: $input_path"
log_line "output: $output_dir"
log_line "run: $run_name"
log_line "reports: $report_raw"

set opened_design 0
set project_file ""

if {[file isfile $input_path] && [file extension $input_path] eq ".dcp"} {
  log_line "opening checkpoint: $input_path"
  if {[catch {open_checkpoint $input_path} err]} {
    log_line "ERROR: failed to open checkpoint: $err"
    close $manifest
    exit 3
  }
  set opened_design 1
} else {
  set project_file [find_project_file $input_path]
  if {$project_file eq ""} {
    log_line "ERROR: Vivado project not found under: $input_path"
    close $manifest
    exit 3
  }
  log_line "opening project: $project_file"
  if {[catch {open_project $project_file} err]} {
    log_line "ERROR: failed to open project: $err"
    close $manifest
    exit 3
  }

  set candidate_runs {}
  foreach candidate [list $run_name "impl_1" "my_rm_impl_1"] {
    if {$candidate eq ""} {
      continue
    }
    if {[llength [get_runs -quiet $candidate]] > 0} {
      lappend candidate_runs $candidate
    }
  }
  set candidate_runs [lsort -unique $candidate_runs]
  foreach candidate $candidate_runs {
    log_line "trying open_run: $candidate"
    if {[catch {open_run $candidate} err]} {
      log_line "open_run failed for $candidate: $err"
      continue
    }
    set opened_design 1
    set run_name $candidate
    break
  }
}

if {!$opened_design} {
  log_line "ERROR: no implemented design is open."
  log_line "The current build may only contain the xclbin/project shell, not the completed Vivado run directory."
  log_line "Rebuild hardware with 'make build-hw' so archived/cfg/vivado_analysis_reports.cfg is applied, or preserve the Vivado impl run."
  close $manifest
  exit 3
}

set selected_reports [parse_report_list $report_raw]
log_line "selected reports: $selected_reports"

if {[wants_report $selected_reports "power"]} {
  try_report "power" "static/dynamic power estimate from current implemented design" {
    report_power -file [file join $output_dir "power.rpt"]
  }
}
if {[wants_report $selected_reports "utilization"]} {
  try_report "utilization" "hierarchical resource utilization" {
    report_utilization -hierarchical -hierarchical_depth 4 -file [file join $output_dir "utilization_hierarchical.rpt"]
  }
  try_report "utilization_flat" "flat resource utilization" {
    report_utilization -file [file join $output_dir "utilization.rpt"]
  }
}
if {[wants_report $selected_reports "timing"]} {
  try_report "timing" "timing summary with unconstrained path check" {
    report_timing_summary -max_paths 10 -report_unconstrained -file [file join $output_dir "timing_summary.rpt"]
  }
}
if {[wants_report $selected_reports "methodology"]} {
  try_report "methodology" "Vivado methodology checks" {
    report_methodology -file [file join $output_dir "methodology.rpt"]
  }
}
if {[wants_report $selected_reports "drc"]} {
  try_report "drc" "design rule checks" {
    report_drc -file [file join $output_dir "drc.rpt"]
  }
}
if {[wants_report $selected_reports "route_status"]} {
  try_report "route_status" "route completion/status report" {
    report_route_status -file [file join $output_dir "route_status.rpt"]
  }
}
if {[wants_report $selected_reports "clock_utilization"]} {
  try_report "clock_utilization" "clocking resource utilization" {
    report_clock_utilization -file [file join $output_dir "clock_utilization.rpt"]
  }
}
if {[wants_report $selected_reports "clock_interaction"]} {
  try_report "clock_interaction" "clock-domain interaction summary" {
    report_clock_interaction -file [file join $output_dir "clock_interaction.rpt"]
  }
}
if {[wants_report $selected_reports "cdc"]} {
  try_report "cdc" "clock-domain crossing checks when supported by the current design/tool flow" {
    report_cdc -file [file join $output_dir "cdc.rpt"]
  }
}
if {[wants_report $selected_reports "design_analysis"]} {
  try_report "design_analysis" "general design analysis report" {
    report_design_analysis -file [file join $output_dir "design_analysis.rpt"]
  }
}
if {[wants_report $selected_reports "qor_suggestions"]} {
  try_report "qor_suggestions" "QoR suggestions from Vivado" {
    report_qor_suggestions -file [file join $output_dir "qor_suggestions.rpt"]
  }
}
if {[wants_report $selected_reports "high_fanout"]} {
  try_report "high_fanout" "high fanout nets" {
    report_high_fanout_nets -file [file join $output_dir "high_fanout_nets.rpt"]
  }
}
if {[wants_report $selected_reports "control_sets"]} {
  try_report "control_sets" "control set distribution" {
    report_control_sets -verbose -file [file join $output_dir "control_sets.rpt"]
  }
}
if {[wants_report $selected_reports "ram_utilization"]} {
  try_report "ram_utilization" "RAM utilization details" {
    report_ram_utilization -file [file join $output_dir "ram_utilization.rpt"]
  }
}

log_line ""
log_line "generated files:"
foreach path [lsort [glob -nocomplain -directory $output_dir *]] {
  log_line "  $path"
}
log_line "manifest: $manifest_path"
close $manifest
exit 0
