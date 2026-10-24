#!/usr/bin/env python3
"""Safely arm Tomb Raider: Anniversary's Final Conflict replay screen.

This prototype operates on the Tomb Raider client window only.  It reads menu
text with Tesseract before every group of keystrokes and stops on an unexpected
screen.  It never sends gameplay input.  In arm mode it leaves Start Game
selected; a later shared-start controller can send the final Enter command.
"""

from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
import json
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Callable, Optional

import mss
import pytesseract
from PIL import Image, ImageEnhance, ImageOps


GAME_TITLE_FRAGMENT = "tomb raider: anniversary"
POLL_SECONDS = 0.35
CONFIRMATIONS = 3

VK_RETURN = 0x0D
VK_UP = 0x26
VK_DOWN = 0x28
KEYEVENTF_KEYUP = 0x0002
SW_RESTORE = 9


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
user32.ShowWindow.argtypes = (wintypes.HWND, ctypes.c_int)
user32.SetForegroundWindow.argtypes = (wintypes.HWND,)
user32.GetForegroundWindow.restype = wintypes.HWND
user32.keybd_event.argtypes = (ctypes.c_ubyte, ctypes.c_ubyte, wintypes.DWORD,
                               ctypes.c_void_p)

EnumWindowsProc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
user32.EnumWindows.argtypes = (EnumWindowsProc, wintypes.LPARAM)
user32.EnumWindows.restype = wintypes.BOOL


def emit(event: str, **fields: object) -> None:
    payload = {"event": event,
               "observed_at_utc": datetime.now(timezone.utc).isoformat(),
               **fields}
    print(json.dumps(payload, separators=(",", ":")), flush=True)


def normalized(value: str) -> str:
    return " ".join("".join(ch if ch.isalnum() else " " for ch in value.casefold()).split())


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
    image = ImageEnhance.Contrast(ImageOps.grayscale(frame)).enhance(2.5)
    return image.resize((image.width * 2, image.height * 2))


def ocr(frame: Image.Image) -> str:
    return normalized(pytesseract.image_to_string(enhance(frame), config="--psm 6"))


def focused_send(hwnd: int, keys: list[int]) -> None:
    if user32.GetForegroundWindow() != hwnd:
        raise RuntimeError("Focus Tomb Raider with Play Game selected before the countdown ends; no input was sent.")
    for key in keys:
        user32.keybd_event(key, 0, 0, None)
        user32.keybd_event(key, 0, KEYEVENTF_KEYUP, None)
        time.sleep(0.12)


def wait_for(capture: mss.mss, predicate: Callable[[Image.Image, str], bool], name: str,
             debug_image: Optional[Path], timeout_seconds: float = 30.0) -> tuple[int, Image.Image, str]:
    deadline = time.monotonic() + timeout_seconds
    confirmations = 0
    while time.monotonic() < deadline:
        hwnd = find_game_window()
        if hwnd is None:
            confirmations = 0
            time.sleep(POLL_SECONDS)
            continue
        try:
            raw = capture.grab(client_region(hwnd))
        except (OSError, RuntimeError):
            confirmations = 0
            time.sleep(POLL_SECONDS)
            continue
        frame = Image.frombytes("RGB", raw.size, raw.rgb)
        if debug_image:
            debug_image.parent.mkdir(parents=True, exist_ok=True)
            frame.save(debug_image)
        text = ocr(frame)
        confirmations = confirmations + 1 if predicate(frame, text) else 0
        if confirmations >= CONFIRMATIONS:
            return hwnd, frame, text
        time.sleep(POLL_SECONDS)
    raise RuntimeError(f"Timed out waiting for {name}.")


def has_all(text: str, *phrases: str) -> bool:
    return all(normalized(phrase) in text for phrase in phrases)


def final_conflict_details(frame: Image.Image) -> str:
    # The lower detail panel names the selected replay entry.  Cropping it
    # avoids accepting Final Conflict merely because it appears elsewhere in
    # the scroll list.
    width, height = frame.size
    panel = frame.crop((0, int(height * 0.53), width, int(height * 0.84)))
    return ocr(panel)


def arm_final_conflict(debug_image: Optional[Path]) -> tuple[int, mss.mss]:
    capture = mss.mss()
    try:
        hwnd, _frame, _text = wait_for(
            capture,
            lambda _frame, text: has_all(text, "play game", "options", "change profile"),
            "the main menu with Play Game highlighted",
            debug_image,
        )
        emit("menu_step", step="main_menu")
        focused_send(hwnd, [VK_RETURN])

        hwnd, _frame, _text = wait_for(
            capture,
            lambda _frame, text: has_all(text, "resume game", "load game", "replay level"),
            "the Game menu",
            debug_image,
        )
        emit("menu_step", step="game_menu")
        focused_send(hwnd, [VK_DOWN, VK_DOWN, VK_RETURN])

        hwnd, frame, text = wait_for(
            capture,
            lambda _frame, text: has_all(text, "replay level", "final conflict"),
            "the replay-level list",
            debug_image,
        )
        emit("menu_step", step="replay_list")
        # Replay Level opens on Croft Manor when it is unlocked and on the
        # entry after Final Conflict otherwise.  The visible list tells us
        # which route applies without relying on the background/menu pointer.
        focused_send(hwnd, [VK_UP] * (2 if "croft manor" in text else 1))

        hwnd, frame, _text = wait_for(
            capture,
            lambda current_frame, _text: "final conflict" in final_conflict_details(current_frame),
            "Final Conflict being selected",
            debug_image,
        )
        emit("menu_step", step="final_conflict_selected")
        focused_send(hwnd, [VK_RETURN])

        hwnd, _frame, _text = wait_for(
            capture,
            lambda _frame, text: has_all(text, "replay settings", "difficulty", "outfits", "start game"),
            "Replay Settings",
            debug_image,
        )
        focused_send(hwnd, [VK_DOWN, VK_DOWN])
        emit("menu_armed", level="Final Conflict")
        return hwnd, capture
    except Exception:
        capture.close()
        raise


def wait_for_launch_command(hwnd: int, capture: mss.mss, debug_image: Optional[Path]) -> None:
    emit("menu_waiting_for_launch", level="Final Conflict")
    for line in sys.stdin:
        try:
            command = json.loads(line)
        except json.JSONDecodeError:
            continue
        if command.get("command") != "launch":
            continue
        _hwnd, _frame, _text = wait_for(
            capture,
            lambda _frame, text: has_all(text, "replay settings", "start game"),
            "armed Replay Settings",
            debug_image,
            timeout_seconds=5.0,
        )
        focused_send(hwnd, [VK_RETURN])
        emit("menu_launch_sent", level="Final Conflict")
        return
    raise RuntimeError("The launch controller closed before sending GO.")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--level", required=True)
    parser.add_argument("--tesseract", type=Path)
    parser.add_argument("--debug-image", type=Path)
    parser.add_argument("--wait-for-launch", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.level != "Final Conflict":
        raise SystemExit("Automatic menu navigation currently supports Final Conflict only.")
    if args.tesseract:
        pytesseract.pytesseract.tesseract_cmd = str(args.tesseract)
    try:
        hwnd, capture = arm_final_conflict(args.debug_image)
        if args.wait_for_launch:
            try:
                wait_for_launch_command(hwnd, capture, args.debug_image)
            finally:
                capture.close()
        else:
            capture.close()
        return 0
    except Exception as exc:
        emit("menu_navigation_failed", error=str(exc))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
