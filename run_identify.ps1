$p = Start-Process 'D:\Trabalho\Comanche\build\src\Debug\c3_extract.exe' -ArgumentList 'D:\Games\C3', '--identify', '--from-disk' -Wait -RedirectStandardOutput 'D:\Trabalho\Comanche\identify_out.txt' -RedirectStandardError 'D:\Trabalho\Comanche\identify_err.txt' -PassThru
$p.ExitCode
Get-Content 'D:\Trabalho\Comanche\identify_out.txt'
Get-Content 'D:\Trabalho\Comanche\identify_err.txt'