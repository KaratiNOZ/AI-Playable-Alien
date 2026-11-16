# Export authored job handles/frames, without modifying any game assets.
param([string]$Level = 'DLC\SALVAGEMODE1')
. "$PSScriptRoot\load_level_assets.ps1" -Level $Level
$script:inspectionRows = [Collections.Generic.List[object]]::new()
function Walk-InspectionJobs($Composite, $PathIds, $Ancestors) {
    if ($PathIds.Count -gt 26 -or $Ancestors -contains $Composite.shortGUID.AsUInt32) { return }
    if ($Composite.name -match '^Archetypes\\Jobs\\SearchFlareHidingSpot(High|Low)$') {
        $high = $Matches[1] -eq 'High'
        $animName = if ($high) { 'job_high_locker_in' } else { 'job_flare_hiding_spot_low_001' }
        $command = $Composite.functions_dictionary.Values | Where-Object {
            $_.function.AsFunctionType.ToString() -eq 'CMD_PlayAnimation' -and
            ($_.parameters | Where-Object { [CATHODE.Scripting.ShortGuidUtils]::FindString($_.name) -eq 'Animation' }).content.value -eq $animName
        } | Select-Object -First 1
        if ($command) {
            $commandPath = [CATHODE.Scripting.EntityPath]::new([CATHODE.Scripting.ShortGuid[]](@($PathIds)+@($command.shortGUID)))
            # Verified against native GetEntityHandle on a live level entity.
            # This includes the root instance seed and excludes the leaf ID.
            $instance = $commandPath.GenerateCompositeInstanceID($true)
            $markerLink = $command.childLinks | Where-Object {
                [CATHODE.Scripting.ShortGuidUtils]::FindString($_.thisParamID) -eq 'Marker'
            } | Select-Object -First 1
            $markerId = if ($markerLink) { $markerLink.linkedEntityID } else { $command.shortGUID }
            $markerPath = [CATHODE.Scripting.EntityPath]::new([CATHODE.Scripting.ShortGuid[]](@($PathIds)+@($markerId)))
            $transform = $commandsAsset.Utils.CalculateInstancedPosition($markerPath)
            $script:inspectionRows.Add([PSCustomObject]@{
                Level=$Level
                Top=$PathIds[0].AsUInt32; Entity=$command.shortGUID.AsUInt32; Instance=$instance.AsUInt32
                High=$high; Position=@($transform.Item1.X,$transform.Item1.Y,$transform.Item1.Z)
                Path=@($PathIds|ForEach-Object {$_.ToString()})
            })
        }
    }
    foreach ($entity in $Composite.functions_dictionary.Values) {
        $sub = $commandsAsset.GetComposite($entity.function)
        if ($null -ne $sub) {
            Walk-InspectionJobs $sub (@($PathIds)+@($entity.shortGUID)) (@($Ancestors)+@($Composite.shortGUID.AsUInt32))
        }
    }
}
foreach ($entry in $commandsAsset.EntryPoints) { Walk-InspectionJobs $entry @() @() }
$inspectionRows | ConvertTo-Json -Depth 5 | Set-Content "$PSScriptRoot\..\..\build-ghidra\inspection_points.json"
$ci=[Globalization.CultureInfo]::InvariantCulture
$lines=[Collections.Generic.List[string]]::new()
$lines.Add('// Generated from authored CATHODE inspection jobs; preferred IDs, no runtime addresses.')
$lines.Add('static constexpr InspectionPoint kInspectionPoints[] = {')
foreach ($r in $inspectionRows) {
    $p=($r.Position|ForEach-Object {$_.ToString('R',$ci)+'f'}) -join ','
    # Ensure integer-looking float literals remain valid C++.
    $p=$p -replace '(?<![\d.])(-?\d+)f(?=,|$)','$1.0f'
    $lines.Add(('  {{"{5}",0x{0:X8}u,0x{1:X8}u,0x{2:X8}u,{{{3}}},{4}}},' -f $r.Top,$r.Entity,$r.Instance,$p,[int]$r.High,$Level.Replace('\','\\')))
}
$lines.Add('};')
$lines | Set-Content "$PSScriptRoot\..\..\build-ghidra\inspection_points.h"
Write-Output ('Exported '+$inspectionRows.Count+' authored inspection commands for '+$Level)
