param([string]$Dir = 'V:\proyectos\LOGOS_TEST', [string]$Exe = 'V:\proyectos\IRON3D_STUDIO\build\bin\Release\Iron3DStudio.exe',
       [string]$Cam = '1000,1000,0,0,2.75,0,-0.03,0')
# Renderiza cada .iron de $Dir a PNG con el render por CPU de Iron3D (--ui-cmd=render-save).
foreach ($f in Get-ChildItem $Dir -Filter *.iron) {
    $png = [IO.Path]::ChangeExtension($f.FullName, '.png')
    Remove-Item $png -ErrorAction SilentlyContinue
    while (Get-Process Iron3DStudio -ErrorAction SilentlyContinue) { Start-Sleep 2 }
    $p = Start-Process $Exe -ArgumentList $f.FullName, '--lang=es', '--no-focus', '--ui-cmd=render-fx', "--ui-cmd=render-save=$Cam,$png" -PassThru
    if (-not $p.WaitForExit(120000)) { $p.Kill(); Write-Host "TIMEOUT $($f.Name)"; continue }
    Write-Host ("{0}: exit={1} png={2}" -f $f.Name, $p.ExitCode, (Test-Path $png))
}
