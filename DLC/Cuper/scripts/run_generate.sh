tapac \
  --work-dir run \
  --top Cuper \
  --part-num xcu280-fsvh2892-2L-e \
  --platform xilinx_u280_xdma_201920_3 \
  --clock-period 3.33 \
  -o Cuper.xo \
  --constraint Cuper_floorplan.tcl \
  --connectivity ../link_config_16.ini \
  --read-only-args SpElement_list_ptr \
  --read-only-args Matrix_data* \
  --read-only-args X \
  --write-only-args Y_out \
  --enable-synth-util \
  --max-parallel-synth-jobs 16 \
  --enable-hbm-binding-adjustment \
  --run-floorplan-dse \
  --min-area-limit 0.6 \
  --min-slr-width-limit 5000 \
  ../src/Cuper.cpp \
   2>&1 | tee tapa.log
