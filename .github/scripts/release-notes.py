#!/usr/bin/env python3
"""Writes the description of the GitHub Release for one firmware version.

The description is what CHANGELOG.md says under the version's heading, such as
"## 0.2.0", followed by .github/release-notes.md with {version} filled in.
Exits 1, with an error GitHub Actions shows on CHANGELOG.md, when the changelog
has nothing under that version.

Usage: release-notes.py VERSION [OUTPUT_FILE]
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def changes(version: str) -> str:
    """The lines under the version's heading, up to the next "## " heading."""
    # Also takes "## v0.2.0", "## [0.2.0]" and a date after the number.
    heading = re.compile(r"^##\s+\[?v?" + re.escape(version) + r"\]?(\s|$)")
    section = None

    for line in (ROOT / "CHANGELOG.md").read_text(encoding="utf-8").splitlines():
        if section is None:
            if heading.match(line):
                section = []
        elif line.startswith("## "):
            break
        else:
            section.append(line)

    return "\n".join(section or []).strip()


def main() -> int:
    if len(sys.argv) not in (2, 3):
        print(__doc__.strip().splitlines()[-1], file=sys.stderr)
        return 2

    version = sys.argv[1]
    text = changes(version)
    if not text:
        print(
            f"::error file=CHANGELOG.md::CHANGELOG.md has nothing under a '## {version}' heading. "
            f"Add what {version} changes there before releasing it."
        )
        return 1

    template = (ROOT / ".github" / "release-notes.md").read_text(encoding="utf-8")
    notes = template.replace("{version}", version).replace("{changes}", text)

    if len(sys.argv) == 3:
        Path(sys.argv[2]).write_text(notes, encoding="utf-8")
    else:
        sys.stdout.write(notes)

    return 0


if __name__ == "__main__":
    sys.exit(main())
