\ QPLIB-style inline-quadratic LP regression fixture.
\ 3 binary variables, a single (redundant) linear constraint, convex MIQP
\ whose unique optimum is 0 at the origin.
Minimize
 obj: 1 b1 + 1 b2 + 1 b3 + [ b1^2 + b2^2 + b3^2 ]
Subject To
 c1: b1 + b2 + b3 <= 3
Binary
 b1 b2 b3
End
