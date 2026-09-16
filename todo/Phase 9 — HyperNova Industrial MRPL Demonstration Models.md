Continue from the existing HyperNova repository.

Goal:
Create representative industrial optimization models demonstrating why HyperNova is useful for MRPL and Indian industry.

Do not use confidential MRPL data.
Use publicly available data, synthetic data, or values derived from open literature.

Build at minimum:

1. Crude blending optimization.
2. Refinery production planning.
3. Transportation/logistics optimization.

Where practical, add:
4. Refinery scheduling.
5. Power/energy dispatch.

For each model define:

- variables
- objective
- constraints
- bounds
- integer decisions where required
- input data
- output interpretation

Crude blending example should model:
- crude availability
- crude cost
- quality properties
- blend proportions
- refinery capacity
- product requirements
- quality constraints

Production planning should model:
- production capacities
- raw material availability
- product demand
- operating costs
- revenue/profit

Logistics should model:
- sources
- destinations
- quantities
- transportation costs
- capacity constraints

Implement the models using HyperNova's own model/API layer.

Do not hard-code a known optimal answer.

Acceptance criteria:
- Models are solved by HyperNova.
- Results pass independent verification.
- Input and output are understandable to non-experts.
- Each model includes documentation explaining the industrial meaning.
- At least one model contains integer decisions and therefore exercises MILP.
- Results include objective value, runtime, solver type and verification status.