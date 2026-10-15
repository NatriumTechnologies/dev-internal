import sys
import os
import re

# Enable virtual terminal processing on Windows for ANSI colors
if os.name == "nt":
    try:
        import ctypes
        kernel32 = ctypes.windll.kernel32
        handle = kernel32.GetStdHandle(-11)  # STD_OUTPUT_HANDLE
        mode = ctypes.c_ulong()
        kernel32.GetConsoleMode(handle, ctypes.byref(mode))
        kernel32.SetConsoleMode(handle, mode.value | 0x0004)  # ENABLE_VIRTUAL_TERMINAL_PROCESSING
    except Exception:
        pass

class Colors:
    RESET   = "\033[0m"
    BOLD    = "\033[1m"
    DIM     = "\033[2m"
    RED     = "\033[91m"
    GREEN   = "\033[92m"
    YELLOW  = "\033[93m"
    BLUE    = "\033[94m"
    MAGENTA = "\033[95m"
    CYAN    = "\033[96m"
    WHITE   = "\033[97m"

ANSI_ESCAPE = re.compile(r"\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~])")

def visible_len(text: str) -> int:
    return len(ANSI_ESCAPE.sub("", text))

def pad_str(text: str, width: int) -> str:
    v_len = visible_len(text)
    pad = max(0, width - v_len)
    return text + (" " * pad)

def banner() -> str:
    return f"""{Colors.CYAN}{Colors.BOLD}
  _   _      _   ____            _          
 | \\ | | ___| |_|  _ \\ _ __ ___ | |__   ___ 
 |  \\| |/ _ \\ __| |_) | '__/ _ \\| '_ \\ / _ \\
 | |\\  |  __/ |_|  __/| | | (_) | |_) |  __/
 |_| \\_|\\___|\\__|_|   |_|  \\___/|_.__/ \\___| {Colors.RESET}{Colors.DIM}v0.1.0{Colors.RESET}
{Colors.DIM}  High-Speed Asynchronous Network Scanner & Packet Analyzer{Colors.RESET}
"""

def print_table(headers: list[str], rows: list[list[str]]) -> None:
    if not rows:
        print(f"{Colors.YELLOW}No active entries detected.{Colors.RESET}")
        return

    # Calculate column widths ignoring ANSI escape sequences
    col_widths = [visible_len(h) for h in headers]
    for row in rows:
        for i, val in enumerate(row):
            if i < len(col_widths):
                col_widths[i] = max(col_widths[i], visible_len(val))

    # Header line
    header_parts = [pad_str(f"{Colors.BOLD}{headers[i]}{Colors.RESET}", col_widths[i]) for i in range(len(headers))]
    print("  ".join(header_parts))
    print(f"{Colors.DIM}" + "  ".join("-" * w for w in col_widths) + f"{Colors.RESET}")

    # Data rows
    for row in rows:
        line_parts = [pad_str(row[i], col_widths[i]) for i in range(len(row))]
        print("  ".join(line_parts))
