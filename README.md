# LMDB Archiver

LMDB Archiver는 Windows 탐색기와 자연스럽게 연결되는 C++17/Qt 6 기반 데스크톱 아카이브 관리자입니다. LMDB의 ACID 트랜잭션과 메모리 맵 I/O를 저장 계층으로 사용하고, 파일 페이로드는 Qt의 zlib 압축으로 보관합니다.

## 주요 기능

- 파일과 디렉터리 전체를 단일 `.lmdb` 아카이브에 추가
- 기존 아카이브의 트리 탐색, 검색, 덮어쓰기, 삭제, 선택/전체 추출
- Windows Explorer에서 끌어다 놓기 및 파일 복사 후 `Ctrl+V`
- `.lmdb` 더블 클릭 열기, **여기에 풀기**, 폴더 **LMDB 아카이브로 묶기** 우클릭 메뉴
- UTF-8 경로, 수정 시간과 파일 권한 보존, 경로 순회 공격 방지
- 손상 없는 업데이트를 위한 LMDB 트랜잭션
- Qt 6.2 이상을 목표로 하며 Qt 6.11/MSVC 2022에서 검증

> LMDB는 압축 포맷이 아니라 키-값 데이터베이스입니다. 이 프로젝트는 LMDB 레코드 내부에 압축된 파일 데이터와 메타데이터를 저장하는 자체 포맷을 정의합니다. 다른 LMDB 도구가 파일을 직접 추출할 수는 없습니다.

## 빌드

필요 도구는 Qt 6 (`Core`, `Widgets`, 테스트 시 `Test`), CMake 3.21+, C++17 컴파일러, Git입니다. LMDB 0.9.33 소스는 CMake가 공식 저장소에서 가져옵니다.

PowerShell/MSVC 예시:

```powershell
$env:QT_ROOT = 'F:\Qt\6.11.0\msvc2022_64'
cmake --preset qt-msvc-release
cmake --build --preset release
ctest --preset release
```

Qt 설치 위치가 다르면 `QT_ROOT`만 변경하십시오. Visual Studio Developer PowerShell에서 실행하면 됩니다.

## 사용법

1. **파일 → 새 아카이브**로 `.lmdb` 파일을 만듭니다.
2. 도구 모음의 **파일 추가**/**폴더 추가**, 드래그앤드롭 또는 Explorer에서 복사한 뒤 `Ctrl+V`로 항목을 넣습니다.
3. 트리에서 여러 항목을 선택해 풀거나 삭제합니다. 같은 내부 경로에 다시 추가하면 최신 내용으로 교체됩니다.
4. **도구 → Windows 탐색기 통합**으로 현재 사용자용 파일 연결과 우클릭 메뉴를 설치합니다.

상세 내용은 [사용자 안내서](docs/USER_GUIDE.ko.md)와 [개발자 안내서](docs/DEVELOPMENT.md)를 참고하십시오.

## 패키지

```powershell
scripts\package.ps1 -BuildDirectory build\release -QtBin F:\Qt\6.11.0\msvc2022_64\bin
```

스크립트는 `windeployqt`로 필요한 Qt DLL과 플러그인, 휴대용 MSVC 런타임, 제3자 고지를 모은 뒤 `out/LMDBArchiver-<version>-win64.zip`을 생성합니다.

## 현재 통합 범위

Windows의 ZIP 압축 폴더와 완전히 같은 가상 폴더 구현은 COM 네임스페이스 확장과 별도 설치/서명이 필요합니다. 현재 버전은 안정적으로 배포 가능한 파일 연결, Explorer 우클릭, 드래그앤드롭, 클립보드 복사를 제공합니다. 향후 네임스페이스 확장은 코어 라이브러리를 재사용할 수 있습니다.

## 라이선스

MIT. 포함되는 LMDB는 OpenLDAP Public License를 따릅니다.
