\* MIQP convex portfolio: min (x0-2)^2 + (x1-2)^2 + (x2-1)^2 - 9            *\
\* x0,x1 integer in [0,2]; x2 continuous in [0,2]; x0+x1+x2 <= 3.5          *\
\* Optimum (2,1,0.5) or (1,2,0.5): model objective x0^2+x1^2+x2^2           *\
\* -4x0-4x1-2x2 = -7.75 (constant offsets folded out).                      *\
minimize
-4 x0 -4 x1 -2 x2
subject to
cap: x0 + x1 + x2 <= 3.5
Bounds
0 <= x0 <= 2
0 <= x1 <= 2
0 <= x2 <= 2
General
x0 x1
Quadratic
x0 * x0 2
x1 * x1 2
x2 * x2 2
End