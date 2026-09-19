NAME          mrpl_refinery_planning_milp
OBJSENSE
    MAX
ROWS
 N  OBJ
 E  Exclusive_Mode_M1
 L  Diesel_Yield_Limit_M1
 L  ATF_Yield_Limit_M1
 G  Min_Gas_Demand_M1
 G  Min_Die_Demand_M1
 G  Min_ATF_Demand_M1
 E  Exclusive_Mode_M2
 L  Diesel_Yield_Limit_M2
 L  ATF_Yield_Limit_M2
 G  Min_Gas_Demand_M2
 G  Min_Die_Demand_M2
 G  Min_ATF_Demand_M2
 E  Exclusive_Mode_M3
 L  Diesel_Yield_Limit_M3
 L  ATF_Yield_Limit_M3
 G  Min_Gas_Demand_M3
 G  Min_Die_Demand_M3
 G  Min_ATF_Demand_M3
COLUMNS
    Gasoline_M1  OBJ       820
    Gasoline_M1  Min_Gas_Demand_M1  1
    Diesel_M1  OBJ       870
    Diesel_M1  Diesel_Yield_Limit_M1  1
    Diesel_M1  Min_Die_Demand_M1  1
    ATF_Jet_M1  OBJ       920
    ATF_Jet_M1  ATF_Yield_Limit_M1  1
    ATF_Jet_M1  Min_ATF_Demand_M1  1
    Mode_Max_Diesel_M1  OBJ       -4500
    Mode_Max_Diesel_M1  Exclusive_Mode_M1  1
    Mode_Max_Diesel_M1  Diesel_Yield_Limit_M1  -450
    Mode_Max_Diesel_M1  ATF_Yield_Limit_M1  -100
    Mode_Max_ATF_M1  OBJ       -6200
    Mode_Max_ATF_M1  Exclusive_Mode_M1  1
    Mode_Max_ATF_M1  Diesel_Yield_Limit_M1  -250
    Mode_Max_ATF_M1  ATF_Yield_Limit_M1  -250
    Gasoline_M2  OBJ       820
    Gasoline_M2  Min_Gas_Demand_M2  1
    Diesel_M2  OBJ       870
    Diesel_M2  Diesel_Yield_Limit_M2  1
    Diesel_M2  Min_Die_Demand_M2  1
    ATF_Jet_M2  OBJ       920
    ATF_Jet_M2  ATF_Yield_Limit_M2  1
    ATF_Jet_M2  Min_ATF_Demand_M2  1
    Mode_Max_Diesel_M2  OBJ       -4500
    Mode_Max_Diesel_M2  Exclusive_Mode_M2  1
    Mode_Max_Diesel_M2  Diesel_Yield_Limit_M2  -450
    Mode_Max_Diesel_M2  ATF_Yield_Limit_M2  -100
    Mode_Max_ATF_M2  OBJ       -6200
    Mode_Max_ATF_M2  Exclusive_Mode_M2  1
    Mode_Max_ATF_M2  Diesel_Yield_Limit_M2  -250
    Mode_Max_ATF_M2  ATF_Yield_Limit_M2  -250
    Gasoline_M3  OBJ       820
    Gasoline_M3  Min_Gas_Demand_M3  1
    Diesel_M3  OBJ       870
    Diesel_M3  Diesel_Yield_Limit_M3  1
    Diesel_M3  Min_Die_Demand_M3  1
    ATF_Jet_M3  OBJ       920
    ATF_Jet_M3  ATF_Yield_Limit_M3  1
    ATF_Jet_M3  Min_ATF_Demand_M3  1
    Mode_Max_Diesel_M3  OBJ       -4500
    Mode_Max_Diesel_M3  Exclusive_Mode_M3  1
    Mode_Max_Diesel_M3  Diesel_Yield_Limit_M3  -450
    Mode_Max_Diesel_M3  ATF_Yield_Limit_M3  -100
    Mode_Max_ATF_M3  OBJ       -6200
    Mode_Max_ATF_M3  Exclusive_Mode_M3  1
    Mode_Max_ATF_M3  Diesel_Yield_Limit_M3  -250
    Mode_Max_ATF_M3  ATF_Yield_Limit_M3  -250
RHS
    RHS1      Exclusive_Mode_M1  1
    RHS1      Min_Gas_Demand_M1  100
    RHS1      Min_Die_Demand_M1  200
    RHS1      Min_ATF_Demand_M1  80
    RHS1      Exclusive_Mode_M2  1
    RHS1      Min_Gas_Demand_M2  120
    RHS1      Min_Die_Demand_M2  220
    RHS1      Min_ATF_Demand_M2  120
    RHS1      Exclusive_Mode_M3  1
    RHS1      Min_Gas_Demand_M3  110
    RHS1      Min_Die_Demand_M3  210
    RHS1      Min_ATF_Demand_M3  90
BOUNDS
 UP BND1      Gasoline_M1  300
 UP BND1      Diesel_M1  450
 UP BND1      ATF_Jet_M1  250
 BV BND1      Mode_Max_Diesel_M1
 BV BND1      Mode_Max_ATF_M1
 UP BND1      Gasoline_M2  300
 UP BND1      Diesel_M2  450
 UP BND1      ATF_Jet_M2  250
 BV BND1      Mode_Max_Diesel_M2
 BV BND1      Mode_Max_ATF_M2
 UP BND1      Gasoline_M3  300
 UP BND1      Diesel_M3  450
 UP BND1      ATF_Jet_M3  250
 BV BND1      Mode_Max_Diesel_M3
 BV BND1      Mode_Max_ATF_M3
ENDATA
