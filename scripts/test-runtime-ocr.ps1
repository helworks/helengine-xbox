param([Parameter(Mandatory=$true)][string]$OcrResult)
$ErrorActionPreference = 'Stop'
$result = Get-Content -LiteralPath $OcrResult -Raw | ConvertFrom-Json
$successful = @($result.diagnostics.ocr_results | Where-Object success)
if ($successful.Count -eq 0) { throw 'No successful HelenUI OCR result.' }
foreach ($reading in $successful) {
    if ($reading.raw_text -match '(?i)core\s+fatal|halted|construction is unsupported|binding.*was not') {
        throw "Runtime failure visible in HelenUI OCR: $($reading.raw_text)"
    }
}
Write-Output 'No recognized runtime fatal in this HelenUI frame. This is not visual rendering acceptance.'
