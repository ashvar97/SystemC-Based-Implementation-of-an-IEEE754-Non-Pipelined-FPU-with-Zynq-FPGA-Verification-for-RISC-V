# ======================
# Multiplication Project Setup
# ======================
# Create project
create_project floating_mult ./floating_mult -part xc7z020clg400-1

# Add design files
add_files FloatingMultiplication.sv
add_files -fileset sim_1 tb.sv

# Set top module
set_property top tb [get_filesets sim_1]

# Simulation settings
set_property runtime {200ns} [get_filesets sim_1]
set_property xsim.simulate.log_all_signals true [get_filesets sim_1]

# Run simulation
launch_simulation -mode behavioral
run all


# ======================
# Division Project Setup
# ======================
# Clean previous runs
close_project -quiet
file delete -force ./floating_div

# Create project
create_project floating_div ./floating_div -part xc7z020clg400-1

# Add design files
add_files TopModule.sv
add_files -fileset sim_1 tb.sv

# Set top module
set_property top tb [get_filesets sim_1]

# Run simulation
launch_simulation -mode behavioral
run all


# ======================
# Subtraction Project Setup
# ======================
# Create project
create_project floating_sub ./floating_sub -part xc7z020clg400-1

# Add design files
add_files FloatingSubtractor.sv
add_files -fileset sim_1 tb_subtractor.sv

# Set top module
set_property top tb_subtractor [get_filesets sim_1]

# Simulation settings
set_property runtime {200ns} [get_filesets sim_1]
set_property xsim.simulate.log_all_signals true [get_filesets sim_1]

# Run simulation
launch_simulation -mode behavioral
run all


# ======================
# Addition Project Setup
# ======================
# Create project
create_project floating_add ./floating_add -part xc7z020clg400-1

# Add design files
add_files FloatingAddition.sv
add_files -fileset sim_1 tb_adder.sv

# Set top module
set_property top tb_adder [get_filesets sim_1]

# Simulation settings
set_property runtime {200ns} [get_filesets sim_1]
set_property xsim.simulate.log_all_signals true [get_filesets sim_1]

# Run simulation
launch_simulation -mode behavioral
run all
