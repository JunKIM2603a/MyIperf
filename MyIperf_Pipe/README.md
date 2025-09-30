# 터미널
Visual Studio 2022 사용

---

# 실행
cd D:\01_SW2_Project\MyIperf_Pipe\
.\Debug\consumer.exe client 127.0.0.1 5201
.\Debug\consumer.exe server 0.0.0.0 5201

---

# 빌드환경 구성
cd D:\01_SW2_Project\MyIperf_Pipe
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

---

# 옵션 사용법
`consumer.exe <mode> <IP> <Port>`

- `<mode>`: 실행 모드 (예: server, client)
- `<IP>`: IP 주소
- `<Port>`: 포트 번호