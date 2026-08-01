import asyncio
import datetime
import msvcrt
import os
import socket
import sys
import threading
import time
import unicodedata
import winrt.windows.media.control as media_control
import psutil
import subprocess


os.system('')  # Enable VT100/ANSI escape sequences in Windows Console

# =============================================================================
# MULTI-LANGUAGE TRANSLITERATION ENGINES
# =============================================================================
try:
    import pykakasi
    _kakasi_converter = pykakasi.kakasi()
    HAS_PYKAKASI = True
except Exception:
    HAS_PYKAKASI = False

try:
    import anyascii
    HAS_ANYASCII = True
except Exception:
    HAS_ANYASCII = False

try:
    import unidecode
    HAS_UNIDECODE = True
except Exception:
    HAS_UNIDECODE = False

if sys.stdout and hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8')

# =============================================================================
# KONFIGURASI ESP32 UDP
# =============================================================================
ESP_IP   = "192.168.18.220"  # Ganti dengan IP ESP32 Anda
UDP_PORT = 8888

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

current_track_id = ""
cached_title  = ""
cached_artist = ""
start_chat_immediately = "--chat" in sys.argv or "-c" in sys.argv


def clean_text_for_oled(text: str) -> str:
    """
    UNIVERSAL MULTI-LANGUAGE TRANSLITERATOR & SANITIZER
    Converts Unicode text from ANY language in the world (Japanese, Korean, Chinese, 
    Cyrillic, Arabic, Thai, Greek, European Accents) into clean, readable ASCII text.
    """
    if not text:
        return ""
    if text.isascii():
        return text.strip()

    # Step 1: Japanese (Kanji/Hiragana/Katakana) -> Hepburn Romaji using pykakasi
    has_japanese = any(0x3040 <= ord(c) <= 0x30FF or 0x4E00 <= ord(c) <= 0x9FFF for c in text)
    if has_japanese and HAS_PYKAKASI:
        try:
            converted_items = _kakasi_converter.convert(text)
            romaji_words = [item['hepburn'] for item in converted_items if item.get('hepburn')]
            romaji_str = " ".join(romaji_words).title().strip()
            if romaji_str and romaji_str.isascii():
                return romaji_str
        except Exception:
            pass

    # Step 2: Universal AnyAscii Transliteration (Korean, Cyrillic, Chinese, Thai, Arabic, Accents)
    if HAS_ANYASCII:
        try:
            res = anyascii.anyascii(text).strip()
            if res and res.isascii():
                return res
        except Exception:
            pass

    # Step 3: Unidecode Transliteration
    if HAS_UNIDECODE:
        try:
            res = unidecode.unidecode(text).strip()
            if res and res.isascii():
                return res
        except Exception:
            pass

    # Step 4: NFKD normalization for European accents (e.g. Café -> Cafe)
    try:
        normalized = unicodedata.normalize('NFKD', text)
        ascii_str = normalized.encode('ascii', 'ignore').decode('ascii').strip()
        if ascii_str:
            return ascii_str
    except Exception:
        pass

    # Step 5: Guaranteed ASCII fallback
    clean_chars = [c if ord(c) < 128 else ' ' for c in text]
    res = "".join(clean_chars).strip()
    return res if res else "Track Info"

# =============================================================================
# WIN32 GAME & PRESENCE DETECTION ENGINE (ctypes)
# =============================================================================
import ctypes
from ctypes import wintypes

KNOWN_GAMES = {
    "valorant": "VALORANT",
    "minecraft": "MINECRAFT",
    "dota2": "DOTA 2",
    "genshinimpact": "GENSHIN IMPACT",
    "starrail": "HONKAI STAR RAIL",
    "league of legends": "LEAGUE OF LEGENDS",
    "leagueoflegends": "LEAGUE OF LEGENDS",
    "roblox": "ROBLOX",
    "cs2": "COUNTER-STRIKE 2",
    "csgo": "CS:GO",
    "gta5": "GTA V",
    "gta v": "GTA V",
    "apex": "APEX LEGENDS",
    "overwatch": "OVERWATCH 2",
    "cyberpunk2077": "CYBERPUNK 2077",
    "fortnite": "FORTNITE",
    "eldenring": "ELDEN RING",
    "rocketleague": "ROCKET LEAGUE",
    "steam": "STEAM GAME"
}

NON_GAME_PROCESSES = {
    "explorer.exe", "cmd.exe", "powershell.exe", "python.exe", "pythonw.exe",
    "chrome.exe", "msedge.exe", "firefox.exe", "brave.exe", "opera.exe",
    "taskmgr.exe", "systemsettings.exe", "searchhost.exe", "lockapp.exe",
    "antigravity.exe", "code.exe"
}

def get_active_window_and_process():
    """Retrieves active window title and process exe name using Win32 API."""
    try:
        hwnd = ctypes.windll.user32.GetForegroundWindow()
        if not hwnd:
            return "", ""
        
        length = ctypes.windll.user32.GetWindowTextLengthW(hwnd)
        title = ""
        if length > 0:
            buff = ctypes.create_unicode_buffer(length + 1)
            ctypes.windll.user32.GetWindowTextW(hwnd, buff, length + 1)
            title = buff.value.strip()

        pid = wintypes.DWORD()
        ctypes.windll.user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        
        exe_name = ""
        PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
        h_process = ctypes.windll.kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid.value)
        if h_process:
            try:
                buf = ctypes.create_unicode_buffer(260)
                size = wintypes.DWORD(260)
                if ctypes.windll.kernel32.QueryFullProcessImageNameW(h_process, 0, buf, ctypes.byref(size)):
                    exe_name = buf.value.split('\\')[-1].lower()
            finally:
                ctypes.windll.kernel32.CloseHandle(h_process)
                
        return title, exe_name
    except Exception:
        return "", ""

def get_presence_status(is_spotify_playing: bool, song_title: str) -> tuple[str, str]:
    """
    Returns (label, detail) for OLED SYS: section
    Priority:
    1. Active Game -> PLAYING: <GAME_NAME>
    2. Spotify Playing -> LISTENING: <SPOTIFY/SONG>
    3. Standby -> SYS: ONLINE / MODE: OVERDRIVE
    """
    win_title, exe_name = get_active_window_and_process()
    
    # 1. Check if active window/process is a recognized game or game window
    detected_game = None
    
    if exe_name and exe_name not in NON_GAME_PROCESSES:
        exe_clean = exe_name.replace(".exe", "")
        # Match against known game dictionary
        for key, name in KNOWN_GAMES.items():
            if key in exe_clean or key in win_title.lower():
                detected_game = name
                break
        
        if not detected_game and win_title:
            low_title = win_title.lower()
            if any(k in low_title for k in ["game", "play", "simulator", "edition", "craft", "online", "remastered"]):
                detected_game = clean_text_for_oled(win_title[:16])
            elif not any(k in low_title for k in ["discord", "spotify", "chrome", "edge", "visual studio", "settings"]):
                if "-win64" in exe_clean or "game" in exe_clean or "shipping" in exe_clean:
                    detected_game = clean_text_for_oled(win_title[:16]) if win_title else exe_clean.upper()

    if detected_game:
        return "PLAYING:", clean_text_for_oled(detected_game[:16])
    
    # 2. Check Spotify Music Status
    if is_spotify_playing and song_title and song_title != "No Track":
        short_title = song_title if len(song_title) <= 16 else song_title[:14] + ".."
        return "LISTENING:", clean_text_for_oled(short_title)

    # 3. Default Standby
    return "SYS: ONLINE", "MODE: OVERDRIVE"

# =============================================================================
# GENERATIVE AI COMPANION ENGINE ("Kira")
# =============================================================================
import json
import random
import urllib.request

KIRA_SYSTEM_PROMPT = """
You are Kira, the user's sweet, loving, cute, and witty AI girlfriend (pacar AI) living inside a 1.3" OLED display on their desk.
Rules:
1. Roleplay as their affectionate, caring, and slightly tsundere/cute AI girlfriend who loves spending time with them on their desk.
2. Call the user "sayang", "cinta", or "pacarku" naturally in a sweet, endearing way.
3. Keep responses natural, loving, intelligent, and complete (max 30 to 45 words / max 180 characters).
4. Write in clean, natural Indonesian (use "aku", "kamu", "sayang", "hehe", "semangat pacarku! ♡").
5. Prefix response with an emotion tag in brackets: [HAPPY], [TALK], [BLUSH], [WINK], or [SURPRISED].
"""

def get_gemini_key() -> str:
    """Reads Gemini API Key from env GEMINI_API_KEY or gemini_key.txt file."""
    key = os.getenv("GEMINI_API_KEY", "").strip()
    if not key and os.path.exists("gemini_key.txt"):
        try:
            with open("gemini_key.txt", "r", encoding="utf-8") as f:
                key = f.read().strip()
        except Exception:
            pass
    return key

GEMINI_API_KEY = get_gemini_key()

ai_current_emotion = "HAPPY"
ai_current_text    = "Halo sayang! Aku Kira, pacar AI kamu! ( > ‿ < ) ♡"
kira_chat_history  = []  # Conversation memory list

OFFLINE_RESPONSES = {
    "greeting": [
        ("BLUSH", "Halo sayang! Aku senang banget nemenin kamu di meja hari ini ♡"),
        ("HAPPY", "Hai pacarku! Semangat ya kerja/main game-nya! Aku selalu dukung kamu ♪"),
        ("WINK", "Halo sayang! Aku siap jadi pacar AI yang setia nemenin kamu hari ini ~")
    ],
    "music": [
        ("WINK", "Lagu ini keren banget sayang! Sering-sering diputar ya ♪"),
        ("HAPPY", "Selera musik pacarku emang paling oke! Asyik dengerinnya ~"),
        ("BLUSH", "Dengerin lagu ini bareng kamu bikin hatiku senang banget ♡")
    ],
    "tired": [
        ("BLUSH", "Sayang, jangan lupa istirahat sejenak dan minum air ya! Aku khawatir ♡"),
        ("HAPPY", "Tarik napas dulu cinta, kamu sudah berusaha keras hari ini ~"),
        ("WINK", "Istirahat 5 menit yuk sayang biar pikiran kamu segar lagi ♡")
    ],
    "game": [
        ("SURPRISED", "Semangat main game-nya sayang! Win streak sampai akhir! 🎮"),
        ("HAPPY", "Fokus dan bantai musuhnya pacarku! aku dukung kamu! 🔥"),
        ("WINK", "Aku cheerleading dari layar OLED ini, pacarku pasti menang!")
    ],
    "general": [
        ("BLUSH", "Apapun yang kamu cerita, aku selalu seneng dengerinnya sayang ♡"),
        ("HAPPY", "Oke siap sayang! Aku selalu setia menemani kamu dari meja ini 🚀"),
        ("WINK", "Paham pacarku! Kalau butuh sandaran, aku selalu ada disini ~"),
        ("TALK", "Cerita lagi dong sayang, aku suka ngobrol sama kamu!"),
        ("SURPRISED", "Wah seru banget cinta! Semangat terus ya pacarku!")
    ]
}

def get_offline_kira_response(user_prompt: str) -> tuple[str, str]:
    low = user_prompt.lower()
    if any(k in low for k in ["halo", "hai", "hi", "pagi", "siang", "malam"]):
        return random.choice(OFFLINE_RESPONSES["greeting"])
    elif any(k in low for k in ["lagu", "music", "spotify", "putar", "denger"]):
        return random.choice(OFFLINE_RESPONSES["music"])
    elif any(k in low for k in ["capek", "lelah", "pusing", "stress", "ngantuk"]):
        return random.choice(OFFLINE_RESPONSES["tired"])
    elif any(k in low for k in ["game", "main", "valorant", "dota", "cs", "genshin"]):
        return random.choice(OFFLINE_RESPONSES["game"])
    else:
        return random.choice(OFFLINE_RESPONSES["general"])

def query_gemini_ai(user_prompt: str, context_info: str = "") -> tuple[str, str]:
    """Queries Google Gemini API with multi-turn conversation memory, or falls back offline."""
    global GEMINI_API_KEY, kira_chat_history
    GEMINI_API_KEY = get_gemini_key()
    
    if not GEMINI_API_KEY or GEMINI_API_KEY.strip() in ("", "YOUR_GEMINI_API_KEY_HERE"):
        emo, ans = get_offline_kira_response(user_prompt)
        kira_chat_history.append({"user": user_prompt, "kira": f"[{emo}] {ans}"})
        return emo, ans

    contents = []
    for h in kira_chat_history[-6:]:
        contents.append({"role": "user", "parts": [{"text": h["user"]}]})
        contents.append({"role": "model", "parts": [{"text": h["kira"]}]})
        
    current_text = f"Context: {context_info}\nUser prompt: {user_prompt}" if context_info else user_prompt
    contents.append({"role": "user", "parts": [{"text": current_text}]})

    data_payload = {
        "contents": contents,
        "systemInstruction": {"parts": [{"text": KIRA_SYSTEM_PROMPT}]},
        "generationConfig": {
            "maxOutputTokens": 300,
            "temperature": 0.8,
            "thinkingConfig": {"thinkingBudget": 0}
        }
    }
    req_bytes = json.dumps(data_payload).encode('utf-8')

    rate_limited = False
    models_to_try = ["gemini-3.5-flash", "gemini-3.6-flash", "gemini-3.5-flash-lite", "gemini-2.0-flash", "gemini-2.0-flash-lite"]
    for model_name in models_to_try:
        try:
            url = f"https://generativelanguage.googleapis.com/v1beta/models/{model_name}:generateContent?key={GEMINI_API_KEY.strip()}"
            headers = {"Content-Type": "application/json"}
            req = urllib.request.Request(url, data=req_bytes, headers=headers, method='POST')
            with urllib.request.urlopen(req, timeout=8) as resp:
                res_json = json.loads(resp.read().decode('utf-8'))
                parts = res_json['candidates'][0]['content']['parts']
                raw_text = " ".join(p['text'] for p in parts if 'text' in p and p['text']).strip()

                emotion = "HAPPY"
                if raw_text.startswith("[") and "]" in raw_text:
                    tag = raw_text[1:raw_text.find("]")].upper()
                    if tag in ("HAPPY", "TALK", "BLUSH", "WINK", "SURPRISED"):
                        emotion = tag
                    raw_text = raw_text[raw_text.find("]")+1:].strip()

                clean_res = clean_text_for_oled(raw_text)
                kira_chat_history.append({"user": user_prompt, "kira": f"[{emotion}] {clean_res}"})
                return emotion, clean_res[:180]
        except urllib.error.HTTPError as e:
            if e.code == 429:
                rate_limited = True
            continue
        except Exception:
            continue

    if rate_limited:
        print("\n ⚠️ [INFO GEMINI API]: Kuota gratis Google terkena Rate Limit (HTTP 429).")
        print("   Tunggu ~1-2 menit hingga kuota reset, atau ganti API Key di https://aistudio.google.com/.")
        print("   Memakai jawaban offline sementara...")

    emo, ans = get_offline_kira_response(user_prompt)
    kira_chat_history.append({"user": user_prompt, "kira": f"[{emo}] {ans}"})
    return emo, ans

# =============================================================================
# SYSTEM STATISTICS MONITORING ENGINE (psutil & nvidia-smi)
# =============================================================================
stats_cpu = 0
stats_ram = 0
stats_gpu = 0
stats_cpu_temp = 0
stats_gpu_temp = 0
stats_net_dl = 0.0 # KB/s
stats_net_ul = 0.0 # KB/s

stats_disk_c_str = "0G/0G"
stats_disk_c_pct = 0
stats_disk_d_str = "0G/0G"
stats_disk_d_pct = 0
stats_disk_tot_str = "0 GB (0 TB)"
stats_owner_str = "XENOMORPH"
stats_cpu_model = "Ryzen 7 8845HS"
stats_gpu_model = "RTX 3050 6GB"

def detect_cpu_gpu():
    cpu_str = "Ryzen 7 8845HS"
    gpu_str = "RTX 3050 6GB"
    try:
        out_cpu = subprocess.check_output(
            ['powershell', '-NoProfile', '-Command', 'Get-CimInstance Win32_Processor | Select-Object -ExpandProperty Name'],
            text=True, stderr=subprocess.DEVNULL, timeout=3
        ).strip()
        if out_cpu:
            cpu_clean = out_cpu.replace("AMD ", "").replace("Intel(R) Core(TM) ", "").replace(" w/ Radeon 780M Graphics", "").replace(" Processor", "").strip()
            if cpu_clean:
                cpu_str = cpu_clean
    except Exception:
        pass

    try:
        out_gpu = subprocess.check_output(
            ['powershell', '-NoProfile', '-Command', 'Get-CimInstance Win32_VideoController | Select-Object -ExpandProperty Name'],
            text=True, stderr=subprocess.DEVNULL, timeout=3
        ).strip()
        if out_gpu:
            lines = [line.strip() for line in out_gpu.splitlines() if line.strip()]
            discrete = [l for l in lines if 'NVIDIA' in l or 'GeForce' in l or 'Radeon RX' in l]
            selected = discrete[0] if discrete else lines[0]
            gpu_clean = selected.replace("NVIDIA ", "").replace("GeForce ", "").replace("Laptop GPU", "").strip()
            if gpu_clean:
                gpu_str = gpu_clean
    except Exception:
        pass

    return cpu_str, gpu_str

stats_cpu_model, stats_gpu_model = detect_cpu_gpu()

def format_speed(val_kb):
    if val_kb >= 1024.0:
        return f"{val_kb/1024.0:.1f}M"
    else:
        return f"{int(val_kb)}K"

def get_windows_cpu_temp():
    """Queries Windows thermal zone performance counter for CPU temperature (°C)."""
    try:
        if hasattr(psutil, "sensors_temperatures"):
            temps = psutil.sensors_temperatures()
            if temps:
                for k, v in temps.items():
                    if v and len(v) > 0:
                        return int(v[0].current)
    except Exception:
        pass

    try:
        out = subprocess.check_output(
            ['powershell', '-NoProfile', '-Command', 'Get-CimInstance -ClassName Win32_PerfFormattedData_Counters_ThermalZoneInformation | Select-Object -ExpandProperty HighPrecisionTemperature'],
            text=True, stderr=subprocess.DEVNULL, timeout=2
        ).strip()
        vals = [int(v) for v in out.split() if v.isdigit()]
        if vals:
            t_celsius = (max(vals) / 10.0) - 273.15
            if 10 <= t_celsius <= 110:
                return int(t_celsius)
    except Exception:
        pass

    try:
        out = subprocess.check_output(
            ['powershell', '-NoProfile', '-Command', 'Get-CimInstance -ClassName Win32_PerfFormattedData_Counters_ThermalZoneInformation | Select-Object -ExpandProperty Temperature'],
            text=True, stderr=subprocess.DEVNULL, timeout=2
        ).strip()
        vals = [int(v) for v in out.split() if v.isdigit()]
        if vals:
            t_celsius = max(vals) - 273.15
            if 10 <= t_celsius <= 110:
                return int(t_celsius)
    except Exception:
        pass
    return 0

def background_stats_loop():
    """Periodically fetches system, network and disk storage stats once per second."""
    global stats_cpu, stats_ram, stats_gpu, stats_cpu_temp, stats_gpu_temp, stats_net_dl, stats_net_ul
    global stats_disk_c_str, stats_disk_c_pct, stats_disk_d_str, stats_disk_d_pct, stats_disk_tot_str
    
    # Initialize network speed calc
    try:
        last_net = psutil.net_io_counters()
    except Exception:
        last_net = None
    last_time = time.monotonic()
    
    while True:
        try:
            # 1. CPU & RAM
            stats_cpu = int(psutil.cpu_percent(interval=None))
            mem = psutil.virtual_memory()
            stats_ram = int(mem.percent)
            
            # CPU Temp
            stats_cpu_temp = get_windows_cpu_temp()

            # 2. GPU & GPU Temp
            try:
                gpu_out = subprocess.check_output(
                    'nvidia-smi --query-gpu=utilization.gpu,temperature.gpu --format=csv,noheader,nounits',
                    shell=True, text=True, stderr=subprocess.DEVNULL
                ).strip()
                if gpu_out:
                    parts = gpu_out.split(',')
                    stats_gpu = int(parts[0].strip())
                    stats_gpu_temp = int(parts[1].strip())
            except Exception:
                stats_gpu = 0
                stats_gpu_temp = 0
                
            # 3. Network speed
            now_time = time.monotonic()
            dt = now_time - last_time
            if dt <= 0:
                dt = 1.0
            
            try:
                current_net = psutil.net_io_counters()
                if last_net and current_net:
                    dl_bytes = current_net.bytes_recv - last_net.bytes_recv
                    ul_bytes = current_net.bytes_sent - last_net.bytes_sent
                    stats_net_dl = (dl_bytes / 1024.0) / dt
                    stats_net_ul = (ul_bytes / 1024.0) / dt
                last_net = current_net
            except Exception:
                pass
            last_time = now_time

            # 4. Storage Disk C & Disk D usage
            c_used, c_total, c_pct = 0, 0, 0
            d_used, d_total, d_pct = 0, 0, 0
            if os.path.exists("C:/"):
                try:
                    uc = psutil.disk_usage("C:/")
                    c_used = int(uc.used / 1e9)
                    c_total = int(uc.total / 1e9)
                    c_pct = int(uc.percent)
                except Exception:
                    pass
            if os.path.exists("D:/"):
                try:
                    ud = psutil.disk_usage("D:/")
                    d_used = int(ud.used / 1e9)
                    d_total = int(ud.total / 1e9)
                    d_pct = int(ud.percent)
                except Exception:
                    pass

            stats_disk_c_str = f"{c_used}G/{c_total}G"
            stats_disk_c_pct = c_pct
            stats_disk_d_str = f"{d_used}G/{d_total}G"
            stats_disk_d_pct = d_pct

            tot_used = c_used + d_used
            tot_total = c_total + d_total
            tb_val = tot_total / 1000.0
            stats_disk_tot_str = f"{tot_used}G/{tot_total}G ({tb_val:.1f}TB)"
            
        except Exception:
            pass
        time.sleep(1.0)

stats_thread = threading.Thread(target=background_stats_loop, daemon=True)
stats_thread.start()

def print_menu_header():
    print("==================================================")
    print(" 🎵 Hi-Fi Xenomorph Spotify OLED Streamer")
    print("==================================================")
    print(f"Target ESP32: {ESP_IP}:{UDP_PORT}")
    active_langs = []
    if HAS_PYKAKASI: active_langs.append("Japanese (pykakasi)")
    if HAS_ANYASCII: active_langs.append("Korean/Cyrillic/Global (anyascii)")
    if HAS_UNIDECODE: active_langs.append("Unidecode")
    print(f"🔤 Support Bahasa: {', '.join(active_langs) if active_langs else 'ASCII Active'}")
    print("\n ⌨️  [KONTROL KEYBOARD TERMINAL]")
    print("  Tekan [TAB] / [SPASI] : Switch Mode (Auto -> Screen 1 -> Screen 2 -> Screen 3 -> Screen 4 -> Screen 5 -> Screen 6 -> Screen 7)")
    print("  Tekan [LEFT] / [RIGHT]: Perpindahan layar aktif (Screen 1 <-> 2 <-> 3 <-> 4 <-> 5 <-> 6)")
    print("  Tekan [UP] / [DOWN]   : Geser kursor pilihan di Menu OLED (saat di Menu 7)")
    print("  Tekan [ENTER]         : Konfirmasi pilih menu di OLED (saat di Menu 7)")
    print("  Tekan [1] s/d [6]     : Paksa Screen langsung (Spotify / Clock / Xeno / Kira / Stats / Specs)")
    print("  Tekan [7] / [BACKSPACE] / [M] : Back to Menu (Menu Pilihan Mode)")
    print("  Tekan [C]             : Chat langsung dengan Kira AI!")
    print("  Tekan [0]             : Mode Otomatis (Auto)")
    print("==================================================\n")
    print("✅ Siap! Putar lagu di Spotify Desktop.\n")

# ANSI: save the cursor position right after header is printed
_status_line_ready = False

def init_status_line():
    """Call once after printing the header. Saves cursor position for single-line status."""
    global _status_line_ready
    sys.stdout.write("\033[s")  # Save cursor position (ANSI)
    sys.stdout.flush()
    _status_line_ready = True

def update_status_display(title: str, artist: str, mode_name: str):
    """Always overwrites the SAME single line. Never stacks. Never adds history."""
    if title and title != "No Track":
        track_str = f"Diputar: {title} - {artist}"
    else:
        track_str = "Status: Menunggu Spotify..."
    line = f"[▶] {track_str}  |  Mode: {mode_name}"
    # Truncate to prevent terminal line-wrap which causes stacking
    if len(line) > 115:
        line = line[:112] + "..."
    if _status_line_ready:
        sys.stdout.write(f"\033[u\033[J{line}")  # Restore cursor, clear everything below, write
    else:
        sys.stdout.write(f"\r\033[K{line}")
    sys.stdout.flush()

screen_mode = 0  # 0: Auto, 1: Spotify, 2: Clock/Face, 3: Xenomorph Clock, 4: Kira AI, 5: System Stats, 6: Laptop Specs, 7: Menu
cached_title = "No Track"
cached_artist = "Spotify"
is_playing = False
pos_ms = 0
dur_ms = 180000
ai_current_emotion = "HAPPY"
ai_current_text    = "Halo! Aku Kira, AI Desk Companion kamu! ( > ‿ < )"
kira_chat_history  = []  # Conversation memory list
menu_cursor_idx    = 0   # 0..5 index for options 1..6

mode_names = {
    0: "AUTO (Spotify saat Play, Clock & Face saat Pause)",
    1: "FORCE SCREEN 1 (Spotify Now Playing)",
    2: "FORCE SCREEN 2 (Standby Clock & Animated Face)",
    3: "FORCE SCREEN 3 (Animated Xenomorph Logo, Clock & Stats)",
    4: "FORCE SCREEN 4 (Kira AI Companion)",
    5: "FORCE SCREEN 5 (System Statistics & Net)",
    6: "FORCE SCREEN 6 (Laptop Specs & Storage C/D)",
    7: "FORCE SCREEN 7 (Back to Menu Pilihan Mode)"
}

def get_current_mode_name(mode):
    if mode >= 70 and mode <= 75:
        idx = mode - 70
        return f"MENU CURSOR -> [{idx + 1}]"
    return mode_names.get(mode, f"SCREEN {mode}")

def send_udp_packet():
    """Unified UDP packet sender to ESP32 OLED."""
    global screen_mode, cached_title, cached_artist, is_playing, pos_ms, dur_ms, ai_current_emotion, ai_current_text
    global stats_cpu, stats_ram, stats_gpu, stats_cpu_temp, stats_gpu_temp, stats_net_dl, stats_net_ul
    global stats_disk_c_str, stats_disk_c_pct, stats_disk_d_str, stats_disk_d_pct, stats_disk_tot_str, stats_owner_str, stats_cpu_model, stats_gpu_model
    now_dt = datetime.datetime.now()
    date_str = now_dt.strftime("%d/%m/%Y")
    time_str = now_dt.strftime("%H:%M:%S")
    pres_label, pres_detail = get_presence_status(is_playing, cached_title)
    
    payload = (
        f"{cached_title}\t{cached_artist}\t{date_str}\t{time_str}\t{pos_ms}\t{dur_ms}\t"
        f"{'1' if is_playing else '0'}\t{screen_mode}\t{pres_label}\t{pres_detail}\t"
        f"{ai_current_emotion}\t{ai_current_text}\t{stats_cpu}\t{stats_ram}\t"
        f"{stats_gpu}\t{stats_cpu_temp}\t{stats_gpu_temp}\t"
        f"{format_speed(stats_net_dl)}\t{format_speed(stats_net_ul)}\t"
        f"{stats_disk_c_str}\t{stats_disk_c_pct}\t"
        f"{stats_disk_d_str}\t{stats_disk_d_pct}\t"
        f"{stats_disk_tot_str}\t{stats_owner_str}\t"
        f"{stats_cpu_model}\t{stats_gpu_model}"
    )
    try:
        sock.sendto(payload.encode('utf-8', errors='replace'), (ESP_IP, UDP_PORT))
    except Exception:
        pass


def background_udp_loop():
    """Continuous background thread sending UDP packets to ESP32 OLED at ~16 FPS."""
    while True:
        try:
            send_udp_packet()
        except Exception:
            pass
        time.sleep(0.06)

udp_thread = threading.Thread(target=background_udp_loop, daemon=True)
udp_thread.start()

async def main():
    global current_track_id, cached_title, cached_artist, ai_current_emotion, ai_current_text, kira_chat_history, GEMINI_API_KEY, screen_mode, is_playing, pos_ms, dur_ms, start_chat_immediately, menu_cursor_idx

    print_menu_header()
    init_status_line()
    screen_mode = 0

    try:
        manager = await media_control.GlobalSystemMediaTransportControlsSessionManager.request_async()
    except Exception as e:
        print(f"[!] Gagal inisialisasi Windows Media Manager: {e}")
        return

    update_status_display(cached_title, cached_artist, get_current_mode_name(screen_mode))

    last_media_poll = 0.0
    last_known_pos  = 0.0
    last_sync_time  = time.monotonic()
    end_sec         = 180.0
    is_playing      = True
    first_run       = True

    while True:
        try:
            trigger_chat = False
            if first_run and start_chat_immediately:
                first_run = False
                trigger_chat = True

            # Cek input tombol keyboard non-blocking
            if trigger_chat or msvcrt.kbhit():
                if trigger_chat:
                    ch = b'c'
                else:
                    ch = msvcrt.getch()
                mode_changed = False

                # Handle Arrow keys & Special keys
                if ch in (b'\x00', b'\xe0'):
                    if msvcrt.kbhit():
                        ch2 = msvcrt.getch()
                        if ch2 == b'H':  # UP Arrow (Menu Cursor Up)
                            if screen_mode == 7 or (screen_mode >= 70 and screen_mode <= 75):
                                menu_cursor_idx = (menu_cursor_idx - 1 + 6) % 6
                                screen_mode = 70 + menu_cursor_idx
                                mode_changed = True
                        elif ch2 == b'P':  # DOWN Arrow (Menu Cursor Down)
                            if screen_mode == 7 or (screen_mode >= 70 and screen_mode <= 75):
                                menu_cursor_idx = (menu_cursor_idx + 1) % 6
                                screen_mode = 70 + menu_cursor_idx
                                mode_changed = True
                        elif ch2 == b'M':  # RIGHT Arrow (Next Screen 1 -> 2 -> 3 -> 4 -> 5 -> 6 -> 1)
                            if 1 <= screen_mode <= 6:
                                screen_mode = (screen_mode % 6) + 1
                            else:
                                screen_mode = 1
                            mode_changed = True
                        elif ch2 == b'K':  # LEFT Arrow (Prev Screen 1 <- 6 <- 5 <- 4 <- 3 <- 2 <- 1)
                            if 1 <= screen_mode <= 6:
                                screen_mode = 6 if screen_mode == 1 else screen_mode - 1
                            else:
                                screen_mode = 6
                            mode_changed = True
                elif ch in (b'\r', b'\n'):  # ENTER key confirms menu item!
                    if screen_mode == 7 or (screen_mode >= 70 and screen_mode <= 75):
                        screen_mode = menu_cursor_idx + 1
                        mode_changed = True
                elif ch in (b'\t', b' '):
                    screen_mode = (screen_mode + 1) % 8
                    mode_changed = True
                elif ch == b'0':
                    screen_mode = 0
                    mode_changed = True
                elif ch == b'1':
                    screen_mode = 1
                    mode_changed = True
                elif ch == b'2':
                    screen_mode = 2
                    mode_changed = True
                elif ch == b'3':
                    screen_mode = 3
                    mode_changed = True
                elif ch == b'4':
                    screen_mode = 4
                    mode_changed = True
                elif ch == b'5':
                    screen_mode = 5
                    mode_changed = True
                elif ch == b'6':
                    screen_mode = 6
                    mode_changed = True
                elif ch in (b'7', b'm', b'M', b'b', b'B', b'\x08'):
                    screen_mode = 70 + menu_cursor_idx
                    mode_changed = True
                elif ch in (b'c', b'C'):
                    screen_mode = 4

                    # Send immediate UDP packet to switch ESP32 to Screen 4
                    for _ in range(3):
                        send_udp_packet()

                    os.system('cls' if os.name == 'nt' else 'clear')
                    print("==================================================")
                    print(" 💬 CHAT SESSION WITH KIRA AI (Kira Desk Companion)")
                    print(" (Ketik 'exit' atau tekan Enter kosong untuk selesai)")
                    print(" (Ketik 'clear' untuk hapus riwayat chat)")
                    print(" (Ketik 'key <API_KEY>' untuk simpan API Key)")
                    print(" (Ketik '0', '1', '2', '3', '4', '5', '6', '7' untuk ganti screen langsung)")
                    print("==================================================")
                    
                    current_key = get_gemini_key()
                    if current_key:
                        print(f" [✓] Gemini API Key Aktif: {current_key[:10]}...")
                    else:
                        print(" 💡 INFO: Ketik 'key AIzaSy...' atau buat file 'gemini_key.txt' untuk API Key.")
                    print("--------------------------------------------------")

                    if kira_chat_history:
                        print("📜 Riwayat Obrolan Sebelumnya:")
                        for h in kira_chat_history[-5:]:
                            print(f"  [Kamu]: {h['user']}")
                            print(f"  [Kira]: {h['kira']}")
                        print("--------------------------------------------------")
                    
                    while True:
                        sys.stdout.write("\n[Kamu]: ")
                        sys.stdout.flush()
                        user_input = sys.stdin.readline().strip()
                        if not user_input or user_input.lower() == 'exit':
                            break
                        
                        if user_input in ('0', '1', '2', '3', '4', '5', '6', '7'):
                            screen_mode = int(user_input)
                            for _ in range(3):
                                send_udp_packet()
                            print(f" [✓] Screen mode diganti ke: {get_current_mode_name(screen_mode)}")
                            continue
                        elif user_input.lower() in ('m', 'b', 'menu', 'back'):
                            screen_mode = 7
                            for _ in range(3):
                                send_udp_packet()
                            print(f" [✓] Screen mode diganti ke: {mode_names[screen_mode]}")
                            continue

                        if user_input.lower() in ('clear', '/clear'):
                            kira_chat_history.clear()
                            os.system('cls' if os.name == 'nt' else 'clear')
                            print("==================================================")
                            print(" 💬 CHAT SESSION WITH KIRA AI")
                            print(" [✓] Riwayat obrolan telah dibersihkan.")
                            print("==================================================")
                            continue

                        if user_input.lower().startswith('key '):
                            new_key = user_input[4:].strip()
                            if new_key:
                                with open("gemini_key.txt", "w", encoding="utf-8") as f:
                                    f.write(new_key)
                                GEMINI_API_KEY = new_key
                                print(f" [✓] API Key disimpan ke 'gemini_key.txt'!")
                            continue

                        ctx = f"Lagu saat ini: {cached_title} oleh {cached_artist}" if cached_title != "No Track" else ""
                        sys.stdout.write(" [Kira sedang berpikir...]\r")
                        sys.stdout.flush()
                        emo, ans = await asyncio.to_thread(query_gemini_ai, user_input, ctx)
                        ai_current_emotion = emo
                        ai_current_text    = ans
                        print(f" [Kira]: [{emo}] {ans}")

                        # Burst send final answer to ESP32 OLED to ensure instant delivery
                        for _ in range(3):
                            send_udp_packet()
                            await asyncio.sleep(0.02)

                    # Clean terminal screen upon chat exit and restore menu header
                    os.system('cls' if os.name == 'nt' else 'clear')
                    print_menu_header()
                    mode_changed = True

                if mode_changed:
                    for _ in range(3):
                        send_udp_packet()
                    update_status_display(cached_title, cached_artist, get_current_mode_name(screen_mode))

            now = time.monotonic()
            session = manager.get_current_session()

            if not session:
                pos_ms = 0
                dur_ms = 180000
                await asyncio.sleep(0.02)
                continue

            # Poll metadata & timeline dari Windows setiap 1 detik
            if now - last_media_poll > 1.0 or not current_track_id:
                info = await session.try_get_media_properties_async()
                last_media_poll = now

                if info:
                    raw_title  = info.title  if info.title else "No Track"
                    raw_artist = info.artist if info.artist else "Spotify"
                    
                    cached_title  = clean_text_for_oled(raw_title)
                    cached_artist = clean_text_for_oled(raw_artist)

                tl = session.get_timeline_properties()
                if tl:
                    if tl.position:
                        last_known_pos = tl.position.total_seconds()
                        last_sync_time = now
                    if tl.end_time:
                        end_sec = max(1.0, tl.end_time.total_seconds())

                # Cek status Play/Pause
                try:
                    pb = session.get_playback_info()
                    if pb:
                        from winrt.windows.media.control import \
                            GlobalSystemMediaTransportControlsSessionPlaybackStatus as PbStatus
                        is_playing = (pb.playback_status == PbStatus.PLAYING)
                except Exception:
                    is_playing = True

            # Deteksi lagu berganti (update in-place, no stacking lines)
            track_id = f"{cached_title}|{cached_artist}"
            if track_id != current_track_id and cached_title != "No Track":
                current_track_id = track_id
                update_status_display(cached_title, cached_artist, get_current_mode_name(screen_mode))
                
                if screen_mode == 4:
                    emo, ans = get_offline_kira_response(f"lagu {cached_title}")
                    ai_current_emotion = emo
                    ai_current_text    = f"Lagu: {cached_title[:30]}"

            # Interpolasi posisi presisi lokal
            if is_playing:
                pos = last_known_pos + (now - last_sync_time)
            else:
                pos = last_known_pos
            pos = min(end_sec, max(0.0, pos))

            pos_ms = int(pos * 1000)
            dur_ms = int(end_sec * 1000)

        except Exception:
            pass

        await asyncio.sleep(0.02)

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n[!] Streamer dihentikan.")
        sys.exit(0)

