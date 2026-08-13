# 개발자 안내서

## 구조

- `src/archive`: Qt Core + LMDB만 사용하는 저장/압축/추출 코어
- `src/app`: Qt Widgets UI, 트리 모델, 외부 파일 드롭
- `src/platform`: Windows 셸 통합과 MSI 설치 상태 감지
- `tests`: 임시 디렉터리를 사용하는 왕복/교체/삭제 테스트
- `installer`: WiX 5 MSI 정의와 설치 라이선스
- `scripts`: 휴대용 ZIP 및 MSI 패키징 자동화

## 아카이브 형식 (Native)

LMDB는 `MDB_NOSUBDIR`로 열립니다. **키는 UTF-8 경로 문자열(`cleanArchivePath`로 정규화), 값은 원본 파일 바이트 그대로**입니다. 헤더·매직·압축·청크 분할이 전혀 없는 순수 스키마로, LucioraEla 원 시스템, Python `lmdb`, C liblmdb 등 모든 표준 LMDB 도구가 투명하게 읽고 쓸 수 있습니다.

보존되지 않는 정보: 빈 디렉터리, 수정 시각, 파일 권한, 무결성 해시(메타데이터를 값에 넣을 수 없기 때문). 디렉터리 노드는 `entries()`가 키 경로에서 메모리 합성하여 트리뷰에 보여줍니다.

쓰기는 파일 전체를 읽어 단일 LMDB 트랜잭션으로 커밋하며, 예상 입력 크기에 따라 map size를 2배 단위로 확장합니다.

### 선택적 gzip 압축

`addPaths(..., compress=true)`로 추가하면 값을 표준 gzip(RFC 1952)으로 압축하고 키에 `.<crc8hex>.gz` 마커를 붙입니다. `<crc8hex>`는 논리 경로의 UTF-8에 대한 CRC-32(8자 소문자 hex)로, `extract(..., autoDecompress=true)`(기본값)는 이 마커를 검증해 자동 해제합니다. 값에는 어떤 메타데이터도 들어가지 않으므로 이식성이 유지되며, CRC 검증 덕분에 사용자의 진짜 `.gz` 파일은 자동 해제 대상에서 안전하게 제외됩니다. 원본 크기는 압축 해제 없이 gzip 트레일러의 ISIZE에서 읽습니다. 압축은 정적으로 포함한 miniz 2.2.0(`vendor/miniz/`)이 담당합니다.

## 진행률 콜백

장시간 동작(`addPaths`, `extract`, `verify`)은 호출자가 진행률을 표시하거나 취소할 수 있도록 `Progress` 콜백을 받는다.

```cpp
enum class ProgressPhase { Collecting, Processing, Finalizing };
struct ProgressInfo {
    ProgressPhase phase;
    QString currentItem;       // 현재 처리 중인 경로
    qint64 bytesDone;          // 누적 처리 바이트
    qint64 bytesTotal;         // 전체 바이트 (0 == 알 수 없음)
    qint64 itemBytesDone;      // 현재 파일 내 진행 바이트
    qint64 itemBytesTotal;     // 현재 파일 전체 바이트
};
using Progress = std::function<bool(const ProgressInfo &)>;
```

- `Collecting` — 사전 수집 단계. `addPaths`의 디렉터리 순회 중 항목 카운트를 보고한다.
- `Processing` — 실제 I/O. 파일 단위로 바이트를 갱신한다.
- `Finalizing` — 트랜잭션 커밋 직전. UI는 busy 스피너로 전환한다.
- `bytesTotal`을 알기 위해 `extract`/`verify`는 처리 전에 한 번 커서를 순회해 총 바이트를 합산한다.
- 콜백이 `false`를 반환하면 트랜잭션을 중단하고 이전 커밋 상태를 유지한다.
- GUI(`MainWindow::applyProgress`)는 바이트 범위와 퍼센트를 대화상자에 표시하고, CLI는 TTY일 때 stderr에 한 줄 캐리지 리턴 갱신으로 출력한다.

## 호환성 원칙

- C++17 및 Qt 6.2의 공개 API 범위 유지
- Qt 패치 버전이나 설치 절대 경로를 소스에 포함하지 않음
- Windows/MSVC가 주 대상이며 코어와 UI는 Linux/macOS에서도 빌드 가능
- 셸 통합은 `Q_OS_WIN`으로 격리하고 다른 OS에서는 명확한 오류 반환

Native 스키마는 버전 필드가 없는 순수 key→value 포맷이므로 별도의 버전 관리가 필요 없다. 압축 여부는 키의 `.<crc8>.gz` 마커로 자기 서술한다.

## Windows 패키징

`scripts/build-msi.ps1`은 먼저 `scripts/package.ps1`로 Qt 플러그인, MSVC 런타임, 라이선스를 포함한 스테이징 디렉터리를 만들고 WiX Toolset 5로 x64 MSI를 생성합니다. WiX는 `build/tools/wix` 아래에 버전 고정으로 설치되며 시스템 전역 설치를 변경하지 않습니다.

MSI는 HKLM에 `.lmdb` ProgID, 열기/풀기 명령, 폴더 묶기, 파일 추가 메뉴를 등록합니다. 휴대용 앱의 메뉴는 HKCU 통합만 관리하며, HKLM 설치를 감지하면 MSI가 관리 중임을 안내합니다. 설치 정의를 바꾼 뒤에는 관리자 설치·제거 시험과 `wix msi validate`를 모두 통과시켜야 합니다.
