#!/usr/bin/env python3
"""Listen for NiclaBleNus_LowPowerTest advertising bursts and print them
to the console as they arrive.
"""

from __future__ import annotations

import argparse
import asyncio
import os
import signal
import sys
import threading
import time
from datetime import timedelta
from pathlib import Path

from bleak import BleakScanner
from bleak.backends.scanner import AdvertisementData, BLEDevice

DEFAULT_NAME = "NiclaBeacon"
DEFAULT_COMPANY_ID = 0xFFFF
DEFAULT_LOG = Path(__file__).with_name("listen_adv.log")


def console(message: str) -> None:
    """Write a line to the console immediately (unbuffered)."""
    line = message if message.endswith("\n") else message + "\n"
    data = line.encode("utf-8", "replace")
    for stream in (sys.stdout, sys.stderr):
        try:
            os.write(stream.fileno(), data)
            return
        except Exception:
            continue


def emit(message: str, log_file) -> None:
    console(message)
    if log_file is not None:
        log_file.write(message + "\n")
        log_file.flush()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Print Nicla Voice beacon advertisements to the console."
    )
    parser.add_argument(
        "--name",
        default=DEFAULT_NAME,
        help=f"Local name to match (default: {DEFAULT_NAME})",
    )
    parser.add_argument(
        "--address",
        default="",
        help="Optional BLE MAC filter, e.g. AA:BB:CC:DD:EE:FF",
    )
    parser.add_argument(
        "--company-id",
        type=lambda value: int(value, 0),
        default=DEFAULT_COMPANY_ID,
        help="Manufacturer company ID to match (default: 0xFFFF)",
    )
    parser.add_argument(
        "--log",
        default=str(DEFAULT_LOG),
        help=f"Also append lines to this file (default: {DEFAULT_LOG.name})",
    )
    parser.add_argument(
        "--no-log",
        action="store_true",
        help="Do not write a log file",
    )
    return parser.parse_args()


def burst_index(advertisement: AdvertisementData, company_id: int) -> str:
    payload = advertisement.manufacturer_data.get(company_id)
    if not payload or len(payload) < 4:
        return "-"
    return str(int.from_bytes(payload[:4], "little"))


def matches(
    device: BLEDevice,
    advertisement: AdvertisementData,
    name: str,
    address: str,
    company_id: int,
) -> bool:
    if address:
        return device.address.lower() == address.lower()
    # Firmware sends AD manufacturer data: company 0xFFFF + 4-byte LE burst
    # index. The local name is often empty on Windows, so do not require it.
    payload = advertisement.manufacturer_data.get(company_id)
    if payload is not None and len(payload) >= 4:
        return True
    local_name = advertisement.local_name or device.name or ""
    return local_name == name


def windows_scan_kwargs() -> dict:
    if sys.platform != "win32":
        return {}

    from winrt.windows.devices.bluetooth import BluetoothSignalStrengthFilter

    strength = BluetoothSignalStrengthFilter()
    strength.sampling_interval = timedelta(0)
    strength.in_range_threshold_in_dbm = -127
    strength.out_of_range_threshold_in_dbm = -127
    strength.out_of_range_timeout = timedelta(seconds=1)
    return {"SignalStrengthFilter": strength}


def _fmt_scan_params(params) -> str:
    window = int(params.scan_window)
    interval = int(params.scan_interval)
    if interval <= 0:
        return f"window={window} interval={interval} (units 0.625 ms)"
    duty = 100.0 * window / interval
    return (
        f"window={window * 0.625:.3f} ms  interval={interval * 0.625:.3f} ms  "
        f"duty={duty:.1f}%"
    )


def arm_windows_low_latency_scan() -> dict:
    """Ask WinRT for LowLatency scan before the watcher starts.

    Bleak has no ScanParameters knob. Windows will not let an app set
    window=interval; LowLatency is the highest documented preset.
    """
    result: dict = {"ok": False, "text": "not Windows"}
    if sys.platform != "win32":
        return result

    from winrt.windows.devices.bluetooth.advertisement import (
        BluetoothLEAdvertisementScanParameters,
        BluetoothLEAdvertisementWatcher,
    )

    orig_start = BluetoothLEAdvertisementWatcher.start

    def start(self) -> None:
        try:
            params = BluetoothLEAdvertisementScanParameters.low_latency()
            self.scan_parameters = params
            result["ok"] = True
            result["text"] = "LowLatency " + _fmt_scan_params(params)
        except Exception as exc:
            result["ok"] = False
            result["text"] = f"LowLatency unavailable ({type(exc).__name__}: {exc})"
        orig_start(self)

    BluetoothLEAdvertisementWatcher.start = start  # type: ignore[method-assign]
    result["text"] = "LowLatency armed (applied on watcher start)"
    return result


def describe_watcher_scan(scanner: BleakScanner) -> str:
    try:
        watcher = scanner._backend.watcher  # type: ignore[attr-defined]
        params = watcher.scan_parameters
        if params is None:
            return "watcher scan parameters: (none / OS default)"
        return "watcher " + _fmt_scan_params(params)
    except Exception as exc:
        return f"watcher scan parameters unread ({type(exc).__name__}: {exc})"


async def _stop_scanner(scanner: BleakScanner) -> None:
    try:
        await asyncio.wait_for(scanner.stop(), timeout=1.0)
    except (asyncio.TimeoutError, Exception):
        pass


async def listen(args: argparse.Namespace, log_file) -> None:
    emit("=== Nicla beacon listener ===", log_file)
    emit("If you see this line, console output is working.", log_file)
    who = args.address or (
        f"name {args.name!r} or company 0x{args.company_id:04X} + 4-byte index"
    )
    emit(f"Waiting for {who}. Ctrl+C to stop.", log_file)

    t0 = time.monotonic()
    loop = asyncio.get_running_loop()
    stop = asyncio.Event()
    out_lock = threading.Lock()

    def request_stop() -> None:
        stop.set()

    try:
        loop.add_signal_handler(2, request_stop)  # SIGINT; ignored on Windows
    except (NotImplementedError, RuntimeError):
        pass

    def elapsed() -> str:
        return f"{time.monotonic() - t0:.3f}s"

    def emit_now(message: str) -> None:
        with out_lock:
            emit(message, log_file)

    def callback(device: BLEDevice, advertisement: AdvertisementData) -> None:
        rssi = advertisement.rssi
        local_name = advertisement.local_name or device.name or ""
        if not matches(
            device, advertisement, args.name, args.address, args.company_id
        ):
            return
        index = burst_index(advertisement, args.company_id)
        emit_now(
            f">>> BEACON  {elapsed()}  addr={device.address}  rssi={rssi}  "
            f"name={local_name!r}  burst={index}"
        )

    scanner = BleakScanner(
        detection_callback=callback,
        scanning_mode="passive",
        **windows_scan_kwargs(),
    )
    scan_req = arm_windows_low_latency_scan()
    emit(scan_req["text"], log_file)
    await scanner.start()
    emit(describe_watcher_scan(scanner), log_file)
    try:
        while not stop.is_set():
            try:
                await asyncio.wait_for(stop.wait(), timeout=10)
            except asyncio.TimeoutError:
                emit_now(f"{elapsed()}  still scanning...")
    except (KeyboardInterrupt, asyncio.CancelledError):
        pass
    finally:
        await _stop_scanner(scanner)


def main() -> int:
    try:
        sys.stdout.reconfigure(line_buffering=True, write_through=True)
        sys.stderr.reconfigure(line_buffering=True, write_through=True)
    except Exception:
        pass

    def _on_ctrl_c(signum, frame) -> None:
        console("Stopped.")
        os._exit(0)

    signal.signal(signal.SIGINT, _on_ctrl_c)
    if hasattr(signal, "SIGBREAK"):
        signal.signal(signal.SIGBREAK, _on_ctrl_c)

    args = parse_args()
    log_file = None if args.no_log else open(args.log, "a", encoding="utf-8")
    try:
        if log_file is not None:
            console(f"Also writing {args.log}")
        asyncio.run(listen(args, log_file))
    except KeyboardInterrupt:
        pass
    emit("Stopped.", log_file)
    if log_file is not None:
        log_file.close()
    os._exit(0)


if __name__ == "__main__":
    sys.exit(main())
