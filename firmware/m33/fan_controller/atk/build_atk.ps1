[CmdletBinding()]
param(
    [string]$AtkRoot,
    [string]$BuildRoot,
    [string]$CMake = 'C:\Program Files\CMake\bin\cmake.exe',
    [string]$Ninja = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe',
    [string]$ArmGccRoot = 'C:\ST\STM32CubeIDE_1.17.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.12.3.rel1.win32_1.1.0.202410251130\tools\bin'
)

$ErrorActionPreference = 'Stop'
$scriptRoot = Split-Path -Parent $PSCommandPath
$repoRoot = [IO.Path]::GetFullPath((Join-Path $scriptRoot '..\..\..\..'))
if (-not $AtkRoot) {
    $AtkRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot '..\..\_workspace\m33\STM32Cube_ATK_FW_MP2_V1.0.0'))
}
if (-not $BuildRoot) {
    $BuildRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot '..\..\_workspace\b\m33fan'))
}
$AtkRoot = [IO.Path]::GetFullPath($AtkRoot)
$BuildRoot = [IO.Path]::GetFullPath($BuildRoot)

$baseRelative = 'Projects\STM32MP257D-ATK\Applications\CM33_OpenAMP_DEMO\OpenAMP_TTY_echo'
$baseProject = Join-Path $AtkRoot $baseRelative
$stagePackage = $AtkRoot
$stageProject = Join-Path (Split-Path -Parent $baseProject) '_EdgeFocus_FanController_build'
$cmakeBuild = Join-Path $BuildRoot 'cmake-build'
$artifactDirectory = Join-Path $BuildRoot 'artifacts'
$projectName = 'EdgeFocus_FanController_CM33_NonSecure'

foreach ($required in @($baseProject, $CMake, $Ninja, (Join-Path $ArmGccRoot 'arm-none-eabi-g++.exe'))) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required path not found: $required"
    }
}

# The build root is user-selected, but destructive cleanup is allowed only
# below the resolved self_developed workspace.
$allowedRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot '..\..'))
if (-not $BuildRoot.StartsWith($allowedRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "BuildRoot must stay below $allowedRoot"
}
if (Test-Path -LiteralPath $BuildRoot) {
    Remove-Item -LiteralPath $BuildRoot -Recurse -Force
}
if (Test-Path -LiteralPath $stageProject) {
    $reparsePoint = Get-ChildItem -LiteralPath $stageProject -Recurse -Force |
        Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint } |
        Select-Object -First 1
    if ($reparsePoint) {
        throw "Refusing recursive cleanup because staged project contains a reparse point: $($reparsePoint.FullName)"
    }
    Remove-Item -LiteralPath $stageProject -Recurse -Force
}
New-Item -ItemType Directory -Force $cmakeBuild, $artifactDirectory | Out-Null
Copy-Item -LiteralPath $baseProject -Destination $stageProject -Recurse

# The vendor archive contains generated objects with stale Linux absolute
# paths. A staged build must start from metadata and sources only.
$staleOutput = Join-Path $stageProject 'STM32CubeIDE\CM33\NonSecure\CA35TDCID_m33_ns_sign'
if (Test-Path -LiteralPath $staleOutput) {
    Remove-Item -LiteralPath $staleOutput -Recurse -Force
}

$coreSource = Join-Path $stageProject 'CM33\NonSecure\Core\Src\main.c'
Copy-Item -LiteralPath (Join-Path $scriptRoot 'overlay\main.c') -Destination $coreSource -Force

$coreInc = Join-Path $stageProject 'CM33\NonSecure\Core\Inc'
$coreSrc = Join-Path $stageProject 'CM33\NonSecure\Core\Src'
$sourceMap = @{
    (Join-Path $scriptRoot 'overlay\edgefocus_fan_board.h') = $coreInc
    (Join-Path $scriptRoot 'overlay\edgefocus_fan_board.cpp') = $coreSrc
    (Join-Path $repoRoot 'firmware\m33\fan_controller\FanControllerApp.h') = $coreInc
    (Join-Path $repoRoot 'firmware\m33\fan_controller\FanControllerApp.cpp') = $coreSrc
    (Join-Path $repoRoot 'platform\fan\core\FanControlCore.h') = $coreInc
    (Join-Path $repoRoot 'platform\fan\core\FanControlCore.cpp') = $coreSrc
    (Join-Path $repoRoot 'platform\fan\core\PwmMapping.h') = $coreInc
    (Join-Path $repoRoot 'platform\fan\core\FanProtocol.h') = $coreInc
    (Join-Path $repoRoot 'platform\fan\core\FanProtocol.cpp') = $coreSrc
}
foreach ($file in $sourceMap.Keys) {
    Copy-Item -LiteralPath $file -Destination $sourceMap[$file] -Force
}

# The OpenAMP echo template does not use a timer, so its HAL configuration
# leaves TIM disabled. Enable it only in the staged firmware copy.
$halConfig = Join-Path $coreInc 'stm32mp2xx_hal_conf.h'
$halConfigText = Get-Content -LiteralPath $halConfig -Raw
$halConfigText = $halConfigText.Replace(
    '/* #define HAL_TIM_MODULE_ENABLED */',
    '#define HAL_TIM_MODULE_ENABLED')
[IO.File]::WriteAllText($halConfig, $halConfigText, (New-Object Text.UTF8Encoding($false)))

$toolchain = Join-Path $scriptRoot 'arm-none-eabi-toolchain.cmake'
$cmakeArguments = @(
    '-S', $scriptRoot,
    '-B', $cmakeBuild,
    '-G', 'Ninja',
    "-DCMAKE_MAKE_PROGRAM=$Ninja",
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
    "-DARM_GCC_ROOT=$ArmGccRoot",
    "-DATK_ROOT=$AtkRoot",
    "-DSTAGE_ROOT=$stageProject",
    '-DCMAKE_BUILD_TYPE=MinSizeRel',
    '-DCMAKE_POLICY_VERSION_MINIMUM=3.5'
)
& $CMake @cmakeArguments
if ($LASTEXITCODE -ne 0) {
    throw "CMake configuration failed with exit code $LASTEXITCODE"
}
& $CMake --build $cmakeBuild --target edgefocus_fan
if ($LASTEXITCODE -ne 0) {
    throw "Ninja firmware build failed with exit code $LASTEXITCODE"
}

$elf = Join-Path $cmakeBuild "$projectName.elf"
$strippedElf = Join-Path $cmakeBuild "${projectName}_stripped.elf"
foreach ($artifact in @($elf, $strippedElf)) {
    if (-not (Test-Path -LiteralPath $artifact)) {
        throw "Expected artifact not produced: $artifact"
    }
}
Copy-Item -LiteralPath $elf, $strippedElf -Destination $artifactDirectory -Force
Get-Item -LiteralPath (Join-Path $artifactDirectory (Split-Path -Leaf $elf)), `
    (Join-Path $artifactDirectory (Split-Path -Leaf $strippedElf)) |
    Select-Object FullName, Length, LastWriteTime
