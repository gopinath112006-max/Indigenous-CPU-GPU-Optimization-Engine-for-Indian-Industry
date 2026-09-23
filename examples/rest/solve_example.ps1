$body = @{
    model_content = "Maximize`n obj: 2 x1 + 3 x2`nSubject To`n c1: x1 + 2 x2 <= 10`nBounds`n 0 <= x1 <= 10`n 0 <= x2 <= 10`nGenerals`n x1`n x2`nEnd"
    format = "lp"
    engine = "auto"
} | ConvertTo-Json

$response = Invoke-RestMethod -Uri "http://localhost:8080/api/v1/solve" -Method Post -Body $body -ContentType "application/json"

if ($response.status -ne "OPTIMAL") {
    Write-Host "[FAIL] Unexpected status: $($response.status)"
    exit 1
}
if ($response.objective_value -ne 20.0) {
    Write-Host "[FAIL] Unexpected objective: $($response.objective_value)"
    exit 1
}

Write-Host "[PASS] REST API solve successful. Status: $($response.status), Objective: $($response.objective_value)"
