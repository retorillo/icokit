$exe = "icokit.exe"
$log = "build.log"
if (test-path $exe) { del $exe }
g++ icokit.cc -std=c++11 -m64 -o $exe 2> $log
type $log
if (test-path $exe) { &".\$exe" extract "./test/target.exe" --output "$home"  }
