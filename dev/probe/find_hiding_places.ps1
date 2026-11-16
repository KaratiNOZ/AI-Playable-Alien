# Read authored hiding controllers near a world point; no game memory writes.
param([string]$Level='DLC\ChallengeMap16', [double]$X=14.2, [double]$Y=25.3, [double]$Z=-22.9)
. "$PSScriptRoot\load_level_assets.ps1" -Level $Level
$rows=[Collections.Generic.List[object]]::new()
function Walk-Hiding($Composite,$PathIds,$Ancestors,$Names) {
    if($PathIds.Count -gt 26 -or $Ancestors -contains $Composite.shortGUID.AsUInt32){return}
    foreach($f in $Composite.functions_dictionary.Values) {
        $ids=@($PathIds)+@($f.shortGUID)
        if($f.function.AsFunctionType.ToString() -eq 'Custom_Hiding_Vignette_controller') {
            $path=[CATHODE.Scripting.EntityPath]::new([CATHODE.Scripting.ShortGuid[]]$ids)
            $t=$commandsAsset.Utils.CalculateInstancedPosition($path)
            $p=$t.Item1
            $rows.Add([pscustomobject]@{Entity=$f.shortGUID.ToString();Instance=$path.GenerateCompositeInstanceID($true).ToString();
                X=$p.X;Y=$p.Y;Z=$p.Z;Distance=[math]::Sqrt([math]::Pow($p.X-$X,2)+[math]::Pow($p.Y-$Y,2)+[math]::Pow($p.Z-$Z,2));
                Parents=$Names;Path=@($ids|ForEach-Object {$_.ToString()})})
        }
        $sub=$commandsAsset.GetComposite($f.function)
        if($sub){Walk-Hiding $sub $ids (@($Ancestors)+@($Composite.shortGUID.AsUInt32)) (@($Names)+@($sub.name))}
    }
}
foreach($entry in $commandsAsset.EntryPoints){Walk-Hiding $entry @() @() @($entry.name)}
$rows | Sort-Object Distance | ConvertTo-Json -Depth 8 | Set-Content "$PSScriptRoot\..\..\build-ghidra\nearby_hiding_controllers.json"
$rows | Sort-Object Distance | Select-Object -First 6 | ConvertTo-Json -Depth 8
