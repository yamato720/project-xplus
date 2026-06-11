# Pre-place floorplan hook for the CuperPcg SLR split experiment.
#
# Vitis sources this file through:
#   --linkhook.do_first vpl.impl.place_design,<this file>
#
# This hook is intentionally conservative by default. Earlier forced splits
# passed placement but failed routing because wide internal FIFOs crossed SLR
# boundaries and created localized SLL pressure. The default mode therefore only
# observes/report key CuperPcg groups and lets Vivado choose placement. The
# balanced modes split the 16 SpMV lanes across SLR0/SLR1 without binding the
# whole SpMV pipeline into one SLR.
#
# Do not hard-bind the full SpMV pipeline to one SLR: it uses 512 URAMs, more
# than the 320 URAM sites available in a single U55C SLR.

proc cuper_pcg_collect_cells {patterns} {
  set cells {}
  foreach pattern $patterns {
    set found [get_cells -quiet -hier -filter "NAME =~ $pattern"]
    foreach cell $found {
      lappend cells $cell
    }
  }
  return [lsort -unique $cells]
}

proc cuper_pcg_assign_slr {label patterns slr required} {
  set cells [cuper_pcg_collect_cells $patterns]
  set count [llength $cells]
  puts "INFO: \[cuper-pcg-slr\] $label -> $slr matched $count cell(s)"
  set sample_count 0
  foreach cell $cells {
    if {$sample_count >= 5} {
      break
    }
    puts "INFO: \[cuper-pcg-slr\]   $label sample cell: $cell"
    incr sample_count
  }

  if {$count == 0} {
    if {$required} {
      error "cuper-pcg-slr: required cell group '$label' was not found"
    }
    return
  }

  set pblock_name "pblock_${label}_${slr}"
  if {[llength [get_pblocks -quiet $pblock_name]] == 0} {
    create_pblock $pblock_name
  }

  resize_pblock [get_pblocks $pblock_name] -add $slr
  if {[catch {
    add_cells_to_pblock [get_pblocks $pblock_name] $cells -clear_locs
  } msg]} {
    puts "WARNING: \[cuper-pcg-slr\] clear_locs pblock add failed for $label: $msg"
    add_cells_to_pblock [get_pblocks $pblock_name] $cells
  }
  if {[catch {set_property USER_SLR_ASSIGNMENT $slr $cells} msg]} {
    puts "WARNING: \[cuper-pcg-slr\] USER_SLR_ASSIGNMENT $slr failed for $label: $msg"
  }
}

proc cuper_pcg_note_cells {label patterns required} {
  set cells [cuper_pcg_collect_cells $patterns]
  set count [llength $cells]
  puts "INFO: \[cuper-pcg-slr\] $label observed $count cell(s); no SLR pblock applied"
  set sample_count 0
  foreach cell $cells {
    if {$sample_count >= 5} {
      break
    }
    puts "INFO: \[cuper-pcg-slr\]   $label sample cell: $cell"
    incr sample_count
  }

  if {$count == 0 && $required} {
    error "cuper-pcg-slr: required cell group '$label' was not found"
  }
}

proc cuper_pcg_report_pblock {pblock_name report_name} {
  set pblocks [get_pblocks -quiet $pblock_name]
  if {[llength $pblocks] == 0} {
    return
  }
  if {[catch {report_utilization -pblocks $pblocks -file $report_name} msg]} {
    puts "WARNING: \[cuper-pcg-slr\] failed to write $report_name: $msg"
  }
}

proc cuper_pcg_safe_report {label command} {
  if {[catch {uplevel 1 $command} msg]} {
    puts "WARNING: \[cuper-pcg-slr\] $label failed: $msg"
  }
}

proc cuper_pcg_clear_experiment_constraints {} {
  set owned_pblock_patterns {
    pblock_update_z_compute_SLR1
    pblock_update_p_compute_SLR2
    pblock_controller_hbm_control_SLR0
    pblock_update_z_hbm_shells_SLR0
    pblock_update_p_hbm_shells_SLR0
    pblock_update_z_vector_service_SLR1
    pblock_update_p_vector_service_SLR1
    pblock_update_p_vector_service_SLR2
    pblock_spmv_pipeline_SLR0
    pblock_spmv_pipeline_SLR1
    pblock_spmv_pipeline_SLR2
    pblock_spmv_lanes_0_7_SLR0
    pblock_spmv_lanes_8_15_SLR1
  }
  foreach pblock_pattern $owned_pblock_patterns {
    set pblocks [get_pblocks -quiet $pblock_pattern]
    if {[llength $pblocks] > 0} {
      puts "INFO: \[cuper-pcg-slr\] deleting stale experiment pblock(s): $pblocks"
      delete_pblocks $pblocks
    }
  }

  set cells [cuper_pcg_collect_cells {
    */CuperPcg_1/inst/Pcg_Controller_0
    */CuperPcg_1/inst/Pcg_UpdateZ_Read_Service_0
    */CuperPcg_1/inst/Pcg_UpdateZ_Compute_Service_0
    */CuperPcg_1/inst/Pcg_UpdateZ_Write_Service_0
    */CuperPcg_1/inst/Pcg_UpdateP_Read_Service_0
    */CuperPcg_1/inst/Pcg_UpdateP_Compute_Service_0
    */CuperPcg_1/inst/Pcg_UpdateP_Write_Service_0
    */CuperPcg_1/inst/Pcg_SpElement_list_ptr_Loader_0
    */CuperPcg_1/inst/Pcg_Vector_Loader_0
    */CuperPcg_1/inst/Pcg_Matrix_Loader_*
    */CuperPcg_1/inst/Pcg_Core_*
    */CuperPcg_1/inst/Pcg_Accumulator_*
    */CuperPcg_1/inst/Pcg_Mult_Sort_Tree_0
    */CuperPcg_1/inst/Pcg_Vector_Checker_0
    */CuperPcg_1/inst/Pcg_Destroy_int_0
    */CuperPcg_1/inst/Pcg_Destroy_float_v16_0
    *Pcg_Controller_0
    *Pcg_UpdateZ_Read_Service_0
    *Pcg_UpdateZ_Compute_Service_0
    *Pcg_UpdateZ_Write_Service_0
    *Pcg_UpdateP_Read_Service_0
    *Pcg_UpdateP_Compute_Service_0
    *Pcg_UpdateP_Write_Service_0
    *Pcg_SpElement_list_ptr_Loader_0
    *Pcg_Vector_Loader_0
    *Pcg_Matrix_Loader_*
    *Pcg_Core_*
    *Pcg_Accumulator_*
    *Pcg_Mult_Sort_Tree_0
    *Pcg_Vector_Checker_0
    *Pcg_Destroy_int_0
    *Pcg_Destroy_float_v16_0
  }]
  if {[llength $cells] > 0} {
    if {[catch {reset_property USER_SLR_ASSIGNMENT $cells} msg]} {
      puts "WARNING: \[cuper-pcg-slr\] reset USER_SLR_ASSIGNMENT failed: $msg"
    } else {
      puts "INFO: \[cuper-pcg-slr\] cleared USER_SLR_ASSIGNMENT on [llength $cells] CuperPcg cell(s)"
    }
  }
}

set cuper_pcg_slr_mode "observe"
if {[info exists ::env(CUPER_PCG_SLR_MODE)]} {
  set cuper_pcg_slr_mode $::env(CUPER_PCG_SLR_MODE)
}

if {$cuper_pcg_slr_mode eq "chain"} {
  puts "INFO: \[cuper-pcg-slr\] mode=chain is deprecated and now aliases to observe; use chain_slr1 or chain_strict for forced split experiments"
  set cuper_pcg_slr_mode "observe"
}

puts "INFO: \[cuper-pcg-slr\] applying CuperPcg SLR split constraints before place_design, mode=$cuper_pcg_slr_mode"
cuper_pcg_clear_experiment_constraints

if {$cuper_pcg_slr_mode eq "legacy"} {
  cuper_pcg_assign_slr update_z_compute {
    */CuperPcg_1/inst/Pcg_UpdateZ_Compute_Service_0
    *Pcg_UpdateZ_Compute_Service_0
  } SLR1 1

  cuper_pcg_assign_slr update_p_compute {
    */CuperPcg_1/inst/Pcg_UpdateP_Compute_Service_0
    *Pcg_UpdateP_Compute_Service_0
  } SLR2 1

  cuper_pcg_assign_slr controller_hbm_control {
    */CuperPcg_1/inst/Pcg_Controller_0
    *Pcg_Controller_0
  } SLR0 0

  cuper_pcg_assign_slr update_z_hbm_shells {
    */CuperPcg_1/inst/Pcg_UpdateZ_Read_Service_0
    */CuperPcg_1/inst/Pcg_UpdateZ_Write_Service_0
    *Pcg_UpdateZ_Read_Service_0
    *Pcg_UpdateZ_Write_Service_0
  } SLR0 0

  cuper_pcg_assign_slr update_p_hbm_shells {
    */CuperPcg_1/inst/Pcg_UpdateP_Read_Service_0
    */CuperPcg_1/inst/Pcg_UpdateP_Write_Service_0
    *Pcg_UpdateP_Read_Service_0
    *Pcg_UpdateP_Write_Service_0
  } SLR0 0
} elseif {$cuper_pcg_slr_mode eq "chain_strict"} {
  cuper_pcg_assign_slr update_z_vector_service {
    */CuperPcg_1/inst/Pcg_UpdateZ_Read_Service_0
    */CuperPcg_1/inst/Pcg_UpdateZ_Compute_Service_0
    */CuperPcg_1/inst/Pcg_UpdateZ_Write_Service_0
    *Pcg_UpdateZ_Read_Service_0
    *Pcg_UpdateZ_Compute_Service_0
    *Pcg_UpdateZ_Write_Service_0
  } SLR1 1

  cuper_pcg_assign_slr update_p_vector_service {
    */CuperPcg_1/inst/Pcg_UpdateP_Read_Service_0
    */CuperPcg_1/inst/Pcg_UpdateP_Compute_Service_0
    */CuperPcg_1/inst/Pcg_UpdateP_Write_Service_0
    *Pcg_UpdateP_Read_Service_0
    *Pcg_UpdateP_Compute_Service_0
    *Pcg_UpdateP_Write_Service_0
  } SLR2 1

  cuper_pcg_note_cells controller_unconstrained {
    */CuperPcg_1/inst/Pcg_Controller_0
    *Pcg_Controller_0
  } 0
} elseif {$cuper_pcg_slr_mode eq "balanced" || $cuper_pcg_slr_mode eq "balanced_observe_updates"} {
  cuper_pcg_assign_slr spmv_lanes_0_7 {
    */CuperPcg_1/inst/Pcg_Matrix_Loader_0
    */CuperPcg_1/inst/Pcg_Matrix_Loader_1
    */CuperPcg_1/inst/Pcg_Matrix_Loader_2
    */CuperPcg_1/inst/Pcg_Matrix_Loader_3
    */CuperPcg_1/inst/Pcg_Matrix_Loader_4
    */CuperPcg_1/inst/Pcg_Matrix_Loader_5
    */CuperPcg_1/inst/Pcg_Matrix_Loader_6
    */CuperPcg_1/inst/Pcg_Matrix_Loader_7
    */CuperPcg_1/inst/Pcg_Core_0
    */CuperPcg_1/inst/Pcg_Core_1
    */CuperPcg_1/inst/Pcg_Core_2
    */CuperPcg_1/inst/Pcg_Core_3
    */CuperPcg_1/inst/Pcg_Core_4
    */CuperPcg_1/inst/Pcg_Core_5
    */CuperPcg_1/inst/Pcg_Core_6
    */CuperPcg_1/inst/Pcg_Core_7
    */CuperPcg_1/inst/Pcg_Accumulator_0
    */CuperPcg_1/inst/Pcg_Accumulator_1
    */CuperPcg_1/inst/Pcg_Accumulator_2
    */CuperPcg_1/inst/Pcg_Accumulator_3
    */CuperPcg_1/inst/Pcg_Accumulator_4
    */CuperPcg_1/inst/Pcg_Accumulator_5
    */CuperPcg_1/inst/Pcg_Accumulator_6
    */CuperPcg_1/inst/Pcg_Accumulator_7
    *Pcg_Matrix_Loader_0
    *Pcg_Matrix_Loader_1
    *Pcg_Matrix_Loader_2
    *Pcg_Matrix_Loader_3
    *Pcg_Matrix_Loader_4
    *Pcg_Matrix_Loader_5
    *Pcg_Matrix_Loader_6
    *Pcg_Matrix_Loader_7
    *Pcg_Core_0
    *Pcg_Core_1
    *Pcg_Core_2
    *Pcg_Core_3
    *Pcg_Core_4
    *Pcg_Core_5
    *Pcg_Core_6
    *Pcg_Core_7
    *Pcg_Accumulator_0
    *Pcg_Accumulator_1
    *Pcg_Accumulator_2
    *Pcg_Accumulator_3
    *Pcg_Accumulator_4
    *Pcg_Accumulator_5
    *Pcg_Accumulator_6
    *Pcg_Accumulator_7
  } SLR0 1

  cuper_pcg_assign_slr spmv_lanes_8_15 {
    */CuperPcg_1/inst/Pcg_Matrix_Loader_8
    */CuperPcg_1/inst/Pcg_Matrix_Loader_9
    */CuperPcg_1/inst/Pcg_Matrix_Loader_10
    */CuperPcg_1/inst/Pcg_Matrix_Loader_11
    */CuperPcg_1/inst/Pcg_Matrix_Loader_12
    */CuperPcg_1/inst/Pcg_Matrix_Loader_13
    */CuperPcg_1/inst/Pcg_Matrix_Loader_14
    */CuperPcg_1/inst/Pcg_Matrix_Loader_15
    */CuperPcg_1/inst/Pcg_Core_8
    */CuperPcg_1/inst/Pcg_Core_9
    */CuperPcg_1/inst/Pcg_Core_10
    */CuperPcg_1/inst/Pcg_Core_11
    */CuperPcg_1/inst/Pcg_Core_12
    */CuperPcg_1/inst/Pcg_Core_13
    */CuperPcg_1/inst/Pcg_Core_14
    */CuperPcg_1/inst/Pcg_Core_15
    */CuperPcg_1/inst/Pcg_Accumulator_8
    */CuperPcg_1/inst/Pcg_Accumulator_9
    */CuperPcg_1/inst/Pcg_Accumulator_10
    */CuperPcg_1/inst/Pcg_Accumulator_11
    */CuperPcg_1/inst/Pcg_Accumulator_12
    */CuperPcg_1/inst/Pcg_Accumulator_13
    */CuperPcg_1/inst/Pcg_Accumulator_14
    */CuperPcg_1/inst/Pcg_Accumulator_15
    *Pcg_Matrix_Loader_8
    *Pcg_Matrix_Loader_9
    *Pcg_Matrix_Loader_10
    *Pcg_Matrix_Loader_11
    *Pcg_Matrix_Loader_12
    *Pcg_Matrix_Loader_13
    *Pcg_Matrix_Loader_14
    *Pcg_Matrix_Loader_15
    *Pcg_Core_8
    *Pcg_Core_9
    *Pcg_Core_10
    *Pcg_Core_11
    *Pcg_Core_12
    *Pcg_Core_13
    *Pcg_Core_14
    *Pcg_Core_15
    *Pcg_Accumulator_8
    *Pcg_Accumulator_9
    *Pcg_Accumulator_10
    *Pcg_Accumulator_11
    *Pcg_Accumulator_12
    *Pcg_Accumulator_13
    *Pcg_Accumulator_14
    *Pcg_Accumulator_15
  } SLR1 1

  cuper_pcg_note_cells vector_services_unconstrained {
    */CuperPcg_1/inst/Pcg_UpdateZ_Read_Service_0
    */CuperPcg_1/inst/Pcg_UpdateZ_Compute_Service_0
    */CuperPcg_1/inst/Pcg_UpdateZ_Write_Service_0
    */CuperPcg_1/inst/Pcg_UpdateP_Read_Service_0
    */CuperPcg_1/inst/Pcg_UpdateP_Compute_Service_0
    */CuperPcg_1/inst/Pcg_UpdateP_Write_Service_0
    *Pcg_UpdateZ_Read_Service_0
    *Pcg_UpdateZ_Compute_Service_0
    *Pcg_UpdateZ_Write_Service_0
    *Pcg_UpdateP_Read_Service_0
    *Pcg_UpdateP_Compute_Service_0
    *Pcg_UpdateP_Write_Service_0
  } 0

  cuper_pcg_note_cells controller_unconstrained {
    */CuperPcg_1/inst/Pcg_Controller_0
    *Pcg_Controller_0
  } 0
} elseif {$cuper_pcg_slr_mode eq "observe"} {
  cuper_pcg_note_cells update_z_vector_service_unconstrained {
    */CuperPcg_1/inst/Pcg_UpdateZ_Read_Service_0
    */CuperPcg_1/inst/Pcg_UpdateZ_Compute_Service_0
    */CuperPcg_1/inst/Pcg_UpdateZ_Write_Service_0
    *Pcg_UpdateZ_Read_Service_0
    *Pcg_UpdateZ_Compute_Service_0
    *Pcg_UpdateZ_Write_Service_0
  } 0

  cuper_pcg_note_cells update_p_vector_service_unconstrained {
    */CuperPcg_1/inst/Pcg_UpdateP_Read_Service_0
    */CuperPcg_1/inst/Pcg_UpdateP_Compute_Service_0
    */CuperPcg_1/inst/Pcg_UpdateP_Write_Service_0
    *Pcg_UpdateP_Read_Service_0
    *Pcg_UpdateP_Compute_Service_0
    *Pcg_UpdateP_Write_Service_0
  } 0

  cuper_pcg_note_cells controller_unconstrained {
    */CuperPcg_1/inst/Pcg_Controller_0
    *Pcg_Controller_0
  } 0
} elseif {$cuper_pcg_slr_mode eq "chain_slr1"} {
  cuper_pcg_assign_slr update_z_vector_service {
    */CuperPcg_1/inst/Pcg_UpdateZ_Read_Service_0
    */CuperPcg_1/inst/Pcg_UpdateZ_Compute_Service_0
    */CuperPcg_1/inst/Pcg_UpdateZ_Write_Service_0
    *Pcg_UpdateZ_Read_Service_0
    *Pcg_UpdateZ_Compute_Service_0
    *Pcg_UpdateZ_Write_Service_0
  } SLR1 1

  cuper_pcg_assign_slr update_p_vector_service {
    */CuperPcg_1/inst/Pcg_UpdateP_Read_Service_0
    */CuperPcg_1/inst/Pcg_UpdateP_Compute_Service_0
    */CuperPcg_1/inst/Pcg_UpdateP_Write_Service_0
    *Pcg_UpdateP_Read_Service_0
    *Pcg_UpdateP_Compute_Service_0
    *Pcg_UpdateP_Write_Service_0
  } SLR1 1

  cuper_pcg_note_cells controller_unconstrained {
    */CuperPcg_1/inst/Pcg_Controller_0
    *Pcg_Controller_0
  } 0
} else {
  error "cuper-pcg-slr: unknown CUPER_PCG_SLR_MODE '$cuper_pcg_slr_mode' (expected observe, balanced, balanced_observe_updates, chain_slr1, chain, chain_strict, or legacy)"
}

cuper_pcg_note_cells spmv_pipeline_unconstrained {
  */CuperPcg_1/inst/Pcg_SpElement_list_ptr_Loader_0
  */CuperPcg_1/inst/Pcg_Vector_Loader_0
  */CuperPcg_1/inst/Pcg_Matrix_Loader_*
  */CuperPcg_1/inst/Pcg_Core_*
  */CuperPcg_1/inst/Pcg_Accumulator_*
  */CuperPcg_1/inst/Pcg_Mult_Sort_Tree_0
  */CuperPcg_1/inst/Pcg_Vector_Checker_0
  */CuperPcg_1/inst/Pcg_Destroy_int_0
  */CuperPcg_1/inst/Pcg_Destroy_float_v16_0
  *Pcg_SpElement_list_ptr_Loader_0
  *Pcg_Vector_Loader_0
  *Pcg_Matrix_Loader_*
  *Pcg_Core_*
  *Pcg_Accumulator_*
  *Pcg_Mult_Sort_Tree_0
  *Pcg_Vector_Checker_0
  *Pcg_Destroy_int_0
  *Pcg_Destroy_float_v16_0
} 0

cuper_pcg_report_pblock pblock_update_z_compute_SLR1 cuper_pcg_slr_update_z_compute_util.rpt
cuper_pcg_report_pblock pblock_update_p_compute_SLR2 cuper_pcg_slr_update_p_compute_util.rpt
cuper_pcg_report_pblock pblock_update_z_vector_service_SLR1 cuper_pcg_slr_update_z_vector_service_util.rpt
cuper_pcg_report_pblock pblock_update_p_vector_service_SLR1 cuper_pcg_slr_update_p_vector_service_slr1_util.rpt
cuper_pcg_report_pblock pblock_update_p_vector_service_SLR2 cuper_pcg_slr_update_p_vector_service_util.rpt
cuper_pcg_report_pblock pblock_spmv_lanes_0_7_SLR0 cuper_pcg_slr_spmv_lanes_0_7_util.rpt
cuper_pcg_report_pblock pblock_spmv_lanes_8_15_SLR1 cuper_pcg_slr_spmv_lanes_8_15_util.rpt
cuper_pcg_safe_report high_fanout_pre_place {
  report_high_fanout_nets -fanout_greater_than 100 -max_nets 80 -file cuper_pcg_slr_high_fanout_pre_place.rpt
}

set cuper_pcg_report_all_pblocks 0
if {[info exists ::env(CUPER_PCG_SLR_REPORT_ALL_PBLOCKS)]} {
  set cuper_pcg_report_all_pblocks $::env(CUPER_PCG_SLR_REPORT_ALL_PBLOCKS)
}
if {$cuper_pcg_report_all_pblocks} {
  cuper_pcg_safe_report pblocks_pre_place {
    set report_files {}
    foreach pblock [get_property NAME [get_pblocks -quiet pblock_*]] {
      set report_file "cuper_pcg_slr_${pblock}_pre_place_util.rpt"
      report_utilization -pblocks $pblock -file $report_file
      lappend report_files $report_file
    }
    puts "INFO: \[cuper-pcg-slr\] wrote pblock utilization reports: $report_files"
  }
}

puts "INFO: \[cuper-pcg-slr\] SLR split constraints applied"
