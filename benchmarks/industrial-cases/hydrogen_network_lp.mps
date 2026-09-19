NAME          mrpl_hydrogen_network_lp
ROWS
 N  OBJ
 G  Hydrogen_Mass_Balance
 G  Min_DHDS_Demand
 G  Min_VGT_Demand
 L  PSA_Recovery_Limit
COLUMNS
    Stream_HGU_Reformer  OBJ       0.45
    Stream_HGU_Reformer  Hydrogen_Mass_Balance  1
    Stream_CCR_Offgas  OBJ       0.15
    Stream_CCR_Offgas  PSA_Recovery_Limit  -0.9
    Stream_PSA_Purge  Hydrogen_Mass_Balance  0.85
    Stream_PSA_Purge  PSA_Recovery_Limit  1
    Stream_DHDS_Unit  Hydrogen_Mass_Balance  -1
    Stream_DHDS_Unit  Min_DHDS_Demand  1
    Stream_VGO_Hydrotreater  Hydrogen_Mass_Balance  -1
    Stream_VGO_Hydrotreater  Min_VGT_Demand  1
RHS
    RHS1      Min_DHDS_Demand  80
    RHS1      Min_VGT_Demand  110
BOUNDS
 UP BND1      Stream_HGU_Reformer  200
 UP BND1      Stream_CCR_Offgas  80
 UP BND1      Stream_PSA_Purge  100
 UP BND1      Stream_DHDS_Unit  150
 UP BND1      Stream_VGO_Hydrotreater  200
ENDATA
