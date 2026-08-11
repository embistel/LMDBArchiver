# 개발자 안내서

## 구조

- `src/archive`: Qt Core + LMDB만 사용하는 저장/압축/추출 코어
- `src/app`: Qt Widgets UI, 트리 모델, 외부 파일 드롭
- `src/platform`: Windows 현재 사용자 셸 통합
- `tests`: 임시 디렉터리를 사용하는 왕복/교체/삭제 테스트

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
