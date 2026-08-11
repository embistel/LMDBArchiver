# LMDB Archiver

LMDB Archiver는 Windows 탐색기와 자연스럽게 연결되는 C++17/Qt 6 기반 데스크톱 아카이브 관리자입니다. [LMDB (Lightning Memory-Mapped Database)](https://github.com/LMDB/lmdb)의 ACID 트랜잭션과 메모리 맵 I/O를 저장 계층으로 사용하고, 파일 페이로드는 Qt의 zlib 압축으로 보관합니다.

![LMDB Archiver에서 폴더와 파일을 탐색하는 화면](docs/images/archive-browser.png)

**[최신 Windows 설치본(MSI) 다운로드](https://github.com/embistel/LMDBArchiver/releases/latest)** · [사용자 안내서](docs/USER_GUIDE.ko.md) · [빌드 방법](docs/DEVELOPMENT.md)

## LMDB를 선택한 이유

[LMDB](https://github.com/LMDB/lmdb)는 OpenLDAP 프로젝트를 위해 개발된 작고 빠른 임베디드 키-값 데이터베이스입니다. 이 프로젝트는 공식 GitHub 미러의 `LMDB_0.9.33` 소스를 CMake에서 직접 가져와 정적으로 연결합니다.

- **메모리 매핑과 zero-copy 읽기**: 운영체제의 가상 메모리와 버퍼 캐시를 활용해 별도 애플리케이션 캐시 없이 데이터를 효율적으로 조회합니다.
- **완전한 ACID 트랜잭션과 MVCC**: 아카이브 갱신 도중 오류나 취소가 발생해도 커밋 전 상태를 유지합니다.
- **읽기 중심 동시성**: 여러 읽기 트랜잭션이 쓰기와 서로 막지 않으며, 쓰기는 직렬화되어 교착 상태를 피합니다.
- **정렬된 B+tree 저장소**: 키가 정렬되어 있어 아카이브 항목의 순차 탐색과 접두 경로 검색에 잘 맞습니다.
- **작고 유지 관리가 단순한 C 라이브러리**: 별도 서버, 로그 기반 복구 프로세스 또는 백그라운드 정리 서비스가 필요하지 않습니다.

LMDB 자체는 압축 포맷이 아닙니다. LMDB Archiver가 그 위에 디렉터리 메타데이터, 4 MiB 청크, SHA-256 무결성 정보와 Qt zlib 압축을 결합해 하나의 `.lmdb` 아카이브 형식을 제공합니다. 자세한 원리는 [LMDB 기술 소개](https://www.symas.com/lmdb.php)와 [OpenLDAP 프로젝트](https://project.openldap.org/)에서 확인할 수 있습니다.

## 주요 기능

- 파일과 디렉터리 전체를 단일 `.lmdb` 아카이브에 추가
- 기존 아카이브의 트리 탐색, 검색, 덮어쓰기, 삭제, 선택/전체 추출
- Windows Explorer에서 양방향 끌어다 놓기, 파일 복사 후 `Ctrl+V`, 아카이브 항목 `Ctrl+C` 후 Explorer 붙여넣기
- `.lmdb` 더블 클릭 열기, **여기에 풀기**, 폴더 **LMDB 아카이브로 묶기** 우클릭 메뉴
- UTF-8 경로, 수정 시간과 파일 권한 보존, 경로 순회 공격 방지
- 4 MiB 청크 스트리밍으로 대용량 파일을 일정한 메모리 사용량으로 처리
- 손상 없는 업데이트를 위한 LMDB 트랜잭션
- 모든 데이터 청크의 SHA-256 검증과 GUI/CLI 아카이브 검사
- 삭제 후 빈 LMDB 페이지를 회수하는 원자적 아카이브 압축 정리
- Qt 6.2 이상을 목표로 하며 Qt 6.11/MSVC 2022 로컬 빌드와 Windows/Linux/macOS CI 구성 제공

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

Windows에서는 [GitHub Releases](https://github.com/embistel/LMDBArchiver/releases/latest)의 `LMDBArchiver-<version>-x64.msi` 설치를 권장합니다. MSI는 프로그램을 `Program Files`에 설치하고 모든 사용자용 `.lmdb` 파일 연결과 Explorer 우클릭 메뉴를 자동으로 등록합니다. Windows 11에서는 우클릭 후 **추가 옵션 표시** 안에 메뉴가 나타날 수 있습니다.

1. **파일 → 새 아카이브**로 `.lmdb` 파일을 만듭니다.
2. 도구 모음의 **파일 추가**/**폴더 추가**, 드래그앤드롭 또는 Explorer에서 복사한 뒤 `Ctrl+V`로 항목을 넣습니다.
3. 트리에서 여러 항목을 선택해 풀거나 삭제합니다. 같은 내부 경로에 다시 추가하면 최신 내용으로 교체됩니다.
4. 휴대용 ZIP을 사용한다면 **도구 → Windows 탐색기 통합**으로 현재 사용자용 파일 연결과 우클릭 메뉴를 설치합니다. MSI 설치에서는 이 작업이 자동입니다.

상세 내용은 [사용자 안내서](docs/USER_GUIDE.ko.md)와 [개발자 안내서](docs/DEVELOPMENT.md)를 참고하십시오.

### 명령줄 자동화

배포 패키지의 `LMDBArchiverCLI.exe`는 대화상자 없이 같은 코어를 사용합니다.

```powershell
LMDBArchiverCLI create photos.lmdb C:\Photos
LMDBArchiverCLI add photos.lmdb C:\More --destination imports
LMDBArchiverCLI list photos.lmdb
LMDBArchiverCLI extract photos.lmdb C:\Restored Photos\2026
LMDBArchiverCLI remove photos.lmdb imports\obsolete.jpg
LMDBArchiverCLI test photos.lmdb
LMDBArchiverCLI compact photos.lmdb
```

`create`는 기존 아카이브를 덮어쓰지 않습니다. `extract`에서 내부 경로를 생략하면 전체를 풉니다.

## 패키지

권장 MSI 설치 패키지:

```powershell
scripts\build-msi.ps1 -BuildDirectory build\release -QtBin F:\Qt\6.11.0\msvc2022_64\bin
```

스크립트는 고정된 WiX Toolset 5를 로컬 빌드 디렉터리에 준비하고, 휴대용 배포 파일을 스테이징한 뒤 `out/LMDBArchiver-<version>-x64.msi`를 생성하고 검사합니다. 생성된 MSI는 아직 코드 서명되지 않았으므로 외부 배포 시 Windows 코드 서명 인증서로 서명하는 것을 권장합니다.

휴대용 ZIP:

```powershell
scripts\package.ps1 -BuildDirectory build\release -QtBin F:\Qt\6.11.0\msvc2022_64\bin
```

스크립트는 `windeployqt`로 필요한 Qt DLL과 플러그인, 휴대용 MSVC 런타임, 제3자 고지를 모은 뒤 `out/LMDBArchiver-<version>-win64.zip`을 생성합니다.

## 현재 통합 범위

Windows의 ZIP 압축 폴더와 완전히 같은 가상 폴더 구현은 COM 네임스페이스 확장과 별도 설치/서명이 필요합니다. 현재 버전은 안정적으로 배포 가능한 파일 연결, Explorer 우클릭, 드래그앤드롭, 클립보드 복사를 제공합니다. 향후 네임스페이스 확장은 코어 라이브러리를 재사용할 수 있습니다.

## 라이선스

MIT. 포함되는 LMDB는 OpenLDAP Public License를 따릅니다.
