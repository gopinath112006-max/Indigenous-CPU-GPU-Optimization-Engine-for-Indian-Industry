NAME          mrpl_logistics_freight_milp
ROWS
 N  OBJ
 G  Pipe_Hsn_Min_Batch
 L  Pipe_Hsn_Max_Cap
 G  Pipe_Blr_Min_Batch
 L  Pipe_Blr_Max_Cap
 G  Rail_Hyd_Min_Rake
 L  Rail_Hyd_Max_Cap
 G  Demand_Hassan
 G  Demand_Bengaluru
 G  Demand_Goa
 G  Demand_Kochi
 G  Demand_Hyderabad
COLUMNS
    Vol_Pipe_Hassan  OBJ       5
    Vol_Pipe_Hassan  Pipe_Hsn_Min_Batch  1
    Vol_Pipe_Hassan  Pipe_Hsn_Max_Cap  1
    Vol_Pipe_Hassan  Demand_Hassan  1
    Trigger_Pipe_Hassan  OBJ       500
    Trigger_Pipe_Hassan  Pipe_Hsn_Min_Batch  -50
    Trigger_Pipe_Hassan  Pipe_Hsn_Max_Cap  -300
    Vol_Pipe_Bengaluru  OBJ       8
    Vol_Pipe_Bengaluru  Pipe_Blr_Min_Batch  1
    Vol_Pipe_Bengaluru  Pipe_Blr_Max_Cap  1
    Vol_Pipe_Bengaluru  Demand_Bengaluru  1
    Trigger_Pipe_Bengaluru  OBJ       800
    Trigger_Pipe_Bengaluru  Pipe_Blr_Min_Batch  -80
    Trigger_Pipe_Bengaluru  Pipe_Blr_Max_Cap  -400
    Vol_Tanker_Goa  OBJ       7
    Vol_Tanker_Goa  Demand_Goa  1
    Vol_Tanker_Kochi  OBJ       9
    Vol_Tanker_Kochi  Demand_Kochi  1
    Vol_Rail_Hyderabad  OBJ       16
    Vol_Rail_Hyderabad  Rail_Hyd_Min_Rake  1
    Vol_Rail_Hyderabad  Rail_Hyd_Max_Cap  1
    Vol_Rail_Hyderabad  Demand_Hyderabad  1
    Trigger_Rail_Hyderabad  OBJ       300
    Trigger_Rail_Hyderabad  Rail_Hyd_Min_Rake  -40
    Trigger_Rail_Hyderabad  Rail_Hyd_Max_Cap  -180
RHS
    RHS1      Demand_Hassan  120
    RHS1      Demand_Bengaluru  250
    RHS1      Demand_Goa  90
    RHS1      Demand_Kochi  140
    RHS1      Demand_Hyderabad  100
BOUNDS
 UP BND1      Vol_Pipe_Hassan  300
 BV BND1      Trigger_Pipe_Hassan
 UP BND1      Vol_Pipe_Bengaluru  400
 BV BND1      Trigger_Pipe_Bengaluru
 UP BND1      Vol_Tanker_Goa  150
 UP BND1      Vol_Tanker_Kochi  200
 UP BND1      Vol_Rail_Hyderabad  180
 BV BND1      Trigger_Rail_Hyderabad
ENDATA
