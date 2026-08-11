# 개발자 안내서

## 구조

- `src/archive`: Qt Core + LMDB만 사용하는 저장/압축/추출 코어
- `src/app`: Qt Widgets UI, 트리 모델, 외부 파일 드롭
- `src/platform`: Windows 셸 통합과 MSI 설치 상태 감지
- `tests`: 임시 디렉터리를 사용하는 왕복/교체/삭제 테스트
- `installer`: WiX 5 MSI 정의와 설치 라이선스
- `scripts`: 휴대용 ZIP 및 MSI 패키징 자동화

## 아카이브 형식 v2

LMDB는 `MDB_NOSUBDIR`로 열립니다. 메타데이터 키는 UTF-8 `entry:<normalized/path>`이며 값은 `QDataStream::Qt_6_0`으로 기록한 다음 필드입니다.

1. magic `LMDA` (`quint32`)
2. 포맷 버전 (`quint16`)
3. 디렉터리 여부
4. 원본 크기
5. UTC epoch 기준 수정 시각(ms)
6. Qt 파일 권한 비트
7. 데이터 청크 수

파일은 최대 4 MiB 단위로 읽고 `chunk:<normalized/path>\0<index>` 키에 저장합니다. 각 청크에는 원본 크기, 압축 여부, 원본 SHA-256, 원본 또는 `qCompress(level=7)` 페이로드가 들어갑니다. 압축 결과가 원본보다 작을 때만 압축 페이로드를 사용합니다. 따라서 파일 전체를 메모리에 올리지 않으며 매우 큰 파일도 일정한 메모리로 처리합니다. 추출과 아카이브 검사 시 SHA-256을 다시 계산해 압축 여부와 관계없이 손상을 검출합니다.

쓰기 작업은 모든 청크와 메타데이터를 단일 LMDB 트랜잭션으로 커밋하며 예상 입력 크기에 따라 map size를 2배 단위로 확장합니다. v1 단일 값 아카이브는 계속 읽고 추출할 수 있고, 같은 항목을 다시 추가하면 v2로 교체됩니다.

## 호환성 원칙

- C++17 및 Qt 6.2의 공개 API 범위 유지
- Qt 패치 버전이나 설치 절대 경로를 소스에 포함하지 않음
- Windows/MSVC가 주 대상이며 코어와 UI는 Linux/macOS에서도 빌드 가능
- 셸 통합은 `Q_OS_WIN`으로 격리하고 다른 OS에서는 명확한 오류 반환

포맷을 바꿀 때는 기존 버전 판독기를 유지하고 `kFormatVersion`을 올리십시오.

## Windows 패키징

`scripts/build-msi.ps1`은 먼저 `scripts/package.ps1`로 Qt 플러그인, MSVC 런타임, 라이선스를 포함한 스테이징 디렉터리를 만들고 WiX Toolset 5로 x64 MSI를 생성합니다. WiX는 `build/tools/wix` 아래에 버전 고정으로 설치되며 시스템 전역 설치를 변경하지 않습니다.

MSI는 HKLM에 `.lmdb` ProgID, 열기/풀기 명령, 폴더 묶기, 파일 추가 메뉴를 등록합니다. 휴대용 앱의 메뉴는 HKCU 통합만 관리하며, HKLM 설치를 감지하면 MSI가 관리 중임을 안내합니다. 설치 정의를 바꾼 뒤에는 관리자 설치·제거 시험과 `wix msi validate`를 모두 통과시켜야 합니다.
