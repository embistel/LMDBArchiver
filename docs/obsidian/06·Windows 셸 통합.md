---
tags: [project, windows, shell, registry, msi]
up: "[[LMDB Archiver (MOC)]]"
created: 2026-08-12
---

# 06 · Windows 셸 통합

> [!info] 위치
> `src/platform/shellintegration.{h,cpp}` · `installer/Product.wxs`
> 모든 Win32 코드는 `#ifdef Q_OS_WIN` 블록. 다른 OS는 명확한 오류 반환.

## 두 가지 설치 경로

| | MSI (시스템 전체) | 휴대용 ZIP (사용자) |
|---|---|---|
| 레지스트리 루트 | **HKLM** `Software\Classes\...` | **HKCU** `Software\Classes\...` |
| 권한 | 관리자 | 불필요 |
| 관리 주체 | Windows "설치된 앱" | 앱 내 **도구 → 탐색기 통합** 메뉴 |
| 검출 | `isMachineInstalled()` | `isInstalled()` |
| 충돌 처리 | MSI가 우선; HKCU 메뉴는 안내만 | HKLM 설치 감지 시 설치 거부/안내 |

> [!important] 충돌 회피 원칙
> 휴대용 앱의 메뉴는 HKCU만 만진다. HKLM 설치가 감지되면 MSI가 관리 중임을 안내하고 사용자 조작을 막는다.

---

## `ShellIntegration` 네임스페이스 API

```cpp
namespace ShellIntegration {
    bool install(const QString &executable, QString *error = nullptr);
    bool uninstall(QString *error = nullptr);
    bool isInstalled();          // HKCU에 ProgID 존재?
    bool isMachineInstalled();   // HKLM에 ProgID 존재?
}
```

---

## `install(executable)` — HKCU 통합 설치

레지스트리에 기록하는 항목들 (루트 `HKCU\Software\Classes`):

| 키 | 값 | 용도 |
|---|---|---|
| `.lmdb` | `LMDBArchiver.Archive` | 확장 연결 |
| `LMDBArchiver.Archive` | `LMDB Archive` | ProgID 설명 |
| `LMDBArchiver.Archive\DefaultIcon` | `"<exe>",0` | 아이콘 |
| `LMDBArchiver.Archive\shell\open\command` | `"<exe>" "%1"` | 더블클릭 열기 |
| `LMDBArchiver.Archive\shell\extract` | `여기에 풀기` | 우클릭 |
| `...\shell\extract\command` | `"<exe>" --extract-here "%1"` | |
| `Directory\shell\LMDBArchiver.Pack` | `LMDB 아카이브로 묶기` | 폴더 우클릭 |
| `...\LMDBArchiver.Pack\command` | `"<exe>" --create-from "%1"` | |
| `*\shell\LMDBArchiver.Add` | `LMDB 아카이브에 추가...` | 파일 우클릭 |
| `...\LMDBArchiver.Add\command` | `"<exe>" --add-dialog "%1"` | |

마지막에 `SHChangeNotify(SHCNE_ASSOCCHANGED, ...)`로 탐색기에 변경 알림.

### 기존 연결 백업
`.lmdb` 확장의 이전 ProgID를 `HKCU\Software\embistel\LMDBArchiver\PreviousLmdbAssociation`에 저장. `uninstall` 시 복원.

---

## `uninstall` — HKCU 통합 제거

1. 현재 `.lmdb` 연결이 우리 ProgID인지 확인
2. 백업된 이전 연결이 있으면 **복원**, 없으면 `.lmdb` 키 자체 삭제
3. ProgID 트리 · Pack · Add 메뉴 트리 삭제 (`RegDeleteTreeW`)
4. 상태 키(`Software\embistel\LMDBArchiver`) 삭제
5. `SHChangeNotify`

---

## MSI (`installer/Product.wxs`) — HKLM 등록

WiX 5, `Scope="perMachine"`, `ProgramFiles64Folder\LMDB Archiver`.

`ShellIntegrationComponent`가 HKLM에 **더 풍부한** 메뉴를 등록:

| (HKCU와의 차이) | 추가 항목 |
|---|---|
| `OpenWithProgids` | `LMDBArchiver.Archive` 빈 문자열 |
| `FriendlyTypeName` | `LMDB Archive` |
| `Applications\LMDBArchiver.exe\SupportedTypes` | `.lmdb` |
| `Directory\Background\shell\LMDBArchiver.PackHere` | **현재 폴더를 LMDB 아카이브로 묶기** (배경 우클릭, `%V` 사용) |

> [!note] 배경 메뉴
> `Directory\Background`는 폴더 **내부의 빈 공간** 우클릭. `%V` (작업 디렉터리)를 사용해 선택된 항목이 아닌 현재 폴더를 전달한다. HKCU(휴대용)에는 이 항목이 없다.

### MSI 기타 속성
- `MajorUpgrade` + 고정 `UpgradeCode` (`EECAB4C2-...`) → 다운그레이드 차단
- 시작 메뉴 바로가기 (advertised)
- `WixUI_InstallDir`, `license.rtf`
- `MediaTemplate EmbedCab="yes" CompressionLevel="high"`
- 코드 서명은 **아직 안 됨** → 외부 배포 시 인증서 권장

---

## 🚧 현재 통합 범위 (한계)

ZIP 압축 폴더 같은 **완전한 가상 폴더(네임스페이스 확장)**는:
- COM 네임스페이스 확장 + 별도 설치/서명 필요
- 현재 버전은 **안정적으로 배포 가능한 범위**만 제공:
  - 파일 연결
  - 탐색기 우클릭 메뉴
  - 드래그앤드롤 (양방향)
  - 클립보드 복사/붙여넣기
- 향후 네임스페이스 확장은 동일 코어 재사용 가능

---

**다음:** [[07·빌드와 CI]] · **이전:** [[05·CLI 자동화]] · **MOC:** [[LMDB Archiver (MOC)]]
