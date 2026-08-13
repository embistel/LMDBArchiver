---
tags: [project, cli, automation]
up: "[[LMDB Archiver (MOC)]]"
created: 2026-08-12
---

# 05 · CLI 자동화

> [!info] 위치
> `src/cli/main.cpp` · 실행파일: `LMDBArchiverCLI.exe`
> 코어만 링크 (Qt6::Core, `lmdbarchiver_core`). UI 없음. 스크립트/CI용.

## 명령 요약

```
LMDBArchiverCLI <command> <archive.lmdb> [args...] [--destination <path>]
```

| 명령 | 인자 | 동작 |
|---|---|---|
| `create` | `<archive.lmdb> <path>...` | 새 아카이브 생성 (덮어쓰기 거부) |
| `add` | `<archive.lmdb> <path>...` | 기존 아카이브에 추가 |
| `list` | `<archive.lmdb>` | 항목 나열 |
| `extract` | `<archive.lmdb> <out-dir> [internal-path]...` | 추출 (경로 생략 시 전체) |
| `remove` | `<archive.lmdb> <internal-path>...` | 항목 삭제 |
| `test` | `<archive.lmdb>` | 무결성 검사 → `OK` |
| `compact` | `<archive.lmdb>` | 빈 페이지 회수 |

### 공통 옵션
- `-d, --destination <path>` — `add` 시 내부 목적지 경로
- `--help`, `--version` (Qt 기본)

---

## 예시

```powershell
# 1. 새 아카이브 생성
LMDBArchiverCLI create photos.lmdb C:\Photos

# 2. 하위 경로에 추가
LMDBArchiverCLI add photos.lmdb C:\More --destination imports

# 3. 목록 (d=디렉터리, f=파일 / 원본 / 저장 / 경로)
LMDBArchiverCLI list photos.lmdb
# d  0       120   photos
# f  1048576  ...   photos/IMG_0001.jpg

# 4. 전체 또는 일부 추출
LMDBArchiverCLI extract photos.lmdb C:\Restored
LMDBArchiverCLI extract photos.lmdb C:\Restored imports

# 5. 항목 삭제
LMDBArchiverCLI remove photos.lmdb imports\obsolete.jpg

# 6. 검증 + 정리
LMDBArchiverCLI test photos.lmdb    # → OK
LMDBArchiverCLI compact photos.lmdb
```

---

## 동작 세부

### `create`
- 인자 최소 2개(아카이브 + 1개 경로) 필요, 부족 시 usage + 종료 코드 2
- 대상 파일이 **이미 존재하면 거부** (`Refusing to overwrite...`) → 종료 1
- 성공 시 표준 출력으로 아카이브 절대경로 출력

### `add`
- `--destination` 값을 `Archive::addPaths`에 전달
- 동일 내부 경로 재추가 = **덮어쓰기** (코어가 기존 청크 제거 후 재기록)

### `list`
출력 형식 (탭 구분):
```
<type>	<originalSize>	<storedSize>	<path>
```
- `type`: `d` (디렉터리) / `f` (파일)

### `extract`
- 첫 번째 인자 = 출력 디렉터리
- 이후 인자가 있으면 해당 내부 경로(및 하위)만, 없으면 **전체**
- 코어가 경로 순회 방지 + SHA-256 검증 수행

### `test`
- `Archive::verify()` 실패 시 표준 오류로 메시지 + 종료 1
- 성공 시 `OK` 출력 + 종료 0

### 종료 코드
| 코드 | 의미 |
|---|---|
| 0 | 성공 |
| 1 | 런타임 오류 (`fail()`) |
| 2 | 인자 부족/알 수 없는 명령 (usage 출력) |

### 진행률 표시 (TTY)
`create`, `add`, `extract`, `test` 명령은 stderr가 **터미널(TTY)**일 때 한 줄 캐리지 리턴 갱신으로 진행률을 표시한다. 리다이렉트/파이프 시엔 출력하지 않아 로그가 깨끗하다.

```
\r[scan] 1.2MB · Photos
\r45% · 4.8GB/10.0GB · Photos/IMG_0001.jpg
\r[commit] 10.0GB/10.0GB
```

- `ProgressPhase`에 따라 접두 `[scan]` / `[commit]` 표시
- `bytesTotal`이 0이면 누적 바이트만
- 작업 완료 후 줄바꿈 한 번

---

## GUI 실행파일의 숨은 CLI 모드

`LMDBArchiver.exe`(GUI)도 탐색기 메뉴용 옵션을 인식한다 → [[04·Qt Widgets GUI#App::run — CLI 옵션 라우팅]].

| 옵션 | 용도 |
|---|---|
| `--create-from <path>` | 폴더 우클릭 "LMDB 아카이브로 묶기" |
| `--extract-here <archive>` | `.lmdb` 우클릭 "여기에 풀기" |
| `--add-dialog <path>` | 파일 우클릭 "LMDB 아카이브에 추가…" |
| `--install-shell` / `--uninstall-shell` | 통합 설치/제거 |

> [!tip] 배포 패키지
> 두 실행파일 모두 동일한 코어를 사용하므로 동작이 일관된다. [[08·패키징과 배포]] 참고.

---

**다음:** [[06·Windows 셸 통합]] · **이전:** [[04·Qt Widgets GUI]] · **MOC:** [[LMDB Archiver (MOC)]]
