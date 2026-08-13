---
tags: [project, gui, qt, widgets]
up: "[[LMDB Archiver (MOC)]]"
created: 2026-08-12
---

# 04 · Qt Widgets GUI

> [!info] 위치
> `src/main.cpp` (진입점/스타일) · `src/app/*` (창·모델·뷰)

## 🎨 진입점 (`src/main.cpp`)

```cpp
QApplication app(argc, argv);
app.setStyle(QStyleFactory::create("Fusion"));
app.setFont(QFont("Segoe UI", 10));
app.setStyleSheet(/* 커스텀 Fusion 테마 */);
return App::run(app);
```

- 조직: `embistel`, 앱: `LMDBArchiver`
- Fusion 스타일 + Segoe UI 10pt
- 밝은 톤의 커스텀 QSS (배경 `#f6f7fb`, 강조 `#3478d4`, 둥근 모서리)
- 버전 매크로: `LMDBARCHIVER_VERSION` (CMake에서 주입)

---

## 🧭 `App::run` — CLI 옵션 라우팅 (`src/app/app.cpp`)

GUI 실행파일은 **탐색기 메뉴**에서 호출되는 비-대화형 모드도 처리:

| 옵션 | 동작 |
|---|---|
| (위치 인자) `<archive>` | 해당 `.lmdb`를 창으로 열기 |
| `--create-from <path>` | 파일/폴더로 새 아카이브 생성 (`uniqueArchivePath`로 이름 충돌 회피) |
| `--extract-here <archive>` | 아카이브를 같은 폴더에 풀기 (`<completeBaseName>` 폴더) |
| `--add-dialog <path>` | 파일 대화상자로 대상 아카이브를 고른 후 경로 추가 |
| `--install-shell` | HKCU 셸 통합 설치 |
| `--uninstall-shell` | HKCU 셸 통합 제거 |

→ 이 옵션들이 [[06·Windows 셸 통합]] 레지스트리 명령과 짝을 이룬다.

---

## 🪟 `MainWindow` — 메인 창

`src/app/mainwindow.{h,cpp}` · `QMainWindow` · 최소 760×480, 기본 1120×720.

### 레이아웃
```
┌─ 배너 (QFrame#ArchiveBanner) ───────────────────┐
│  [아이콘]  아카이브 이름 (#ArchiveName)           │
│           절대 경로   (#ArchiveLocation)          │
├─────────────────────────────────────────────────┤
│  [검색 QLineEdit (placeholder: 아카이브에서 검색)]│
├─────────────────────────────────────────────────┤
│  DropView (QTreeView)                            │
│   이름 | 크기 | 저장 크기 | 수정한 날짜 | 형식    │
│   · 확장 선택 · 행 단위 · 컨텍스트 메뉴           │
│   · 정렬 가능 · 교대 행 색 · 드래그 in/out        │
├─────────────────────────────────────────────────┤
│  상태 표시줄: "N개 파일 · 크기 · 경로"            │
└─────────────────────────────────────────────────┘
```

### 메뉴 구조
| 메뉴 | 항목 |
|---|---|
| **파일(&F)** | 새 아카이브(Ctrl+N) · 열기(Ctrl+O) · 끝내기(Ctrl+Q) |
| **편집(&E)** | 파일 추가(Ctrl+I) · 폴더 추가 · 클립보드 붙여넣기(Ctrl+V) · 복사(Ctrl+C) · 삭제(Del) |
| **아카이브(&A)** | 선택 풀기(Ctrl+E) · 모두 풀기 · 검사 · 압축 정리 · 새로고침(F5) |
| **도구(&T)** | Windows 탐색기 통합… |
| **도움말(&H)** | LMDB Archiver 정보 |

툴바: 새로 만들기 · 열기 · 파일 추가 · 폴더 추가 · 풀기 · 삭제 (아이콘+텍스트)

### 창 상태 복원
- `QSettings`로 geometry 저장/복원 (`window/geometry`)
- 시작 시 `cleanStaleExports()` — 7일 이상 지난 임시 내보내기 폴더 제거

---

## 🌳 `ArchiveModel` — 트리 모델

`src/app/archivemodel.{h,cpp}` · `QStandardItemModel`.

### 열
| 0 이름 | 1 크기 | 2 저장 크기 | 3 수정한 날짜 | 4 형식 |
|---|---|---|---|---|
| 폴더/파일 아이콘 + 이름 | `QLocale::formattedDataSize` | 동일 | `yyyy-MM-dd HH:mm` | "폴더"/"파일" |

### 커스텀 역할
- `PathRole` (`Qt::UserRole+1`) — 내부 정규화 경로
- `DirectoryRole` — 디렉터리 여부

### `setEntries(entries, filter)`
- 평탄한 `ArchiveEntry` 목록을 **중첩 트리**로 빌드
- 부모 경로를 `QHash<QString, QStandardItem*> folders`로 캐싱
- `filter`가 비어 있지 않으면 경로 `contains` 필터링
- 디렉터리 노드는 크기/날짜/형식 비움
- 아이콘: `QFileIconProvider` (Folder / File)

### `pathForIndex`
선택된 인덱스의 내부 경로를 반환 (`PathRole`).

---

## 🖱️ `DropView` — 드래그앤드롭

`src/app/dropview.{h,cpp}` · `QTreeView` 하위.

| 시그널 | 언제 |
|---|---|
| `localFilesDropped(paths, destination)` | 외부 파일이 뷰에 드롭됨 |
| `archiveDragRequested(paths)` | 사용자가 항목을 밖으로 드래그 시작 (`startDrag`) |

- `DragDropMode = InternalMove` 호환 + 외부 URL 수락
- 드롭 위치가 폴더/파일이면 적절한 `destination` 계산
- 드래그 시작 시 선택 행들의 `PathRole` 수집

---

## 📋 주요 슬롯 동작

### `addLocalPaths` (파일 추가 공통)
`QProgressDialog`를 모달로 띄워 `Archive::addPaths`의 `ProgressInfo` 콜백을 공통 헬퍼 `applyProgress`로 표시. 라벨은 2줄(단계 + 현재 파일 / `3.2GB / 10.0GB (32%)`), `bytesTotal`이 0이면 busy 스피너. 취소 시 안내. 완료 후 `refresh()`.

### `applyProgress` / `formatProgressLabel` (공통 진행률 헬퍼)
`addLocalPaths`·`extractPaths`·`verifyArchive`가 공유. `ProgressInfo`를 받아:
- `phase`별 라벨: "파일 수집 중…" / "처리 중…" / "변경 사항 저장 중…"
- `bytesTotal > 0` → `%` 진행바 (`(bytesDone*100)/bytesTotal`)
- `bytesTotal == 0` 또는 `Finalizing` → Qt indeterminate 스피너
- 크기는 `QLocale::formattedDataSize`

### `exportPaths` (복사·드래그·열기용)
1. 요청 경로를 **최상위 집합**으로 정규화 (상위가 이미 포함되면 제외)
2. `%TEMP%/LMDBArchiver/exports/<uuid>` 에 추출
3. 로컬 파일 경로 목록 반환

`copySelected()`는 이 결과를 클립보드 `QMimeData::setUrls`로, `dragSelectedOut()`은 `QDrag`로, `openSelected()`는 `QDesktopServices::openUrl`로.

### `openSelected`
- 디렉터리 → 확장/축소 토글
- 파일 → 임시 추출 후 기본 프로그램으로 열기

### `verifyArchive`
모달 진행 표시와 함께 `Archive::verify()`. Native 스키마는 원본 바이트 그대로 저장되므로, 모든 레코드의 가독성을 확인한다.

### `compactArchive`
- 확인 대화상자 (작업 중 사용 불가 안내)
- 정리 전후 파일 크기를 `QLocale::formattedDataSize`로 표시
- 취소 버튼 없는 모달 진행

### `configureShell`
MSI 설치 여부(`isMachineInstalled`) → 안내만 / HKCU 설치(`isInstalled`) → 제거 옵션 / 미설치 → 설치 옵션. → [[06·Windows 셸 통합]]

---

## 🧮 상태 표시줄 (`refresh`)

```
<N>개 파일 · <원본 총 크기> · <파일 경로>
```

---

**다음:** [[05·CLI 자동화]] · **이전:** [[03·Archive 코어 API]] · **MOC:** [[LMDB Archiver (MOC)]]
