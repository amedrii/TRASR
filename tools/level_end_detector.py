#!/usr/bin/env python3
"""Recognise Tomb Raider: Anniversary's level-completion screen.

This is a prototype verifier, not an input bot.  It scans only the Tomb Raider
window, requires several completion-screen clues in consecutive frames, and
emits exactly one JSON event per visible result screen.  A future TRSR results
endpoint can be supplied with --webhook; until then events are written locally.
"""

from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
import json
import re
import sys
import time
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Optional
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

try:
    import mss
    import pytesseract
    from PIL import Image, ImageEnhance, ImageOps
except ImportError as exc:
    raise SystemExit(
        "Missing Python package: "
        f"{exc.name}. Install tools/requirements-level-end-detector.txt first."
    ) from exc


GAME_TITLE_FRAGMENT = "tomb raider: anniversary"
COMPLETION_PHRASES = ("completion time", "completed time trial")
CONTINUE_PHRASE = "continue"


class RECT(ctypes.Structure):
    _fields_ = (("left", wintypes.LONG), ("top", wintypes.LONG),
                ("right", wintypes.LONG), ("bottom", wintypes.LONG))


user32 = ctypes.WinDLL("user32", use_last_error=True)
user32.GetWindowTextLengthW.argtypes = (wintypes.HWND,)
user32.GetWindowTextLengthW.restype = ctypes.c_int
user32.GetWindowTextW.argtypes = (wintypes.HWND, wintypes.LPWSTR, ctypes.c_int)
user32.GetWindowTextW.restype = ctypes.c_int
user32.IsWindowVisible.argtypes = (wintypes.HWND,)
user32.IsWindowVisible.restype = wintypes.BOOL
user32.GetClientRect.argtypes = (wintypes.HWND, ctypes.POINTER(RECT))
user32.GetClientRect.restype = wintypes.BOOL
user32.ClientToScreen.argtypes = (wintypes.HWND, ctypes.POINTER(wintypes.POINT))
user32.ClientToScreen.restype = wintypes.BOOL

EnumWindowsProc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
user32.EnumWindows.argtypes = (EnumWindowsProc, wintypes.LPARAM)
user32.EnumWindows.restype = wintypes.BOOL


@dataclass(frozen=True)
class CompletionEvent:
    event: str
    level: str
    observed_at_utc: str
    completion_time_ocr: Optional[str]
    frame_confirmations: int


def normalized(value: str) -> str:
    return re.sub(r"[^a-z0-9]+", " ", value.casefold()).strip()


def window_title(hwnd: int) -> str:
    length = user32.GetWindowTextLengthW(hwnd)
    if length <= 0:
        return ""
    buffer = ctypes.create_unicode_buffer(length + 1)
    user32.GetWindowTextW(hwnd, buffer, len(buffer))
    return buffer.value


def find_game_window() -> Optional[int]:
    found: list[int] = []

    @EnumWindowsProc
    def visit(hwnd: int, _lparam: int) -> bool:
        if user32.IsWindowVisible(hwnd) and GAME_TITLE_FRAGMENT in window_title(hwnd).casefold():
            found.append(hwnd)
            return False
        return True

    user32.EnumWindows(visit, 0)
    return found[0] if found else None


def client_region(hwnd: int) -> dict[str, int]:
    rect = RECT()
    if not user32.GetClientRect(hwnd, ctypes.byref(rect)):
        raise OSError(ctypes.get_last_error(), "GetClientRect failed")
    origin = wintypes.POINT(rect.left, rect.top)
    if not user32.ClientToScreen(hwnd, ctypes.byref(origin)):
        raise OSError(ctypes.get_last_error(), "ClientToScreen failed")
    width, height = rect.right - rect.left, rect.bottom - rect.top
    if width < 100 or height < 100:
        raise RuntimeError("The game window is minimized or too small to scan.")
    return {"left": origin.x, "top": origin.y, "width": width, "height": height}


def enhance(frame: Image.Image) -> Image.Image:
    # The result-screen text is pale on a dark overlay.  Enlarging and raising
    # contrast makes OCR far more reliable without inspecting the desktop.
    image = ImageOps.grayscale(frame)
    image = ImageEnhance.Contrast(image).enhance(2.5)
    return image.resize((image.width * 2, image.height * 2))


def read_screen(frame: Image.Image) -> str:
    return pytesseract.image_to_string(enhance(frame), config="--psm 6")


def completion_time(text: str) -> Optional[str]:
    match = re.search(r"\b\d{1,2}:\d{2}:\d{2}\b", text)
    return match.group(0) if match else None


def is_completion_screen(text: str, expected_level: str) -> bool:
    folded = normalized(text)
    has_completion = any(normalized(phrase) in folded for phrase in COMPLETION_PHRASES)
    has_continue = normalized(CONTINUE_PHRASE) in folded
    has_level = normalized(expected_level) in folded
    return has_completion and has_continue and has_level


def emit_event(event: CompletionEvent, event_file: Optional[Path], webhook: Optional[str]) -> None:
    payload = asdict(event)
    line = json.dumps(payload, separators=(",", ":"))
    print(line, flush=True)

    if event_file:
        event_file.parent.mkdir(parents=True, exist_ok=True)
        with event_file.open("a", encoding="utf-8", newline="\n") as output:
            output.write(line + "\n")

    if webhook:
        request = Request(
            webhook,
            data=line.encode("utf-8"),
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        try:
            with urlopen(request, timeout=10) as response:
                if not 200 <= response.status < 300:
                    raise RuntimeError(f"webhook returned HTTP {response.status}")
        except (HTTPError, URLError, TimeoutError) as exc:
            raise RuntimeError(f"Could not submit completion event: {exc}") from exc


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--level", required=True, help="Exact selected TRSR level name.")
    parser.add_argument("--poll-seconds", type=float, default=0.40)
    parser.add_argument("--confirmations", type=int, default=3,
                        help="Consecutive matching frames required before one event is emitted.")
    parser.add_argument("--event-file", type=Path,
                        help="Append the verified JSON event to this local file.")
    parser.add_argument("--tesseract", type=Path,
                        help="Path to tesseract.exe when it is not on PATH.")
    parser.add_argument("--webhook", help="POST the verified JSON event to this future results endpoint.")
    parser.add_argument("--debug-image", type=Path,
                        help="Overwrite this image with the most recent game-window capture.")
    parser.add_argument("--once", action="store_true", help="Exit after one verified completion.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.poll_seconds <= 0 or args.confirmations < 2:
        raise SystemExit("--poll-seconds must be positive and --confirmations must be at least 2.")
    if args.tesseract:
        pytesseract.pytesseract.tesseract_cmd = str(args.tesseract)

    confirmed = 0
    completed_this_screen = False
    print("Waiting for Tomb Raider: Anniversary and its completion screen…", flush=True)

    with mss.mss() as capture:
        while True:
            hwnd = find_game_window()
            if hwnd is None:
                confirmed = 0
                completed_this_screen = False
                time.sleep(args.poll_seconds)
                continue

            try:
                raw = capture.grab(client_region(hwnd))
            except (OSError, RuntimeError) as exc:
                print(f"Capture unavailable: {exc}", file=sys.stderr, flush=True)
                time.sleep(args.poll_seconds)
                continue

            frame = Image.frombytes("RGB", raw.size, raw.rgb)
            if args.debug_image:
                args.debug_image.parent.mkdir(parents=True, exist_ok=True)
                frame.save(args.debug_image)
            text = read_screen(frame)

            if is_completion_screen(text, args.level):
                confirmed += 1
            else:
                confirmed = 0
                completed_this_screen = False

            if confirmed >= args.confirmations and not completed_this_screen:
                event = CompletionEvent(
                    event="level_complete_detected",
                    level=args.level,
                    observed_at_utc=datetime.now(timezone.utc).isoformat(),
                    completion_time_ocr=completion_time(text),
                    frame_confirmations=confirmed,
                )
                emit_event(event, args.event_file, args.webhook)
                completed_this_screen = True
                if args.once:
                    return 0

            time.sleep(args.poll_seconds)


if __name__ == "__main__":
    raise SystemExit(main())
