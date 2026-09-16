\* MIQP: min 0.5*x0^2 - 2*x0 + x1^2, x0 continuous in [0,4], x1 integer     *\
\* in [0,2], x0 + x1 = 3. Hard case: the integer variable is pinned at a     *\
\* branch bound while the equality couples both variables.                   *\
\* Optimum (3,0): model objective 0.5*9 - 6 + 0 = -1.5.                      *\
minimize
-2 x0
subject to
eq: x0 + x1 = 3
Bounds
0 <= x0 <= 4
0 <= x1 <= 2
General
x1
Quadratic
x0 * x0 1
x1 * x1 2
End