---
tags: [project, api, core, lmdb]
up: "[[LMDB Archiver (MOC)]]"
created: 2026-08-12
---

# 03 · Archive 코어 API

> [!info] 위치
> `src/archive/archive.{h,cpp}` · `src/archive/archiveentry.h`
> 의존성: Qt6::Core + lmdb_vendor (정적). UI 없음.

## `ArchiveEntry` (POD)

```cpp
struct ArchiveEntry {
    QString path;
    bool directory = false;
    qint64 originalSize = 0;   // 원본 바이트
    qint64 storedSize = 0;     // LMDB 내 저장 바이트 (동적 계산)
    QDateTime modified;
    quint32 permissions = 0;   // QFile::permissions 비트
};
```

---

## `Archive` 클래스 — 공개 인터페이스

```cpp
class Archive final {
public:
    using Progress = std::function<bool(const ProgressInfo &)>;

    bool open(const QString &filePath, bool create, QString *error = nullptr);
    void close();
    bool isOpen() const;
    QString filePath() const;

    QList<ArchiveEntry> entries(QString *error = nullptr) const;
    bool clear(QString *error = nullptr);
    bool compact(QString *error = nullptr);
    bool verify(QString *error = nullptr, const Progress &progress = {}) const;

    bool addPaths(const QStringList &paths, const QString &destination = {},
                  QString *error = nullptr, const Progress &progress = {});
    bool removePaths(const QStringList &archivePaths, QString *error = nullptr);
    bool extract(const QStringList &archivePaths, const QString &destination,
                 QString *error = nullptr, const Progress &progress = {}) const;
};
```

> [!note] `ProgressInfo` 진행률 구조체
> 진행률 콜백은 바이트 기반 구조체를 받는다. `false` 반환 = **취소** → 트랜잭션 중단.
> ```cpp
> enum class ProgressPhase { Collecting, Processing, Finalizing };
> struct ProgressInfo {
>     ProgressPhase phase;
>     QString currentItem;
>     qint64 bytesDone, bytesTotal;        // 0 == 알 수 없음
>     qint64 itemBytesDone, itemBytesTotal; // 현재 파일 내 진행
> };
> ```
> - `Collecting`: 사전 수집(디렉터리 순회)
> - `Processing`: 4 MiB 청크마다 바이트 갱신 → 큰 파일도 부드러운 진행률
> - `Finalizing`: 트랜잭션 커밋/디렉터리 메타데이터 복원 직전 → UI busy 스피너
> - `extract`/`verify`는 처리 전 한 번 순회해 `bytesTotal`을 합산한다.

---

## `open` — 아카이브 열기/생성

```cpp
bool open(const QString &filePath, bool create, QString *error);
```

1. 기존 열려 있으면 `close()`
2. `create=false`이면 파일 존재 검증
3. `mdb_env_create` + `mdb_env_set_maxdbs(2)`
4. **map size 계산**: 초기 64 MiB, 기존 파일 크기 이상까지 2배
5. `mdb_env_open(..., MDB_NOSUBDIR, 0664)` — `.lmdb`를 파일 자체로 취급
6. 초기화 트랜잭션으로 DBI 열기

> `MDB_NOSUBDIR` 덕분에 `data.mdb` 같은 자식 파일이 아니라 `.lmdb` 파일 **자체**가 DB가 된다.

---

## `addPaths` — 파일/폴더 추가 (쓰기)

```cpp
bool addPaths(const QStringList &paths, const QString &destination = {},
              QString *error = nullptr, const Progress &progress = {});
```

**절차:**
1. **수집 단계** — 각 루트와 (디렉터리면) 하위 항목을 순회해 `pending` 리스트 생성. 동시에 총 바이트 계산.
2. **용량 확보** — `ensureCapacity(bytes + pending.size() * 8KB)` 로 map size 사전 확장
3. **단일 쓰기 트랜잭션** 안에서:
   - 진행 콜백 호출 (취소 확인)
   - 키 길이 상한 검증 (`mdb_env_get_maxkeysize`)
   - 기존 항목 제거 (`deleteStoredEntry`)
   - 파일이면 4 MiB 청크로 읽어 `encodeChunk` → `mdb_put`
   - 읽은 크기 ≠ `info.size()`면 **중단** (동시 수정 감지)
   - entry 헤더 `mdb_put`
4. **커밋** — 성공 시 `mdb_txn_commit`, 실패 시 `mdb_txn_abort`

> [!important] 원자성
> 모든 청크와 메타데이터가 **하나의 트랜잭션**. 일부만 들어가는 일은 없다.

---

## `extract` — 추출 (읽기 전용)

```cpp
bool extract(const QStringList &archivePaths, const QString &destination,
             QString *error = nullptr, const Progress &progress = {}) const;
```

- 읽기 전용 트랜잭션(`MDB_RDONLY`)
- `archivePaths`가 비어 있으면 **전체 추출**, 그렇지 않으면 일치/접두 매칭
- **경로 순회 방지**: 정규화 후 `destinationRoot` 하위인지 검증 (Win: 대소문자 무시)
- 디렉터리는 `mkpath`, 파일은 `QSaveFile` (원자적 rename via `commit()`)
- 디렉터리의 수정 시간/권한은 **가장 나중에** 일괄 적용 (하위 파일이 시간을 덮어쓰지 않도록 깊은 경로순 정렬)

---

## `removePaths` — 항목 삭제

```cpp
bool removePaths(const QStringList &archivePaths, QString *error = nullptr);
```

- `entries()`로 전체 목록 확보
- 각 항목이 요청 경로 또는 그 하위면 `deleteStoredEntry`
- 단일 트랜잭션 커밋

> `removePaths({"docs"})`는 `docs/`, `docs/a.txt`, `docs/sub/b.txt` 모두 삭제.

---

## `verify` — 무결성 검사 (읽기 전용)

```cpp
bool verify(QString *error = nullptr, const Progress &progress = {}) const;
```

- 커서 순회하며 모든 value가 읽기 가능한지 확인
- Native 스키마는 원본 바이트 그대로 저장되므로, 가독성 검증이 무결성 확인의 전부
- SHA-256 검증은 없음 (헤더·해시를 저장하지 않으므로)

→ [[09·테스트#readabilityCheck]]에서 모든 레코드의 가독성을 확인.

---

## `compact` — 페이지 회수

```cpp
bool compact(QString *error = nullptr);
```

LMDB는 삭제 후에도 빈 페이지를 파일에 남긴다. `compact`는:
1. `mdb_env_copy2(..., MDB_CP_COMPACT)` 로 **새 파일**에 compact 사본 생성
2. 현재 환경 `close()`
3. `replaceWithBackup()` — Windows: `ReplaceFileW` (원자적 교체 + 백업), POSIX: rename 기반
4. 다시 `open()`
5. **롤백 보장**: 재개방 실패 시 백업에서 복구

> [!warning] 작업 중 아카이브 사용 불가
> compact는 close → 복사 → 교체 → 재개방 과정을 거치므로 GUI에서 모달 진행 표시.

→ [[09·테스트#compactArchive]]에서 삭제 후 파일 크기 감소 검증.

---

## `clear` — 전체 비우기

```cpp
bool clear(QString *error = nullptr);
```

`mdb_drop(txn, dbi, 0)` 로 모든 레코드 삭제. DBI 자체는 유지.

---

## 🔒 트랜잭션과 취소

```cpp
// 콜백이 false를 반환하면 rc = MDB_BAD_TXN → 루프 중단 → abort
if (progress && !progress(pending[i].target, i, pending.size())) { rc = MDB_BAD_TXN; break; }
```

취소 시:
- 루프 탈출 → `mdb_txn_abort` → **아카이브는 이전 커밋 상태 그대로**
- 부분 기록이 남지 않음

→ [[09·테스트#transactionalCancellation]]에서 3번째 콜백에 취소를 요청해 원본이 보존되는지 검증.

---

## 🛡️ 경로 안전성

두 겹의 방어:
1. **저장 시** `cleanArchivePath` — `..`, `.` 정리 후 거부
2. **추출 시** 목적지 루트 하위인지 다시 검증 (Win은 대소문자 무시):
   ```cpp
   if (!normalized.startsWith(destinationRoot + '/', pathCase)) → MDB_INVALID
   ```

---

## 🧭 내부 헬퍼 (익명 namespace)

| 함수 | 역할 |
|---|---|
| `lmdbError(int)` | `mdb_strerror` → `QString` |
| `ancestorDirectories` | 경로에서 상위 디렉터리 집합 생성 (트리뷰 가상 폴더용) |
| `nativePathForLmdb` | LMDB용 네이티브 절대경로 |
| `replaceWithBackup` | Windows `ReplaceFileW` / POSIX rename |

---

**다음:** [[04·Qt Widgets GUI]] · **이전:** [[02·아카이브 포맷 v2]] · **MOC:** [[LMDB Archiver (MOC)]]
