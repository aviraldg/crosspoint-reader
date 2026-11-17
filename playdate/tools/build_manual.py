#!/usr/bin/env python3
"""Build the CrossPoint Playdate manual EPUB.

The manual combines hand-written chapters from playdate/manual/chapters with
licence chapters generated from the notice files listed in
playdate/manual/licenses.json. Licence text is always read from the source
files, so notices stay in step with the code that ships.

The output is deterministic for the same inputs. A short content hash is
written beside the EPUB so the firmware can tell when to replace the copy on
the Data disk.
"""

from __future__ import annotations

import argparse
import hashlib
import html
import json
import os
import re
import subprocess
import sys
import xml.etree.ElementTree as ET
import zipfile
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
NOTICE_NAMES = ("LICENSE", "LICENCE", "NOTICE", "COPYING")
ZIP_DATE = (1980, 1, 1, 0, 0, 0)
BOOK_UID = "urn:crosspoint:playdate-manual"


@dataclass
class Chapter:
    file_id: str
    title: str
    body: str


def fail(message: str) -> None:
    print(f"build_manual: error: {message}", file=sys.stderr)
    sys.exit(1)


def git(*args: str) -> str:
    try:
        return subprocess.check_output(
            ["git", *args], cwd=REPO_ROOT, text=True, stderr=subprocess.DEVNULL
        ).strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def read_text(path: Path) -> str:
    if not path.is_file():
        fail(f"missing input file: {path}")
    return path.read_text(encoding="utf-8-sig", errors="replace")


def read_pdxinfo(path: Path) -> dict[str, str]:
    info = {}
    for line in read_text(path).splitlines():
        key, sep, value = line.partition("=")
        if sep:
            info[key.strip()] = value.strip()
    return info


def text_to_xhtml(text: str) -> str:
    """Turn plain notice text into paragraphs that reflow on a small screen."""
    out = []
    for block in re.split(r"\n\s*\n", text.replace("\r\n", "\n")):
        lines = [line.strip() for line in block.splitlines()]
        # Drop ruler lines such as "=====" or "-----".
        lines = [line for line in lines if line and not re.fullmatch(r"[=\-_*#~]{3,}", line)]
        if lines:
            out.append(f"<p>{html.escape(' '.join(lines), quote=False)}</p>")
    return "\n".join(out)


def subsection(title: str, body_xhtml: str) -> str:
    return f"<h2>{html.escape(title, quote=False)}</h2>\n{body_xhtml}\n"


def fetchcontent_deps(cmake_path: Path) -> list[dict[str, str]]:
    source = read_text(cmake_path)
    deps = []
    for match in re.finditer(r"FetchContent_Declare\(\s*(\w+)(.*?)\)", source, re.DOTALL):
        args = match.group(2)
        repo = re.search(r"GIT_REPOSITORY\s+(\S+)", args)
        tag = re.search(r"GIT_TAG\s+(\S+)", args)
        url = re.search(r"\bURL\s+(\S+)", args)
        deps.append({
            "name": match.group(1),
            "source": repo.group(1) if repo else (url.group(1) if url else "unknown"),
            "revision": tag.group(1) if tag else "",
        })
    if not deps:
        fail(f"no FetchContent_Declare entries found in {cmake_path}")
    return deps


def fetchcontent_section(cmake_path: Path, deps_dir: Path) -> str:
    body = ""
    for dep in fetchcontent_deps(cmake_path):
        src_dir = deps_dir / f"{dep['name'].lower()}-src"
        if not src_dir.is_dir():
            fail(f"FetchContent source for {dep['name']} not found at {src_dir}; build first")
        notices = sorted(
            p for p in src_dir.iterdir()
            if p.is_file() and p.name.upper().split(".")[0] in NOTICE_NAMES
        )
        if not notices:
            fail(f"no licence file found for {dep['name']} in {src_dir}")
        source_line = dep["source"] + (f" at {dep['revision']}" if dep["revision"] else "")
        body += subsection(dep["name"], f"<p>Source: {html.escape(source_line, quote=False)}</p>")
        for notice in notices:
            body += text_to_xhtml(read_text(notice)) + "\n"
    return body


def source_notice_section(patterns: list[str]) -> str:
    seen = set()
    body = ""
    for pattern in patterns:
        paths = sorted(REPO_ROOT.glob(pattern))
        if not paths:
            fail(f"source notice pattern matched no files: {pattern}")
        for path in paths:
            source = read_text(path)
            for match in re.finditer(r"/\*.*?\*/", source, re.DOTALL):
                notice = match.group()
                if not re.search(r"copyright|permission is|ALTERNATIVE [AB]", notice, re.IGNORECASE):
                    continue
                if notice in seen:
                    continue
                seen.add(notice)
                cleaned = "\n".join(
                    re.sub(r"^\s*(/\*+|\*+/|\*)\s?", "", line) for line in notice.splitlines()
                )
                body += subsection(str(path.relative_to(REPO_ROOT)), text_to_xhtml(cleaned))
    return body


def licence_chapters(manifest_path: Path, deps_dir: Path) -> list[tuple[str, str]]:
    manifest = json.loads(read_text(manifest_path))
    chapters = []
    for group in manifest["groups"]:
        body = ""
        if group.get("credit"):
            body += f"<p><b>{html.escape(group['credit'], quote=False)}</b></p>\n"
        for rel in group.get("files", []):
            body += subsection(rel, text_to_xhtml(read_text(REPO_ROOT / rel)))
        if group.get("fetchcontent"):
            body += fetchcontent_section(REPO_ROOT / group["fetchcontent"], deps_dir)
        if group.get("source_notices"):
            body += source_notice_section(group["source_notices"])
        if not body:
            fail(f"licence group '{group['title']}' produced no text")
        chapters.append((group["title"], body))
    return chapters


def render_template(text: str, values: dict[str, str]) -> str:
    def replace(match: re.Match[str]) -> str:
        key = match.group(1)
        if key not in values:
            fail(f"unknown template variable {{{{{key}}}}}")
        return html.escape(values[key], quote=False)

    return re.sub(r"\{\{\s*(\w+)\s*\}\}", replace, text)


def written_chapters(chapters_dir: Path, values: dict[str, str]) -> list[tuple[str, str]]:
    chapters = []
    for path in sorted(chapters_dir.glob("*.xhtml")):
        body = render_template(read_text(path), values)
        title = re.search(r"<h1[^>]*>(.*?)</h1>", body, re.DOTALL)
        if not title:
            fail(f"{path.name} needs an <h1> title")
        chapters.append((re.sub(r"<[^>]+>", "", title.group(1)).strip(), body))
    if not chapters:
        fail(f"no chapters found in {chapters_dir}")
    return chapters


def xhtml_document(title: str, body: str) -> str:
    doc = (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        '<html xmlns="http://www.w3.org/1999/xhtml">\n'
        f"<head><title>{html.escape(title, quote=False)}</title></head>\n"
        f"<body>\n{body}\n</body>\n</html>\n"
    )
    try:
        ET.fromstring(doc.split("\n", 1)[1])
    except ET.ParseError as err:
        fail(f"chapter '{title}' is not well-formed XHTML: {err}")
    return doc


def build_epub(chapters: list[Chapter], title: str, author: str, language: str) -> bytes:
    manifest = "\n".join(
        f'    <item id="{c.file_id}" href="{c.file_id}.xhtml" media-type="application/xhtml+xml"/>'
        for c in chapters
    )
    spine = "\n".join(f'    <itemref idref="{c.file_id}"/>' for c in chapters)
    nav = "\n".join(
        f'    <navPoint id="nav-{i}" playOrder="{i}">'
        f"<navLabel><text>{html.escape(c.title, quote=False)}</text></navLabel>"
        f'<content src="{c.file_id}.xhtml"/></navPoint>'
        for i, c in enumerate(chapters, start=1)
    )
    esc_title = html.escape(title, quote=False)
    files = {
        "META-INF/container.xml": (
            '<?xml version="1.0" encoding="UTF-8"?>\n'
            '<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">\n'
            '  <rootfiles><rootfile full-path="OEBPS/content.opf" '
            'media-type="application/oebps-package+xml"/></rootfiles>\n'
            "</container>\n"
        ),
        "OEBPS/content.opf": (
            '<?xml version="1.0" encoding="UTF-8"?>\n'
            '<package xmlns="http://www.idpf.org/2007/opf" unique-identifier="bookid" version="2.0">\n'
            '  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">\n'
            f"    <dc:title>{esc_title}</dc:title>\n"
            f"    <dc:creator>{html.escape(author, quote=False)}</dc:creator>\n"
            f"    <dc:language>{language}</dc:language>\n"
            f'    <dc:identifier id="bookid">{BOOK_UID}</dc:identifier>\n'
            "  </metadata>\n"
            "  <manifest>\n"
            '    <item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>\n'
            f"{manifest}\n"
            "  </manifest>\n"
            f'  <spine toc="ncx">\n{spine}\n  </spine>\n'
            "</package>\n"
        ),
        "OEBPS/toc.ncx": (
            '<?xml version="1.0" encoding="UTF-8"?>\n'
            '<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">\n'
            f'  <head><meta name="dtb:uid" content="{BOOK_UID}"/></head>\n'
            f"  <docTitle><text>{esc_title}</text></docTitle>\n"
            f"  <navMap>\n{nav}\n  </navMap>\n"
            "</ncx>\n"
        ),
    }
    for c in chapters:
        files[f"OEBPS/{c.file_id}.xhtml"] = xhtml_document(c.title, c.body)

    from io import BytesIO

    buffer = BytesIO()
    with zipfile.ZipFile(buffer, "w") as archive:
        # EPUB requires an uncompressed "mimetype" entry first.
        info = zipfile.ZipInfo("mimetype", ZIP_DATE)
        info.compress_type = zipfile.ZIP_STORED
        archive.writestr(info, "application/epub+zip")
        for name, content in files.items():
            info = zipfile.ZipInfo(name, ZIP_DATE)
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, content.encode("utf-8"))
    return buffer.getvalue()


def main() -> None:
    manual_dir = REPO_ROOT / "playdate" / "manual"
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--deps-dir", type=Path, required=True,
                        help="CMake FetchContent _deps directory from a configured build")
    parser.add_argument("--output", type=Path, required=True, help="EPUB output path")
    parser.add_argument("--stamp", type=Path, required=True, help="content hash output path")
    parser.add_argument("--pdxinfo", type=Path, default=REPO_ROOT / "playdate" / "Source" / "pdxinfo")
    parser.add_argument("--chapters", type=Path, default=manual_dir / "chapters")
    parser.add_argument("--licenses", type=Path, default=manual_dir / "licenses.json")
    parser.add_argument("--repository", default=os.environ.get("GITHUB_REPOSITORY", ""),
                        help="owner/name of the source repository (default: origin remote)")
    args = parser.parse_args()

    pdxinfo = read_pdxinfo(args.pdxinfo)
    repository = args.repository
    if not repository:
        match = re.search(r"github\.com[:/](.+?)(?:\.git)?$", git("remote", "get-url", "origin"))
        repository = match.group(1) if match else ""
    if not repository:
        fail("cannot determine the source repository; pass --repository owner/name")
    # Keep values stable between commits: the content hash decides when devices
    # replace their copy, which also resets reading progress in the manual.
    values = {
        "app_name": pdxinfo.get("name", "CrossPoint Reader"),
        "bundle_id": pdxinfo.get("bundleID", ""),
        "version": pdxinfo.get("version", ""),
        "sdk_revision": git("rev-parse", "HEAD:freeink-sdk"),
        "source_url": f"https://github.com/{repository}",
    }

    chapters = []
    for title, body in written_chapters(args.chapters, values):
        chapters.append(Chapter(f"ch{len(chapters) + 1:02d}", title, body))
    for title, body in licence_chapters(args.licenses, args.deps_dir):
        heading = f"Licences: {title}"
        chapters.append(Chapter(f"ch{len(chapters) + 1:02d}", heading,
                                f"<h1>{html.escape(heading, quote=False)}</h1>\n{body}"))

    data = build_epub(chapters, f"{values['app_name']} Manual", values["app_name"] + " contributors", "en")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(data)
    args.stamp.write_text(hashlib.sha256(data).hexdigest()[:16] + "\n", encoding="ascii")
    print(f"build_manual: wrote {args.output} ({len(data)} bytes, {len(chapters)} chapters)")


if __name__ == "__main__":
    main()
