\* LP file written by HyperNova *\

Minimize
 OBJ:75 Arab_Light + 82 Bonny_Light + 62 Maya_Heavy + 79 Murban_Sweet + 64 Basrah_Heavy

Subject To
 Total_Throughput_Target: Arab_Light +  Bonny_Light +  Maya_Heavy +  Murban_Sweet +  Basrah_Heavy = 300
 API_Gravity_Min_BSVI:2.2 Arab_Light + 4.4 Bonny_Light - 9.2 Maya_Heavy + 9.2 Murban_Sweet - 7.5 Basrah_Heavy >= 0
 Sulfur_Max_Desulfurization_Cap:1.85 Arab_Light + 0.14 Bonny_Light + 3.4 Maya_Heavy + 0.78 Murban_Sweet + 2.95 Basrah_Heavy <= 540
 Viscosity_Min_Limit:1.2 Arab_Light + 0.1 Bonny_Light + 6.5 Maya_Heavy - 0.2 Murban_Sweet + 5.1 Basrah_Heavy >= 0

Bounds
 0 <= Arab_Light <= 120
 0 <= Bonny_Light <= 90
 0 <= Maya_Heavy <= 60
 0 <= Murban_Sweet <= 100
 0 <= Basrah_Heavy <= 70

Quadratic
 Arab_Light * Arab_Light 0.18
 Arab_Light * Bonny_Light -0.666
 Arab_Light * Maya_Heavy 1.29
 Arab_Light * Murban_Sweet -0.282
 Arab_Light * Basrah_Heavy 1.02
 Bonny_Light * Bonny_Light 0.61605
 Bonny_Light * Maya_Heavy -2.3865
 Bonny_Light * Murban_Sweet 0.5217
 Bonny_Light * Basrah_Heavy -1.887
 Maya_Heavy * Maya_Heavy 2.31125
 Maya_Heavy * Murban_Sweet -1.0105
 Maya_Heavy * Basrah_Heavy 3.655
 Murban_Sweet * Murban_Sweet 0.11045
 Murban_Sweet * Basrah_Heavy -0.799
 Basrah_Heavy * Basrah_Heavy 1.445

End
