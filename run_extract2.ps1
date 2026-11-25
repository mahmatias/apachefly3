$p = Start-Process 'D:\Trabalho\Comanche\build\src\Debug\c3_extract.exe' -ArgumentList 'D:\Games\C3', '--list', '--from-disk' -Wait -RedirectStandardOutput 'D:\Trabalho\Comanche\out2.txt' -RedirectStandardError 'D:\Trabalho\Comanche\err2.txt' -PassThru
$p.ExitCode
Get-Content 'D:\Trabalho\Comanche\out2.txt'
Get-Content 'D:\Trabalho\Comanche\err2.txt'