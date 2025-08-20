# 터미널
Visual Studio 2022 사용

---

# 서버 실행

./IPEFTC --mode server --target 0.0.0.0 --port 5201
//# 또는 설정 파일을 사용하는 경우
//# ./IPEFTC --mode server --config server_config.json

cd D:\01_SW2_Project\MyIperf\
.\build\Debug\IPEFTC.exe --mode server --target 0.0.0.0 --port 5201
.\build\Release\IPEFTC.exe --mode server --target 0.0.0.0 --port 5201


---

# 클라이언트 실행
./IPEFTC --mode client --target 192.168.1.100 --port 5201 --packet-size 8192 
//# 또는 설정 파일을 사용하는 경우
//# ./IPEFTC --mode client --config client_config.json

cd D:\01_SW2_Project\MyIperf
.\build\Debug\IPEFTC.exe --mode client --target 127.0.0.1 --port 5201 --packet-size 8192 --num-packets 100
.\build\Release\IPEFTC.exe --mode client --target 127.0.0.1 --port 5201 --packet-size 8192 --num-packets 100

---

# 빌드환경 구성
cd D:\01_SW2_Project\MyIperf
Remove-Item .\build\ -Recurse -Force -ErrorAction Stop
mkdir build
cd build
cmake ..
cmake --build . --config Debug
cmake --build . --config Release

del build
mkdir build
cd build
cmake ..

# 컴파일
./build/ 안의 sln 프로젝트를 visual studio 로 열기
visual studio 에서 컴파일

cmake --build . --config Release



# Cmake Clean 하는 방법
del CMakeCache.txt
del .\CMakeFile
