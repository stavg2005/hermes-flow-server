import socket
import struct
import audioop
import pyaudio
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.backends import default_backend
from cryptography.hazmat.primitives.kdf.hkdf import HKDF
from cryptography.hazmat.primitives import hashes

# ============================================================================
# 1. System Configuration
# ============================================================================
LISTEN_IP = "127.0.0.1"
LISTEN_PORT = 6000

# Out-of-Band (OOB) Pre-Shared Master Key
STATIC_MASTER_KEY = bytes([
    0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6,
    0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C
])

# Out-of-Band (OOB) Pre-Shared Salt
STATIC_SALT = bytes([
    0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6, 0x07, 0x18,
    0x29, 0x3A, 0x4B, 0x5C, 0x6D, 0x7E, 0x8F, 0x90
])

# ============================================================================
# 2. Audio Hardware Setup
# ============================================================================
p = pyaudio.PyAudio()
audio_stream = p.open(
    format=pyaudio.paInt16,  # 16-bit linear PCM
    channels=1,              # Mono
    rate=8000,               # G.711 A-Law standard rate
    output=True
)

# ============================================================================
# 3. Network Setup
# ============================================================================
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((LISTEN_IP, LISTEN_PORT))
print(f"[*] Hermes-Flow Receiver Online")
print(f"[*] Listening for AES-CTR SRTP on {LISTEN_IP}:{LISTEN_PORT}...")

# ============================================================================
# 4. Session State Tracking (Optimization & Security)
# ============================================================================
active_ssrc = None
session_key = None
base_iv = None

# RFC 3711 Rollover Counter variables
roc = 0
max_seq_num = 0

try:
    while True:
        # 1. Ingest UDP Packet
        packet, addr = sock.recvfrom(2048)
        if len(packet) < 12:
            continue  # Drop malformed packets

        # 2. Parse RTP Header
        # Sequence number (Bytes 2-3) and SSRC (Bytes 8-11) in Big-Endian
        seq_num = struct.unpack('!H', packet[2:4])[0]
        ssrc = struct.unpack('!I', packet[8:12])[0]
        encrypted_payload = packet[12:]

        # 3. Dynamic Key Generation (Only triggers on a NEW session)
        if ssrc != active_ssrc:
            print(f"\n[*] ------------------------------------")
            print(f"[*] New Audio Stream Detected!")
            print(f"[*] SSRC: {ssrc}")

            active_ssrc = ssrc
            ssrc_bytes = struct.pack('!I', ssrc) # Convert to bytes for crypto

            # Reset ROC for the new stream
            roc = 0
            max_seq_num = seq_num

            # A. Derive Session Key via HKDF-SHA256
            hkdf = HKDF(
                algorithm=hashes.SHA256(),
                length=16,
                salt=ssrc_bytes,
                info=None,
                backend=default_backend()
            )
            session_key = hkdf.derive(STATIC_MASTER_KEY)

            # B. Derive Base IV (Salt XOR SSRC)
            iv_mutable = bytearray(STATIC_SALT)
            iv_mutable[0] ^= (ssrc >> 24) & 0xFF
            iv_mutable[1] ^= (ssrc >> 16) & 0xFF
            iv_mutable[2] ^= (ssrc >> 8) & 0xFF
            iv_mutable[3] ^= ssrc & 0xFF
            base_iv = bytes(iv_mutable)

            print(f"[*] Derived Key (First 4 bytes): {[hex(b) for b in session_key[:4]]}")
            print(f"[*] Base IV (First 4 bytes): {[hex(b) for b in base_iv[:4]]}")
            print(f"[*] Cryptographic Engine Locked & Synchronized.")
            print(f"[*] ------------------------------------\n")

        # 4. Calculate ROC (RFC 3711 Sequence Number Handling)
        # This handles UDP out-of-order packets and genuine 16-bit wrap-arounds safely.
        v_roc = roc
        if max_seq_num < 32768:
            if seq_num - max_seq_num > 32768:
                # Out-of-order packet from the PREVIOUS sequence cycle
                v_roc = (roc - 1) & 0xFFFFFFFF
            else:
                max_seq_num = max(max_seq_num, seq_num)
        else:
            if max_seq_num - seq_num > 32768:
                # Genuine rollover to the NEXT sequence cycle
                v_roc = (roc + 1) & 0xFFFFFFFF
                roc = v_roc
                max_seq_num = seq_num
            else:
                if seq_num > max_seq_num:
                    max_seq_num = seq_num

        # 5. Calculate Per-Packet IV (AES-CTR Counter Logic)
        # Create a 48-bit index from the 32-bit ROC and 16-bit Sequence Number
        packet_index = (v_roc << 16) | seq_num

        # Mimicking the C++ Little-Endian memory layout for the sequence number XOR
        iv_mutable = bytearray(base_iv)
        tail_64bit = struct.unpack('<Q', iv_mutable[8:16])[0]
        tail_64bit ^= packet_index
        iv_mutable[8:16] = struct.pack('<Q', tail_64bit)
        current_iv = bytes(iv_mutable)

        # 6. Decrypt Payload
        cipher = Cipher(algorithms.AES(session_key), modes.CTR(current_iv), backend=default_backend())
        decryptor = cipher.decryptor()
        decrypted_alaw = decryptor.update(encrypted_payload) + decryptor.finalize()

        # 7. Decode G.711 A-Law to Linear PCM and push to speakers
        pcm_audio = audioop.alaw2lin(decrypted_alaw, 2)
        audio_stream.write(pcm_audio)

except KeyboardInterrupt:
    print("\n[*] Stopping receiver...")
finally:
    audio_stream.stop_stream()
    audio_stream.close()
    p.terminate()
    sock.close()
