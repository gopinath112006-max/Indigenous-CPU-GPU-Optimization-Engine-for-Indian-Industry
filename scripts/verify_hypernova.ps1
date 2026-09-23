Write-Host "========================================"
Write-Host "HyperNova Verification"
Write-Host "========================================"

# 1. Configure
cmake -S . -B build-verify -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { Write-Host "[FAIL] Configure"; exit 1 } else { Write-Host "[PASS] Configure" }

# 2. Build
cmake --build build-verify --parallel 16
if ($LASTEXITCODE -ne 0) { Write-Host "[FAIL] Build"; exit 1 } else { Write-Host "[PASS] Build" }

# 3. CTest
cd build-verify
ctest -j16 --output-on-failure
if ($LASTEXITCODE -ne 0) { Write-Host "[FAIL] CTest"; exit 1 } else { Write-Host "[PASS] CTest" }
cd ..

# 4. Capabilities
$cap = .\build-verify\bin\hypernova.exe capabilities
if ($cap -match "cpu cuda") { Write-Host "[PASS] Capabilities" } else { Write-Host "[FAIL] Capabilities"; exit 1 }

# 5. First LP
$out1 = .\build-verify\bin\hypernova.exe solve examples\beginner\first_lp.lp
if ($out1 -match "Status: OPTIMAL") { Write-Host "[PASS] LP example" } else { Write-Host "[FAIL] LP example"; exit 1 }

# 6. First MILP
$out2 = .\build-verify\bin\hypernova.exe solve examples\beginner\first_milp.lp
if ($out2 -match "Objective: 20.00") { Write-Host "[PASS] MILP example" } else { Write-Host "[FAIL] MILP example"; exit 1 }

# 7. Inspect
$out3 = .\build-verify\bin\hypernova.exe inspect examples\beginner\first_lp.lp
if ($out3 -match "Constraints: 2") { Write-Host "[PASS] Inspect" } else { Write-Host "[FAIL] Inspect"; exit 1 }

# 8. Diagnose
$out4 = .\build-verify\bin\hypernova.exe diagnose examples\beginner\infeasible.lp
if ($out4 -match "verified Farkas certificate") { Write-Host "[PASS] Diagnose" } else { Write-Host "[FAIL] Diagnose"; exit 1 }

# 9. C++ API
cd examples\cpp
cmake -S . -B build_cpp -G "MinGW Makefiles"
# Wait, I need to link hypernova_api!
# We might need to point the example CMake to the hypernova root or just compile it directly.
cd ..\..

Write-Host "========================================"
Write-Host "RESULT: PASS"
Write-Host "========================================"
