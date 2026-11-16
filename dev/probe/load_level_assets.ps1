# Read-only asset inspection through the user's installed OpenCAGE library.
param([string]$Level = 'DLC\SALVAGEMODE1')
$ErrorActionPreference = 'Stop'
$cathode = [Reflection.Assembly]::LoadFrom('C:\Program Files (x86)\Steam\steamapps\common\OpenCAGE\CathodeLib.dll')
$assetRoot = 'C:\Program Files (x86)\Steam\steamapps\common\Alien Isolation\DATA\ENV\PRODUCTION\' + $Level + '\WORLD\'
function New-CathodeAsset($Name, $Arguments) {
    [Activator]::CreateInstance($cathode.GetType('CATHODE.' + $Name), [object[]]$Arguments)
}
# Commands' parser requires non-null supporting containers, even for inspection.
$animationStrings = New-CathodeAsset AnimationStrings @("$PSScriptRoot\..\..\build-ghidra\ANIM_STRING_DB.BIN")
$environmentAnimations = New-CathodeAsset EnvironmentAnimations @(($assetRoot + 'ENVIRONMENT_ANIMATION.DAT'), $animationStrings)
$physicsAsset = New-CathodeAsset HavokPackfile @($assetRoot + 'PHYSICS.HKX')
$collisionAsset = New-CathodeAsset CollisionMaps @(($assetRoot + 'COLLISION.MAP'), $null, $null, $physicsAsset)
$renderableAsset = New-CathodeAsset RenderableElements @(($assetRoot + 'REDS.BIN'), $null, $null)
$commandsAsset = New-CathodeAsset Commands @(($assetRoot + 'COMMANDS.PAK'), $environmentAnimations,
    $collisionAsset, $renderableAsset, $physicsAsset, $null, $null)
if (!$commandsAsset.Loaded) { throw "Could not read level commands: $Level" }
