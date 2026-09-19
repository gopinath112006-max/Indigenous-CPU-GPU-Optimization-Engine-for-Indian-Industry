NAME          mrpl_cogen_power_milp
ROWS
 N  OBJ
 E  Refinery_Power_Demand_110MW
 G  B1_Min_Load
 L  B1_Max_Load
 G  B2_Min_Load
 L  B2_Max_Load
 G  GTG_Min_Load
 L  GTG_Max_Load
 G  Min_HP_Steam_Header
COLUMNS
    Power_Boiler1  OBJ       45
    Power_Boiler1  Refinery_Power_Demand_110MW  1
    Power_Boiler1  B1_Min_Load  1
    Power_Boiler1  B1_Max_Load  1
    Power_Boiler1  Min_HP_Steam_Header  1
    Commit_Boiler1  OBJ       500
    Commit_Boiler1  B1_Min_Load  -15
    Commit_Boiler1  B1_Max_Load  -60
    Power_Boiler2  OBJ       40
    Power_Boiler2  Refinery_Power_Demand_110MW  1
    Power_Boiler2  B2_Min_Load  1
    Power_Boiler2  B2_Max_Load  1
    Power_Boiler2  Min_HP_Steam_Header  1
    Commit_Boiler2  OBJ       700
    Commit_Boiler2  B2_Min_Load  -20
    Commit_Boiler2  B2_Max_Load  -80
    Power_GTG  OBJ       38
    Power_GTG  Refinery_Power_Demand_110MW  1
    Power_GTG  GTG_Min_Load  1
    Power_GTG  GTG_Max_Load  1
    Commit_GTG  OBJ       400
    Commit_GTG  GTG_Min_Load  -10
    Commit_GTG  GTG_Max_Load  -40
    Power_Grid_Import  OBJ       65
    Power_Grid_Import  Refinery_Power_Demand_110MW  1
RHS
    RHS1      Refinery_Power_Demand_110MW  110
    RHS1      Min_HP_Steam_Header  70
BOUNDS
 UP BND1      Power_Boiler1  60
 BV BND1      Commit_Boiler1
 UP BND1      Power_Boiler2  80
 BV BND1      Commit_Boiler2
 UP BND1      Power_GTG  40
 BV BND1      Commit_GTG
 UP BND1      Power_Grid_Import  100
ENDATA
