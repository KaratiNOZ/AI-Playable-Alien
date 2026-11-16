# Build a source catalog for every installed level; game files remain read-only.
$ErrorActionPreference='Stop'
$production='C:\Program Files (x86)\Steam\steamapps\common\Alien Isolation\DATA\ENV\PRODUCTION'
$repo=[IO.Path]::GetFullPath("$PSScriptRoot\..\..")
$packages=@(& rg --files $production -g COMMANDS.PAK)
$entries=[Collections.Generic.List[string]]::new()
$report=[Collections.Generic.List[object]]::new()
foreach($package in $packages) {
    $level=$package.Substring($production.Length+1) -replace '\\WORLD\\COMMANDS\.PAK$',''
    try {
        & "$PSScriptRoot\export_inspection_points.ps1" -Level $level *> "$repo\build-ghidra\inspection_export_last.log"
        $rows=@(Get-Content "$repo\build-ghidra\inspection_points.h" | Where-Object {$_ -match '^  \{'})
        foreach($row in $rows){$entries.Add($row)}
        $report.Add([PSCustomObject]@{Level=$level;Count=$rows.Count;Error=$null})
        Write-Output ($level+': '+$rows.Count)
    } catch {
        $report.Add([PSCustomObject]@{Level=$level;Count=0;Error=$_.Exception.Message})
        Write-Output ($level+': FAILED '+$_.Exception.Message)
    }
    [GC]::Collect()
}
$report | ConvertTo-Json | Set-Content "$repo\build-ghidra\inspection_levels.json"
if(@($report|Where-Object Error).Count){throw 'Some levels could not be exported; catalog was not replaced.'}
$all=@('// Generated from authored CATHODE jobs in the installed game; regenerate with dev/probe/export_all_inspection_points.ps1.',
       'static constexpr InspectionPoint kInspectionPoints[] = {')+@($entries)+@('};')
$all | Set-Content "$repo\data\inspection_points.inc"
Write-Output ('TOTAL: '+$entries.Count+' points in '+$packages.Count+' levels')
