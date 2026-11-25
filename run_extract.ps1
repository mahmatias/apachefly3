$p = Start-Process 'D:\Trabalho\Comanche\build\src\Debug\c3_extract.exe' -ArgumentList '--list', '--iso', 'D:\Trabalho\Comanche\c3_ptbr.iso' -Wait -RedirectStandardOutput 'D:\Trabalho\Comanche\out.txt' -RedirectStandardError 'D:\Trabalho\Comanche\err.txt' -PassThru
$p.ExitCode
Get-Content 'D:\Trabalho\Comanche\out.txt'
Get-Content 'D:\Trabalho\Comanche\err.txt'