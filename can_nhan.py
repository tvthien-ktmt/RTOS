#!/usr/bin/env python3
# =====================================================
# can_nhan.py — Nhận CAN bus trên Raspberry Pi
# Chạy: sudo python3 can_nhan.py
#
# CAN frames từ BNON555.ino (ESP32C3 + BNO055):
#   0x100 — IMU:      heading, roll, pitch (int16 ×100, little-endian) + sys_cal, mag_cal
#   0x103 — Distance: dist_cm (uint32 little-endian) + local_ok, remote_ok
#
# Setup can0 trước khi chạy:
#   sudo ip link set can0 up type can bitrate 500000
# =====================================================

import can
import struct

bus = can.interface.Bus(channel='can0', interface='socketcan')
print("=== CAN BUS RECEIVER ===")
print("Dang lang nghe... (Ctrl+C de thoat)\n")

count = 0
while True:
    msg = bus.recv(timeout=2.0)

    if msg is None:
        print("[!] Timeout — khong nhan duoc du lieu, kiem tra ket noi!")
        continue

    count += 1

    # --------------------------------------------------
    # 0x100: BNO055 IMU — heading, roll, pitch + cal
    # Format: int16 little-endian x100 cho goc, 1 byte cho moi cal
    # --------------------------------------------------
    if msg.arbitration_id == 0x100:
        heading = struct.unpack_from('<h', msg.data, 0)[0] / 100.0
        roll    = struct.unpack_from('<h', msg.data, 2)[0] / 100.0
        pitch   = struct.unpack_from('<h', msg.data, 4)[0] / 100.0
        sys_cal = msg.data[6]
        mag_cal = msg.data[7]
        print(f"[0x100] IMU  Head={heading:7.2f}  Roll={roll:7.2f}  Pitch={pitch:7.2f}  | Cal: Sys={sys_cal}/3  Mag={mag_cal}/3")

    # --------------------------------------------------
    # 0x103: Distance in cm + GPS status
    # Format: uint32 little-endian (cm) + 2 status bytes
    # --------------------------------------------------
    elif msg.arbitration_id == 0x103:
        dist_cm    = struct.unpack_from('<I', msg.data, 0)[0]   # unsigned int32
        local_ok   = msg.data[4]
        remote_ok  = msg.data[5]
        print(f"[0x103] DIST {dist_cm/100:.2f}m  |  GPS Thuyen={'OK' if local_ok else 'NO'}  GPS Nan nhan={'OK' if remote_ok else 'NO'}")

    # --------------------------------------------------
    # 0x101: GPS thuyền (lat, lon int32 ×1,000,000)
    # --------------------------------------------------
    elif msg.arbitration_id == 0x101:
        lat = struct.unpack_from('<i', msg.data, 0)[0] / 1_000_000
        lon = struct.unpack_from('<i', msg.data, 4)[0] / 1_000_000
        print(f"[0x101] GPS Thuyen:    lat={lat:.6f}  lon={lon:.6f}")

    # --------------------------------------------------
    # 0x102: GPS nạn nhân (lat, lon int32 ×1,000,000)
    # --------------------------------------------------
    elif msg.arbitration_id == 0x102:
        lat = struct.unpack_from('<i', msg.data, 0)[0] / 1_000_000
        lon = struct.unpack_from('<i', msg.data, 4)[0] / 1_000_000
        print(f"[0x102] GPS Nan nhan:  lat={lat:.6f}  lon={lon:.6f}")

    # --------------------------------------------------
    # ID khong xac dinh
    # --------------------------------------------------
    else:
        print(f"[0x{msg.arbitration_id:03X}] Unknown ({msg.dlc} bytes): {msg.data.hex()}")

    if count % 20 == 0:
        print(f"--- {count} frames nhan duoc ---")