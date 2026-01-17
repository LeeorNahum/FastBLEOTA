#!/usr/bin/env python3
"""
FastBLEOTA Firmware Uploader

Upload firmware to ESP32/nRF52 devices via BLE OTA.

Usage:
  GUI:  python BLE_OTA.py
  CLI:  python BLE_OTA.py -a AA:BB:CC:DD:EE:FF -f firmware.bin
        python BLE_OTA.py --scan                    # Scan for devices
        python BLE_OTA.py -a ADDRESS -f FILE -v     # Verbose mode

Copyright (c) 2024-2026 Leeor Nahum
"""

import os
import sys
import asyncio
import struct
import time
import argparse
import threading
import zlib
from dataclasses import dataclass, field
from enum import IntEnum
from typing import Optional, Callable, Dict, Any, List
from queue import Queue

try:
    from bleak import BleakClient, BleakScanner, BleakError
except ImportError:
    print("Error: bleak required. Install with: pip install bleak")
    sys.exit(1)

# =============================================================================
# Constants & Protocol Definition
# =============================================================================

SERVICE_UUID = "a4517317-df10-4aed-bcbd-442977fe3fe5"
DATA_CHARACTERISTIC_UUID = "d026496c-0b77-43fb-bd68-fce361a1be1c"
CONTROL_CHARACTERISTIC_UUID = "98f56d4d-0a27-487b-a01b-03ed15daedc7"
PROGRESS_CHARACTERISTIC_UUID = "094b7399-a3a0-41f3-bf8b-5d5f3170ceb0"


class DeviceState(IntEnum):
    """Device OTA state machine states."""
    IDLE = 0
    WAITING_INIT = 1
    RECEIVING = 2
    VALIDATING = 3
    APPLYING = 4
    ERROR = 5


class DeviceError(IntEnum):
    """Device error codes."""
    NONE = 0
    INIT_PACKET_INVALID = 1
    SIZE_TOO_LARGE = 2
    STORAGE_BEGIN_FAILED = 3
    WRITE_FAILED = 4
    CRC_MISMATCH = 5
    SIZE_MISMATCH = 6
    FINALIZE_FAILED = 7
    TIMEOUT = 8
    ABORTED = 9
    NOT_SUPPORTED = 10


class ControlCommand(IntEnum):
    """Control characteristic commands."""
    ABORT = 0x00
    RESET = 0x01
    APPLY = 0x02
    GET_STATUS = 0x03


STATE_NAMES = {
    DeviceState.IDLE: "Idle",
    DeviceState.WAITING_INIT: "Waiting",
    DeviceState.RECEIVING: "Receiving",
    DeviceState.VALIDATING: "Validating",
    DeviceState.APPLYING: "Applying",
    DeviceState.ERROR: "Error"
}

ERROR_NAMES = {
    DeviceError.NONE: "None",
    DeviceError.INIT_PACKET_INVALID: "Invalid init packet",
    DeviceError.SIZE_TOO_LARGE: "Firmware too large",
    DeviceError.STORAGE_BEGIN_FAILED: "Storage begin failed",
    DeviceError.WRITE_FAILED: "Write failed",
    DeviceError.CRC_MISMATCH: "CRC mismatch",
    DeviceError.SIZE_MISMATCH: "Size mismatch",
    DeviceError.FINALIZE_FAILED: "Finalize failed",
    DeviceError.TIMEOUT: "Timeout",
    DeviceError.ABORTED: "Aborted",
    DeviceError.NOT_SUPPORTED: "Not supported"
}


# =============================================================================
# Data Structures
# =============================================================================

@dataclass
class DeviceProgress:
    """Parsed device progress notification."""
    state: DeviceState = DeviceState.IDLE
    error: DeviceError = DeviceError.NONE
    percent: int = 0
    bytes_received: int = 0
    bytes_expected: int = 0
    crc_calculated: int = 0
    
    @classmethod
    def from_bytes(cls, data: bytes) -> 'DeviceProgress':
        if len(data) >= 15:
            state, error, percent, received, expected, crc = struct.unpack('<BBBIIi', data[:15])
            return cls(
                DeviceState(state), 
                DeviceError(error), 
                percent, 
                received, 
                expected, 
                crc
            )
        return cls()
    
    @property
    def state_name(self) -> str:
        return STATE_NAMES.get(self.state, "Unknown")
    
    @property
    def error_name(self) -> str:
        return ERROR_NAMES.get(self.error, "Unknown")


@dataclass
class TransferStats:
    """Statistics for the current transfer."""
    start_time: float = 0.0
    bytes_sent: int = 0
    bytes_total: int = 0
    packets_sent: int = 0
    packets_total: int = 0
    
    @property
    def percent(self) -> int:
        if self.bytes_total == 0:
            return 0
        return min(100, (self.bytes_sent * 100) // self.bytes_total)
    
    @property
    def elapsed(self) -> float:
        if self.start_time == 0:
            return 0.0
        return time.time() - self.start_time
    
    @property
    def speed(self) -> float:
        if self.elapsed <= 0:
            return 0.0
        return self.bytes_sent / self.elapsed
    
    @property
    def eta(self) -> float:
        if self.bytes_sent <= 0 or self.elapsed <= 0:
            return 0.0
        remaining = max(0, self.bytes_total - self.bytes_sent)
        return remaining / self.speed


@dataclass
class UploadResult:
    """Result of an upload operation."""
    success: bool = False
    error_message: str = ""
    stats: TransferStats = field(default_factory=TransferStats)
    device_error: Optional[DeviceError] = None


# =============================================================================
# Utility Functions
# =============================================================================

def calculate_crc32(data: bytes) -> int:
    """Calculate CRC32 using the same algorithm as the device (IEEE 802.3 / zlib)."""
    return zlib.crc32(data) & 0xFFFFFFFF


def format_size(size: int) -> str:
    """Format byte size for display."""
    if size >= 1024 * 1024:
        return f"{size / 1024 / 1024:.2f} MB"
    elif size >= 1024:
        return f"{size / 1024:.1f} KB"
    return f"{size} B"


def format_time(seconds: float) -> str:
    """Format time duration for display."""
    if seconds < 60:
        return f"{seconds:.1f}s"
    return f"{int(seconds // 60)}m {int(seconds % 60)}s"


def create_init_packet(size: int, crc: int, flags: int = 0) -> bytes:
    """Create the 9-byte init packet."""
    return struct.pack('<IIB', size, crc, flags)


# =============================================================================
# BLE OTA Protocol Handler
# =============================================================================

class OTAProtocol:
    """
    Handles the BLE OTA protocol communication.
    
    This class manages:
    - Device connection and characteristic discovery
    - Init packet transmission
    - Firmware chunk transmission with flow control
    - Progress notification handling
    - Device state synchronization
    """
    
    # Send ACK request every N chunks (syncs with device's FBO_ACK_INTERVAL)
    ACK_INTERVAL = 200  # Sync every 200 chunks for performance
    
    def __init__(self):
        self.client: Optional[BleakClient] = None
        self.device_progress = DeviceProgress()
        self.transfer_stats = TransferStats()
        self.disconnected = asyncio.Event()
        self._progress_callbacks: List[Callable[[DeviceProgress], None]] = []
        self._log_callbacks: List[Callable[[str], None]] = []
        self._transfer_callbacks: List[Callable[[TransferStats], None]] = []
        self._last_transfer_emit_time = 0.0
        self._last_transfer_percent = -1
        
    def on_progress(self, callback: Callable[[DeviceProgress], None]):
        """Register a callback for device progress updates."""
        self._progress_callbacks.append(callback)
        
    def on_log(self, callback: Callable[[str], None]):
        """Register a callback for log messages."""
        self._log_callbacks.append(callback)
    
    def on_transfer(self, callback: Callable[[TransferStats], None]):
        """Register a callback for transfer stats updates."""
        self._transfer_callbacks.append(callback)
        
    def _log(self, message: str):
        """Emit a log message."""
        for cb in self._log_callbacks:
            cb(message)
            
    def _emit_progress(self, progress: DeviceProgress):
        """Emit a progress update."""
        for cb in self._progress_callbacks:
            cb(progress)

    def _emit_transfer(self, stats: TransferStats):
        """Emit transfer stats updates."""
        for cb in self._transfer_callbacks:
            cb(stats)
    
    def _handle_disconnect(self, client):
        """Handle device disconnection."""
        self._log("Device disconnected")
        self.disconnected.set()
    
    def _handle_progress_notification(self, sender, data: bytes):
        """Handle incoming progress notifications from device."""
        self.device_progress = DeviceProgress.from_bytes(data)
        self._emit_progress(self.device_progress)
        
        if self.device_progress.state == DeviceState.ERROR:
            self._log(f"Device error: {self.device_progress.error_name}")
    
    async def scan_devices(self, timeout: float = 5.0) -> List[Dict[str, Any]]:
        """Scan for BLE devices."""
        self._log(f"Scanning for BLE devices ({timeout}s)...")
        devices = await BleakScanner.discover(timeout=timeout)
        
        result = []
        for d in devices:
            if d.name and d.name.strip():
                result.append({
                    'address': d.address,
                    'name': d.name,
                    'rssi': d.rssi if hasattr(d, 'rssi') else None
                })
        
        result.sort(key=lambda x: x['name'].lower())
        self._log(f"Found {len(result)} devices with names")
        return result
    
    async def upload(self, address: str, firmware_data: bytes) -> UploadResult:
        """
        Upload firmware to the device.
        
        Args:
            address: BLE device address
            firmware_data: Raw firmware binary data
            
        Returns:
            UploadResult with success status and statistics
        """
        result = UploadResult()
        result.stats.bytes_total = len(firmware_data)
        
        firmware_crc = calculate_crc32(firmware_data)
        
        self._log(f"Firmware size: {len(firmware_data):,} bytes ({format_size(len(firmware_data))})")
        self._log(f"Firmware CRC32: 0x{firmware_crc:08X}")
        self._log(f"Connecting to {address}...")
        
        # Find the device
        device = await BleakScanner.find_device_by_address(address, timeout=10.0)
        if not device:
            result.error_message = f"Device {address} not found"
            self._log(f"ERROR: {result.error_message}")
            return result
        
        self._log(f"Found: {device.name or 'Unknown'}")
        self.disconnected.clear()
        
        try:
            async with BleakClient(device, disconnected_callback=self._handle_disconnect) as client:
                self.client = client
                mtu = client.mtu_size
                chunk_size = mtu - 3
                
                self._log(f"Connected! MTU: {mtu}, Chunk size: {chunk_size}")
                
                # Subscribe to progress notifications
                try:
                    await client.start_notify(PROGRESS_CHARACTERISTIC_UUID, self._handle_progress_notification)
                    self._log("Subscribed to progress notifications")
                except Exception:
                    self._log("Note: Progress notifications unavailable")
                
                # Send init packet
                init_packet = create_init_packet(len(firmware_data), firmware_crc)
                await client.write_gatt_char(DATA_CHARACTERISTIC_UUID, init_packet, response=True)
                self._log(f"Sent init packet (size={len(firmware_data)}, crc=0x{firmware_crc:08X})")
                
                # Brief delay for device to process init
                await asyncio.sleep(0.05)
                
                # Check for immediate errors
                if self.device_progress.state == DeviceState.ERROR:
                    result.error_message = f"Device rejected init: {self.device_progress.error_name}"
                    result.device_error = self.device_progress.error
                    self._log(f"ERROR: {result.error_message}")
                    return result
                
                # Transfer firmware
                result.stats = await self._transfer_firmware(client, firmware_data, chunk_size)
                
                # Wait for device to apply and reboot
                result.success = await self._wait_for_completion()
                
                if result.success:
                    self._log("")
                    self._log("=== Transfer Complete ===")
                    self._log(f"  Time: {format_time(result.stats.elapsed)}")
                    self._log(f"  Speed: {result.stats.speed / 1024:.2f} KB/s")
                    self._log(f"  Packets: {result.stats.packets_sent}")
                
        except BleakError as e:
            # Disconnection during apply is expected
            if self.disconnected.is_set() or self.device_progress.state == DeviceState.APPLYING:
                result.success = True
                self._log("Device rebooted - update successful!")
            else:
                result.error_message = str(e)
                self._log(f"BLE error: {e}")
                
        except Exception as e:
            if self.disconnected.is_set():
                result.success = True
                self._log("Device rebooted - update successful!")
            else:
                result.error_message = str(e)
                self._log(f"Error: {e}")
        
        return result
    
    async def _transfer_firmware(self, client: BleakClient, firmware_data: bytes, chunk_size: int) -> TransferStats:
        """Transfer firmware data in chunks."""
        stats = TransferStats()
        stats.bytes_total = len(firmware_data)
        stats.packets_total = (len(firmware_data) + chunk_size - 1) // chunk_size
        stats.start_time = time.time()
        
        offset = 0
        last_log_percent = -1
        
        while offset < len(firmware_data):
            chunk = firmware_data[offset:offset + chunk_size]
            is_last = (offset + len(chunk) >= len(firmware_data))
            
            # Use response write periodically for sync, and always on last chunk
            use_response = is_last or (stats.packets_sent % self.ACK_INTERVAL == 0 and stats.packets_sent > 0)
            
            await client.write_gatt_char(DATA_CHARACTERISTIC_UUID, chunk, response=use_response)
            
            offset += len(chunk)
            stats.bytes_sent = offset
            stats.packets_sent += 1
            
            current_percent = stats.percent
            now = time.time()
            if is_last or current_percent != self._last_transfer_percent or (now - self._last_transfer_emit_time) >= 0.15:
                self._last_transfer_percent = current_percent
                self._last_transfer_emit_time = now
                self._emit_transfer(stats)
            
            # Log progress every 1%
            if current_percent != last_log_percent or is_last:
                last_log_percent = current_percent
                self._log(f"[{current_percent:3d}%] {stats.packets_sent}/{stats.packets_total} | "
                         f"{offset:,}/{len(firmware_data):,} B")
        
        return stats
    
    async def _wait_for_completion(self) -> bool:
        """Wait for the device to complete the update and reboot."""
        self._log("")
        self._log("Waiting for device to apply update...")
        
        try:
            # Wait for disconnect (device reboots)
            await asyncio.wait_for(self.disconnected.wait(), timeout=15.0)
            self._log("Device rebooted - update successful!")
            return True
        except asyncio.TimeoutError:
            # Check device state
            if self.device_progress.state == DeviceState.APPLYING:
                self._log("Device is applying update...")
                return True
            elif self.device_progress.state == DeviceState.ERROR:
                self._log(f"Device error: {self.device_progress.error_name}")
                return False
            else:
                self._log("Warning: Device did not reboot (may need manual restart)")
                return True


# =============================================================================
# CLI Interface
# =============================================================================

class CLIUploader:
    """Command-line interface for firmware uploads."""
    
    def __init__(self, verbose: bool = False):
        self.verbose = verbose
        self.protocol = OTAProtocol()
        self.protocol.on_log(self._on_log)
        self.protocol.on_progress(self._on_progress)
        
    def _on_log(self, message: str):
        print(message)
        
    def _on_progress(self, progress: DeviceProgress):
        if self.verbose:
            print(f"  [Device] State: {progress.state_name}, "
                  f"Progress: {progress.percent}%, "
                  f"CRC: 0x{progress.crc_calculated:08X}")
    
    async def scan(self, timeout: float = 5.0):
        """Scan and list available devices."""
        devices = await self.protocol.scan_devices(timeout)
        
        if not devices:
            print("No BLE devices found.")
            return
        
        print("\nAvailable devices:")
        print("-" * 50)
        for d in devices:
            rssi = f" (RSSI: {d['rssi']})" if d['rssi'] else ""
            print(f"  {d['address']}  {d['name']}{rssi}")
        print()
    
    async def upload(self, address: str, file_path: str) -> bool:
        """Upload firmware file to device."""
        if not os.path.exists(file_path):
            print(f"Error: File not found: {file_path}")
            return False
        
        print(f"\n=== FastBLEOTA Upload ===")
        print(f"File: {os.path.basename(file_path)}")
        print()
        
        with open(file_path, 'rb') as f:
            firmware_data = f.read()
        
        result = await self.protocol.upload(address, firmware_data)
        return result.success


# =============================================================================
# GUI Interface (Optional)
# =============================================================================

def create_gui():
    """Create and run the GUI application."""
    try:
        import tkinter as tk
        from tkinter import filedialog, ttk
    except ImportError:
        print("Error: tkinter not available")
        sys.exit(1)
    
    root = tk.Tk()
    root.title("FastBLEOTA")
    root.geometry("650x550")
    root.configure(bg='#1e1e1e')
    root.minsize(500, 400)
    
    # Colors
    BG = '#1e1e1e'
    BG_LIGHT = '#252526'
    FG = '#d4d4d4'
    FG_DIM = '#808080'
    ACCENT = '#4ec9b0'
    BLUE = '#569cd6'
    RED = '#f14c4c'
    
    style = ttk.Style()
    style.theme_use('clam')
    style.configure('TFrame', background=BG)
    style.configure('TLabel', background=BG, foreground=FG, font=('Segoe UI', 10))
    style.configure('Header.TLabel', font=('Segoe UI', 10, 'bold'), foreground=BLUE)
    style.configure('Stat.TLabel', font=('Consolas', 9), foreground=FG_DIM)
    style.configure('TButton', font=('Segoe UI', 9), padding=4)
    style.map('TButton', background=[('active', '#3d3d3d'), ('!active', '#2d2d2d')])
    style.configure('Upload.TButton', font=('Segoe UI', 10, 'bold'), padding=8)
    style.configure('TProgressbar', thickness=16, troughcolor='#2d2d2d', background=ACCENT)
    
    # State
    selected_device = {'address': '', 'name': ''}
    firmware_path = tk.StringVar()
    message_queue = Queue()
    
    # Main frame
    main = ttk.Frame(root, padding=12)
    main.pack(fill=tk.BOTH, expand=True)
    
    # Top row: Device + File
    top_frame = ttk.Frame(main)
    top_frame.pack(fill=tk.X, pady=(0, 8))
    
    # Device section
    device_frame = ttk.Frame(top_frame)
    device_frame.pack(side=tk.LEFT, fill=tk.X, expand=True)
    
    ttk.Label(device_frame, text="Device", style='Header.TLabel').pack(anchor='w')
    device_row = ttk.Frame(device_frame)
    device_row.pack(fill=tk.X, pady=2)
    
    scan_btn = ttk.Button(device_row, text="Scan", width=6)
    scan_btn.pack(side=tk.LEFT)
    
    device_label = ttk.Label(device_row, text="None", foreground=FG_DIM)
    device_label.pack(side=tk.LEFT, padx=8)
    
    ttk.Frame(top_frame, width=15).pack(side=tk.LEFT)
    
    # File section
    file_frame = ttk.Frame(top_frame)
    file_frame.pack(side=tk.LEFT, fill=tk.X, expand=True)
    
    ttk.Label(file_frame, text="Firmware", style='Header.TLabel').pack(anchor='w')
    file_row = ttk.Frame(file_frame)
    file_row.pack(fill=tk.X, pady=2)
    
    file_btn = ttk.Button(file_row, text="Browse", width=7)
    file_btn.pack(side=tk.LEFT)
    
    file_label = ttk.Label(file_row, text="None", foreground=FG_DIM)
    file_label.pack(side=tk.LEFT, padx=8)
    
    # Progress section
    progress_frame = ttk.Frame(main)
    progress_frame.pack(fill=tk.X, pady=8)
    
    progress_bar = ttk.Progressbar(progress_frame, mode='determinate', style='TProgressbar')
    progress_bar.pack(fill=tk.X)
    
    # Stats row
    stats_frame = ttk.Frame(progress_frame)
    stats_frame.pack(fill=tk.X, pady=4)
    
    percent_label = ttk.Label(stats_frame, text="0%", font=('Segoe UI', 14, 'bold'), foreground=ACCENT)
    percent_label.pack(side=tk.LEFT)
    
    eta_label = ttk.Label(stats_frame, text="", style='Stat.TLabel')
    eta_label.pack(side=tk.RIGHT)
    
    speed_label = ttk.Label(stats_frame, text="", style='Stat.TLabel')
    speed_label.pack(side=tk.RIGHT, padx=12)
    
    # Device state
    state_frame = ttk.Frame(main)
    state_frame.pack(fill=tk.X, pady=2)
    
    state_label = ttk.Label(state_frame, text="Device: Idle", style='Stat.TLabel')
    state_label.pack(side=tk.LEFT)
    
    crc_label = ttk.Label(state_frame, text="", style='Stat.TLabel')
    crc_label.pack(side=tk.RIGHT)
    
    # Upload button
    upload_btn = ttk.Button(main, text="Upload Firmware", style='Upload.TButton', state=tk.DISABLED)
    upload_btn.pack(pady=8)
    
    # Log section
    log_frame = ttk.Frame(main)
    log_frame.pack(fill=tk.BOTH, expand=True)
    
    ttk.Label(log_frame, text="Log", style='Header.TLabel').pack(anchor='w')
    
    log_container = ttk.Frame(log_frame)
    log_container.pack(fill=tk.BOTH, expand=True, pady=4)
    
    log_text = tk.Text(log_container, bg=BG_LIGHT, fg=FG, font=('Consolas', 9), 
                       relief='flat', padx=8, pady=6, wrap='none',
                       insertbackground=FG, selectbackground='#264f78')
    log_scroll_y = ttk.Scrollbar(log_container, orient='vertical', command=log_text.yview)
    log_scroll_x = ttk.Scrollbar(log_container, orient='horizontal', command=log_text.xview)
    log_text.configure(yscrollcommand=log_scroll_y.set, xscrollcommand=log_scroll_x.set)
    
    log_scroll_y.pack(side=tk.RIGHT, fill=tk.Y)
    log_scroll_x.pack(side=tk.BOTTOM, fill=tk.X)
    log_text.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
    
    log_text.tag_configure('success', foreground=ACCENT)
    log_text.tag_configure('error', foreground=RED)
    log_text.tag_configure('info', foreground=BLUE)
    
    def update_upload_state():
        if selected_device['address'] and firmware_path.get():
            upload_btn.config(state=tk.NORMAL)
        else:
            upload_btn.config(state=tk.DISABLED)
    
    def log_msg(text: str, tag: str = None):
        message_queue.put(('log', text, tag))
    
    def scan_devices():
        scan_btn.config(state=tk.DISABLED)
        log_msg("Scanning for BLE devices...", 'info')
        
        async def do_scan():
            protocol = OTAProtocol()
            return await protocol.scan_devices(timeout=3.0)
        
        def run_scan():
            devices = asyncio.run(do_scan())
            message_queue.put(('devices', devices))
        
        threading.Thread(target=run_scan, daemon=True).start()
    
    def select_file():
        path = filedialog.askopenfilename(filetypes=[("Binary", "*.bin"), ("All", "*.*")])
        if path:
            firmware_path.set(path)
            size = os.path.getsize(path)
            crc = calculate_crc32(open(path, 'rb').read())
            file_label.config(text=f"{os.path.basename(path)} ({format_size(size)})", foreground=FG)
            log_msg(f"Selected: {os.path.basename(path)}", 'info')
            log_msg(f"  Size: {format_size(size)}, CRC32: 0x{crc:08X}")
            update_upload_state()
    
    def reset_ui():
        percent_label.config(text="0%")
        speed_label.config(text="")
        eta_label.config(text="")
        state_label.config(text="Device: Idle")
        crc_label.config(text="")
        progress_bar['value'] = 0
    
    def start_upload():
        upload_btn.config(state=tk.DISABLED)
        scan_btn.config(state=tk.DISABLED)
        file_btn.config(state=tk.DISABLED)
        reset_ui()
        
        protocol = OTAProtocol()
        
        def on_log(msg):
            tag = None
            lower = msg.lower()
            if 'error' in lower or 'failed' in lower:
                tag = 'error'
            elif 'success' in lower or 'complete' in lower:
                tag = 'success'
            message_queue.put(('log', msg, tag))
        
        def on_progress(progress: DeviceProgress):
            message_queue.put(('device_progress', progress))

        def on_transfer(stats: TransferStats):
            message_queue.put(('transfer', stats))
        
        protocol.on_log(on_log)
        protocol.on_progress(on_progress)
        protocol.on_transfer(on_transfer)
        
        def run_upload():
            with open(firmware_path.get(), 'rb') as f:
                firmware_data = f.read()
            
            result = asyncio.run(protocol.upload(selected_device['address'], firmware_data))
            message_queue.put(('done', result))
        
        threading.Thread(target=run_upload, daemon=True).start()
    
    scan_btn.config(command=scan_devices)
    file_btn.config(command=select_file)
    upload_btn.config(command=start_upload)
    
    def process_queue():
        while not message_queue.empty():
            item = message_queue.get()
            
            if item[0] == 'devices':
                scan_btn.config(state=tk.NORMAL)
                devices = item[1]
                
                if devices:
                    dialog = tk.Toplevel(root)
                    dialog.title("Select Device")
                    dialog.geometry("400x300")
                    dialog.configure(bg=BG)
                    dialog.transient(root)
                    dialog.grab_set()
                    
                    ttk.Label(dialog, text="Available Devices", style='Header.TLabel').pack(pady=(10, 5))
                    
                    listbox = tk.Listbox(dialog, bg=BG_LIGHT, fg=FG, font=('Consolas', 10), 
                                        relief='flat', selectbackground='#264f78', 
                                        selectforeground='#ffffff', height=10)
                    listbox.pack(fill=tk.BOTH, expand=True, padx=10, pady=5)
                    
                    for d in devices:
                        listbox.insert(tk.END, f"{d['name']}  [{d['address']}]")
                    
                    def on_select(e=None):
                        sel = listbox.curselection()
                        if sel:
                            d = devices[sel[0]]
                            selected_device['address'] = d['address']
                            selected_device['name'] = d['name']
                            device_label.config(text=f"{d['name']} ({d['address']})", foreground=FG)
                            log_msg(f"Selected: {d['name']} [{d['address']}]", 'info')
                            update_upload_state()
                            dialog.destroy()
                    
                    listbox.bind('<Double-Button-1>', on_select)
                    ttk.Button(dialog, text="Select", command=on_select).pack(pady=8)
                else:
                    log_msg("No BLE devices found", 'error')
                    
            elif item[0] == 'log':
                log_text.config(state=tk.NORMAL)
                log_text.insert(tk.END, item[1] + '\n', (item[2],) if item[2] else ())
                log_text.see(tk.END)
                log_text.config(state=tk.DISABLED)
            elif item[0] == 'transfer':
                stats = item[1]
                pct = stats.percent
                progress_bar['value'] = pct
                percent_label.config(text=f"{pct}%")

                speed_str = format_size(int(stats.speed)) + "/s" if stats.speed > 0 else "..."
                eta_str = format_time(stats.eta) if stats.eta > 0 else "..."
                elapsed_str = format_time(stats.elapsed)

                speed_label.config(text=f"Speed: {speed_str}")
                eta_label.config(text=f"ETA: {eta_str} | Elapsed: {elapsed_str}")
                        
            elif item[0] == 'device_progress':
                progress = item[1]
                state_label.config(text=f"Device: {progress.state_name}")
                if progress.crc_calculated != 0:
                    crc_label.config(text=f"CRC: 0x{progress.crc_calculated:08X}")
                
            elif item[0] == 'done':
                upload_btn.config(state=tk.NORMAL)
                scan_btn.config(state=tk.NORMAL)
                file_btn.config(state=tk.NORMAL)
                result = item[1]
                if result.success:
                    percent_label.config(text="100%")
                    progress_bar['value'] = 100
        
        root.after(50, process_queue)
    
    root.after(50, process_queue)
    root.mainloop()


# =============================================================================
# Main Entry Point
# =============================================================================

def main():
    parser = argparse.ArgumentParser(
        description="FastBLEOTA Firmware Uploader",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python BLE_OTA.py                           # Launch GUI
  python BLE_OTA.py --scan                    # Scan for devices
  python BLE_OTA.py -a AA:BB:CC:DD:EE:FF -f firmware.bin
  python BLE_OTA.py -a AA:BB:CC:DD:EE:FF -f firmware.bin -v
        """
    )
    parser.add_argument('-a', '--address', help='BLE device address')
    parser.add_argument('-f', '--file', help='Firmware binary file')
    parser.add_argument('--scan', action='store_true', help='Scan for BLE devices')
    parser.add_argument('-v', '--verbose', action='store_true', help='Verbose output')
    parser.add_argument('-t', '--timeout', type=float, default=5.0, help='Scan timeout (default: 5.0)')
    
    args = parser.parse_args()
    
    # GUI mode (no arguments)
    if len(sys.argv) == 1:
        create_gui()
        return
    
    # CLI mode
    cli = CLIUploader(verbose=args.verbose)
    
    if args.scan:
        asyncio.run(cli.scan(args.timeout))
        return
    
    if not args.address or not args.file:
        parser.print_help()
        sys.exit(1)
    
    if not os.path.exists(args.file):
        print(f"Error: File not found: {args.file}")
        sys.exit(1)
    
    success = asyncio.run(cli.upload(args.address, args.file))
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
