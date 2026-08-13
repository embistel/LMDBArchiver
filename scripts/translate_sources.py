#!/usr/bin/env python3
# One-off: convert Korean tr() source strings to English (idiomatic Qt i18n).
# Replaces the full tr("...") token so substring collisions are impossible.
import io, sys

# (Korean inner text exactly as in source, English inner text)
pairs = [
# mainwindow.cpp
("%1개 파일 · %2 · %3", "%1 files · %2 · %3"),
("%1개 항목을 복사했습니다. Explorer에서 붙여넣으세요.", "Copied %1 item(s). Paste in Explorer."),
("LMDB Archiver 정보", "About LMDB Archiver"),
("LMDB 아카이브 (*.lmdb);;모든 파일 (*.*)", "LMDB archive (*.lmdb);;All files (*.*)"),
("LMDB 아카이브 (*.lmdb)", "LMDB archive (*.lmdb)"),
("LMDB 아카이브 관리자", "LMDB Archive Manager"),
("LMDB 아카이브 열기", "Open LMDB Archive"),
("Windows 탐색기 통합...", "Windows Explorer Integration..."),
("Windows 탐색기 통합", "Windows Explorer Integration"),
("검사 작업이 취소되었습니다.", "The verification was cancelled."),
("끝내기(&X)", "E&xit"),
("도구(&T)", "&Tools"),
("도움말(&H)", "&Help"),
("모두 풀기(&X)...", "E&xtract All..."),
("모든 항목을 풀 폴더", "Folder to extract everything to"),
("모든 항목의 데이터가 정상적으로 읽혔습니다.", "All entries were read successfully."),
("변경 사항 저장 중…", "Saving changes…"),
("사용하지 않는 LMDB 페이지를 제거해 파일 크기를 줄일까요?\\n작업 중에는 아카이브를 사용할 수 없습니다.", "Remove unused LMDB pages to shrink the file?\\nThe archive is unavailable during this operation."),
("삭제", "Delete"),
("새 LMDB 아카이브", "New LMDB Archive"),
("새 아카이브(&N)...", "&New Archive..."),
("새 아카이브를 만들거나 기존 .lmdb 파일을 여세요.", "Create a new archive or open an existing .lmdb file."),
("새로 고침", "Refresh"),
("새로 만들기", "New"),
("선택 항목 삭제(&R)", "&Delete Selected"),
("선택 항목 열기", "Open Selected"),
("선택 항목 풀기(&E)...", "&Extract Selected..."),
("선택 항목을 Explorer로 복사(&C)", "&Copy Selected to Explorer"),
("선택 항목을 풀 폴더", "Folder to extract the selection to"),
("선택 항목을 풀었습니다.", "Extracted the selection."),
("선택한 %1개 항목을 아카이브에서 삭제할까요?\\n이 작업은 되돌릴 수 없습니다.", "Delete the %1 selected item(s) from the archive?\\nThis cannot be undone."),
("아카이브 검사(&T)", "Archive &Test"),
("아카이브 검사", "Verify Archive"),
("아카이브 레코드를 검사하는 중...", "Verifying archive records..."),
("아카이브 압축 정리(&C)", "&Compact Archive"),
("아카이브 압축 정리", "Compact Archive"),
("아카이브 크기: %1 → %2", "Archive size: %1 → %2"),
("아카이브(&A)", "&Archive"),
("아카이브를 모두 풀었습니다.", "Extracted the whole archive."),
("아카이브를 안전하게 다시 작성하는 중...", "Safely rewriting the archive..."),
("아카이브를 열거나 새로 만드세요. 파일과 폴더를 창으로 끌어 놓을 수 있습니다.", "Open or create an archive. You can drag files and folders into the window."),
("아카이브에서 검색...", "Search archive..."),
("아카이브에서 항목을 푸는 중...", "Extracting entries from the archive..."),
("압축 정리 완료", "Compaction complete"),
("연결된 프로그램으로 파일을 열 수 없습니다.", "Could not open the file with the associated program."),
("열기(&O)...", "&Open..."),
("열기", "Open"),
("완료", "Done"),
("임시 내보내기 폴더를 만들 수 없습니다.", "Could not create a temporary export folder."),
("주 도구 모음", "Main Toolbar"),
("처리 중…", "Processing…"),
("추가 시 gzip 압축(&Z)", "G&zip on Add"),
("추가 작업이 취소되었습니다.", "The add operation was cancelled."),
("추가할 파일", "Files to add"),
("추가할 폴더", "Folder to add"),
("취소", "Cancel"),
("클립보드에 추가할 파일이 없습니다.", "No files on the clipboard to add."),
("클립보드의 파일 추가(&P)", "&Paste Files"),
("탐색기 통합을 설치했습니다.", "Explorer integration installed."),
("탐색기 통합을 제거했습니다.", "Explorer integration removed."),
("탐색기 통합이 MSI 설치 패키지에 의해 시스템 전체에 설치되어 있습니다.\\n변경하거나 제거하려면 Windows의 설치된 앱에서 LMDB Archiver를 관리하세요.", "Explorer integration is installed system-wide by the MSI package.\\nTo change or remove it, manage LMDB Archiver from Windows Installed apps."),
("탐색기 통합이 설치되어 있습니다. 제거할까요?", "Explorer integration is installed. Remove it?"),
("파일 수집 중…", "Collecting files…"),
("파일 추가(&A)...", "&Add File..."),
("파일 추가", "Add File"),
("파일(&F)", "&File"),
("편집(&E)", "&Edit"),
("폴더 추가(&D)...", "Ad&d Folder..."),
("폴더 추가", "Add Folder"),
("풀기 작업이 취소되었습니다.", "The extract operation was cancelled."),
("풀기", "Extract"),
("항목 삭제", "Delete Items"),
("항목을 아카이브에 추가하는 중...", "Adding entries to the archive..."),
("현재 사용자 계정에 .lmdb 연결과 파일·폴더 우클릭 메뉴를 설치할까요?\\n관리자 권한은 필요하지 않습니다.", "Install the .lmdb association and file/folder context menus for the current user?\\nNo administrator privileges required."),
# app.cpp
("LMDB 기반 아카이브 관리자", "LMDB-based archive manager"),
("Windows 탐색기 통합 설치", "Install Windows Explorer integration"),
("Windows 탐색기 통합 제거", "Remove Windows Explorer integration"),
("대상 아카이브를 선택해 경로 추가", "Choose a target archive to add the path to"),
("아카이브를 같은 위치에 풀기", "Extract the archive in place"),
("아카이브를 만들었습니다.\\n%1", "Created the archive.\\n%1"),
("아카이브를 풀었습니다.\\n%1", "Extracted the archive.\\n%1"),
("열 .lmdb 파일", ".lmdb file to open"),
("추가할 LMDB 아카이브", "LMDB archive to add to"),
("파일 또는 폴더로 아카이브 생성", "Create an archive from a file or folder"),
# archivemodel.cpp
("수정한 날짜", "Date modified"),
("압축", "Compressed"),
("이름", "Name"),
("저장 크기", "Stored size"),
("크기", "Size"),
("파일", "File"),
("폴더", "Folder"),
("형식", "Type"),
]

def remap(text):
    count = 0
    for ko, en in pairs:
        old = 'tr("' + ko + '")'
        new = 'tr("' + en + '")'
        n = text.count(old)
        if n:
            text = text.replace(old, new)
            count += n
    return text, count

total = 0
for path in sys.argv[1:]:
    with io.open(path, encoding="utf-8") as f:
        src = f.read()
    out, c = remap(src)
    with io.open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(out)
    total += c
    print("%s: %d replacements" % (path, c))
print("TOTAL:", total)
