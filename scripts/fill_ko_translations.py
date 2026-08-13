#!/usr/bin/env python3
# One-off: fill Korean <translation> entries in translations/app_ko.ts.
# Source strings are English (after translate_sources.py); Korean goes here.
import io, sys
import xml.etree.ElementTree as ET

# en -> ko. Newlines are real newlines (lupdate turns the C "\n" into a newline
# inside <source>), so multi-line entries use real newlines here too.
NL = "\n"
en2ko = {
# mainwindow.cpp
"%1 files · %2 · %3": "%1개 파일 · %2 · %3",
"Copied %1 item(s). Paste in Explorer.": "%1개 항목을 복사했습니다. Explorer에서 붙여넣으세요.",
"About LMDB Archiver": "LMDB Archiver 정보",
"LMDB archive (*.lmdb);;All files (*.*)": "LMDB 아카이브 (*.lmdb);;모든 파일 (*.*)",
"LMDB archive (*.lmdb)": "LMDB 아카이브 (*.lmdb)",
"LMDB Archive Manager": "LMDB 아카이브 관리자",
"Open LMDB Archive": "LMDB 아카이브 열기",
"Windows Explorer Integration...": "Windows 탐색기 통합...",
"Windows Explorer Integration": "Windows 탐색기 통합",
"The verification was cancelled.": "검사 작업이 취소되었습니다.",
"E&xit": "끝내기(&X)",
"&Tools": "도구(&T)",
"&Help": "도움말(&H)",
"E&xtract All...": "모두 풀기(&X)...",
"Folder to extract everything to": "모든 항목을 풀 폴더",
"All entries were read successfully.": "모든 항목의 데이터가 정상적으로 읽혔습니다.",
"Saving changes…": "변경 사항 저장 중…",
"Remove unused LMDB pages to shrink the file?" + NL + "The archive is unavailable during this operation.": "사용하지 않는 LMDB 페이지를 제거해 파일 크기를 줄일까요?" + NL + "작업 중에는 아카이브를 사용할 수 없습니다.",
"Delete": "삭제",
"New LMDB Archive": "새 LMDB 아카이브",
"&New Archive...": "새 아카이브(&N)...",
"Create a new archive or open an existing .lmdb file.": "새 아카이브를 만들거나 기존 .lmdb 파일을 여세요.",
"Refresh": "새로 고침",
"New": "새로 만들기",
"&Delete Selected": "선택 항목 삭제(&R)",
"Open Selected": "선택 항목 열기",
"&Extract Selected...": "선택 항목 풀기(&E)...",
"&Copy Selected to Explorer": "선택 항목을 Explorer로 복사(&C)",
"Folder to extract the selection to": "선택 항목을 풀 폴더",
"Extracted the selection.": "선택 항목을 풀었습니다.",
"Delete the %1 selected item(s) from the archive?" + NL + "This cannot be undone.": "선택한 %1개 항목을 아카이브에서 삭제할까요?" + NL + "이 작업은 되돌릴 수 없습니다.",
"Archive &Test": "아카이브 검사(&T)",
"Verify Archive": "아카이브 검사",
"Verifying archive records...": "아카이브 레코드를 검사하는 중...",
"&Compact Archive": "아카이브 압축 정리(&C)",
"Compact Archive": "아카이브 압축 정리",
"Archive size: %1 → %2": "아카이브 크기: %1 → %2",
"&Archive": "아카이브(&A)",
"Extracted the whole archive.": "아카이브를 모두 풀었습니다.",
"Safely rewriting the archive...": "아카이브를 안전하게 다시 작성하는 중...",
"Open or create an archive. You can drag files and folders into the window.": "아카이브를 열거나 새로 만드세요. 파일과 폴더를 창으로 끌어 놓을 수 있습니다.",
"Search archive...": "아카이브에서 검색...",
"Extracting entries from the archive...": "아카이브에서 항목을 푸는 중...",
"Compaction complete": "압축 정리 완료",
"Could not open the file with the associated program.": "연결된 프로그램으로 파일을 열 수 없습니다.",
"&Open...": "열기(&O)...",
"Open": "열기",
"Done": "완료",
"Could not create a temporary export folder.": "임시 내보내기 폴더를 만들 수 없습니다.",
"Main Toolbar": "주 도구 모음",
"Processing…": "처리 중…",
"G&zip on Add": "추가 시 gzip 압축(&Z)",
"The add operation was cancelled.": "추가 작업이 취소되었습니다.",
"Files to add": "추가할 파일",
"Folder to add": "추가할 폴더",
"Cancel": "취소",
"No files on the clipboard to add.": "클립보드에 추가할 파일이 없습니다.",
"&Paste Files": "클립보드의 파일 추가(&P)",
"Explorer integration installed.": "탐색기 통합을 설치했습니다.",
"Explorer integration removed.": "탐색기 통합을 제거했습니다.",
"Explorer integration is installed system-wide by the MSI package." + NL + "To change or remove it, manage LMDB Archiver from Windows Installed apps.": "탐색기 통합이 MSI 설치 패키지에 의해 시스템 전체에 설치되어 있습니다." + NL + "변경하거나 제거하려면 Windows의 설치된 앱에서 LMDB Archiver를 관리하세요.",
"Explorer integration is installed. Remove it?": "탐색기 통합이 설치되어 있습니다. 제거할까요?",
"Collecting files…": "파일 수집 중…",
"&Add File...": "파일 추가(&A)...",
"Add File": "파일 추가",
"&File": "파일(&F)",
"&Edit": "편집(&E)",
"Ad&d Folder...": "폴더 추가(&D)...",
"Add Folder": "폴더 추가",
"The extract operation was cancelled.": "풀기 작업이 취소되었습니다.",
"Extract": "풀기",
"Delete Items": "항목 삭제",
"Adding entries to the archive...": "항목을 아카이브에 추가하는 중...",
"Install the .lmdb association and file/folder context menus for the current user?" + NL + "No administrator privileges required.": "현재 사용자 계정에 .lmdb 연결과 파일·폴더 우클릭 메뉴를 설치할까요?" + NL + "관리자 권한은 필요하지 않습니다.",
"When enabled, files are stored compressed with standard gzip on add. The key self-describes this with a \".<hash>.gz\" marker.": "켜면 파일을 추가할 때 표준 gzip으로 압축하여 저장합니다. 키가 \".<해시>.gz\" 표시로 자기 서술됩니다.",
"<h2>LMDB Archiver</h2><p>A fast, reliable LMDB-based desktop archive manager</p><p>Version %1 · Qt %2 · LMDB 0.9.33</p><p>MIT License</p>": "<h2>LMDB Archiver</h2><p>빠르고 안정적인 LMDB 기반 데스크톱 아카이브 관리자</p><p>버전 %1 · Qt %2 · LMDB 0.9.33</p><p>MIT 라이선스</p>",
# app.cpp
"LMDB-based archive manager": "LMDB 기반 아카이브 관리자",
"Install Windows Explorer integration": "Windows 탐색기 통합 설치",
"Remove Windows Explorer integration": "Windows 탐색기 통합 제거",
"Choose a target archive to add the path to": "대상 아카이브를 선택해 경로 추가",
"Extract the archive in place": "아카이브를 같은 위치에 풀기",
"Created the archive." + NL + "%1": "아카이브를 만들었습니다." + NL + "%1",
"Extracted the archive." + NL + "%1": "아카이브를 풀었습니다." + NL + "%1",
".lmdb file to open": "열 .lmdb 파일",
"LMDB archive to add to": "추가할 LMDB 아카이브",
"Create an archive from a file or folder": "파일 또는 폴더로 아카이브 생성",
# archivemodel.cpp
"Date modified": "수정한 날짜",
"Compressed": "압축",
"Name": "이름",
"Stored size": "저장 크기",
"Size": "크기",
"File": "파일",
"Folder": "폴더",
"Type": "형식",
# language-neutral / proper nouns (identity)
"LMDB Archiver": "LMDB Archiver",
NL + "%1 / %2 (%3%)": NL + "%1 / %2 (%3%)",
# language menu
"Language": "언어",
"English": "English",
"The language will change after restarting the app. Restart now?": "앱을 다시 시작한 후 언어가 변경됩니다. 지금 다시 시작할까요?",
}

ET.register_namespace("", "ts")  # not strictly needed
path = sys.argv[1] if len(sys.argv) > 1 else "translations/app_ko.ts"
tree = ET.parse(path)
root = tree.getroot()
filled = 0
missing = []
for msg in root.iter("message"):
    src_el = msg.find("source")
    tr_el = msg.find("translation")
    if src_el is None or tr_el is None:
        continue
    src = "".join(src_el.itertext()) if list(src_el) else (src_el.text or "")
    if src in en2ko:
        tr_el.text = en2ko[src]
        for sub in list(tr_el):
            tr_el.remove(sub)
        if "type" in tr_el.attrib:
            del tr_el.attrib["type"]
        filled += 1
    else:
        missing.append(src)

# Write back with the lupdate-style preamble.
header = '<?xml version="1.0" encoding="utf-8"?>\n<!DOCTYPE TS>\n'
body = ET.tostring(root, encoding="unicode")
with io.open(path, "w", encoding="utf-8", newline="\n") as f:
    f.write(header + body + "\n")
print("filled:", filled)
print("missing:", len(missing))
for m in missing:
    print("  MISSING:", repr(m))
