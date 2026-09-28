#
# Copyright 2025-2026 NXP
#
# SPDX-License-Identifier: BSD-3-Clause
#
import tkinter as tk
from tkinter import ttk, messagebox, scrolledtext
import cv2
from PIL import Image, ImageTk
import threading
import socket
import re
from datetime import datetime
import time
import struct
import subprocess
import atexit
import numpy as np
import math

class FPSCounter:
    """Track frames per second for video/audio streams"""

    def __init__(self, window_size=1.0):
        self.window_size = window_size  # Time window in seconds
        self.frame_times = []
        self.total_frames = 0
        self.lock = threading.Lock()

    def update(self):
        """Record a new frame"""
        with self.lock:
            current_time = time.time()
            self.frame_times.append(current_time)
            self.total_frames += 1

            # Remove old frames outside the window
            cutoff_time = current_time - self.window_size
            self.frame_times = [t for t in self.frame_times if t > cutoff_time]

    def get_fps(self):
        """Get current FPS"""
        with self.lock:
            if len(self.frame_times) < 2:
                return 0.0

            current_time = time.time()
            cutoff_time = current_time - self.window_size

            # Count frames in the window
            recent_frames = [t for t in self.frame_times if t > cutoff_time]

            if len(recent_frames) < 2:
                return 0.0

            # Calculate FPS
            time_span = current_time - recent_frames[0]
            if time_span > 0:
                return len(recent_frames) / time_span
            return 0.0

    def get_total_frames(self):
        """Get total frame count"""
        with self.lock:
            return self.total_frames

    def reset(self):
        """Reset the counter"""
        with self.lock:
            self.frame_times = []
            self.total_frames = 0


class AudioGenerator:
    """Generate simple audio tones and melodies"""

    def __init__(self, sample_rate=8000):
        self.sample_rate = sample_rate

    def generate_tone(self, frequency, duration, amplitude=0.3):
        """Generate a simple sine wave tone"""
        num_samples = int(self.sample_rate * duration)
        t = np.linspace(0, duration, num_samples, False)
        tone = amplitude * np.sin(2 * np.pi * frequency * t)
        # Convert to 16-bit PCM
        audio_data = (tone * 32767).astype(np.int16)
        return audio_data.tobytes()

    def generate_simple_melody(self):
        """Generate a simple repeating melody (like 'Twinkle Twinkle Little Star')"""
        # Note frequencies (C major scale)
        notes = {
            'C4': 261.63,
            'D4': 293.66,
            'E4': 329.63,
            'F4': 349.23,
            'G4': 392.00,
            'A4': 440.00,
            'B4': 493.88,
            'C5': 523.25,
            'REST': 0
        }

        # Simple melody: "Twinkle Twinkle Little Star" pattern
        # C C G G A A G - F F E E D D C
        melody = [
            ('C4', 0.3), ('C4', 0.3), ('G4', 0.3), ('G4', 0.3),
            ('A4', 0.3), ('A4', 0.3), ('G4', 0.6),
            ('F4', 0.3), ('F4', 0.3), ('E4', 0.3), ('E4', 0.3),
            ('D4', 0.3), ('D4', 0.3), ('C4', 0.6),
            ('REST', 0.2)  # Short pause before repeat
        ]

        audio_chunks = []
        for note, duration in melody:
            if note == 'REST':
                # Generate silence
                num_samples = int(self.sample_rate * duration)
                silence = np.zeros(num_samples, dtype=np.int16)
                audio_chunks.append(silence.tobytes())
            else:
                audio_chunks.append(self.generate_tone(notes[note], duration))

        return b''.join(audio_chunks)

    def generate_test_pattern(self):
        """Generate a simple test pattern (beep sequence)"""
        # Three beeps pattern
        beep1 = self.generate_tone(440, 0.2)  # A4
        beep2 = self.generate_tone(554, 0.2)  # C#5
        beep3 = self.generate_tone(659, 0.2)  # E5

        silence = self.generate_tone(0, 0.1)  # Short silence

        pattern = beep1 + silence + beep2 + silence + beep3 + silence * 3
        return pattern


class FirewallManager:
    """Manage Windows Firewall rules for UDP ports"""

    def __init__(self, rule_name_prefix="RTSP_Client_RTP"):
        self.rule_name_prefix = rule_name_prefix
        self.rules_added = []
        self.enabled = False

    def add_firewall_rules(self, ports):
        """Add firewall rules for specified ports"""
        if not isinstance(ports, list):
            ports = [ports]

        success_count = 0

        for port in ports:
            rule_name = f"{self.rule_name_prefix}_Port_{port}"

            try:
                # Check if rule already exists and delete it
                check_cmd = [
                    'netsh', 'advfirewall', 'firewall', 'show', 'rule',
                    f'name={rule_name}'
                ]

                result = subprocess.run(
                    check_cmd,
                    capture_output=True,
                    text=True,
                    timeout=5,
                    creationflags=subprocess.CREATE_NO_WINDOW if hasattr(subprocess, 'CREATE_NO_WINDOW') else 0
                )

                # Delete existing rule
                if "No rules match" not in result.stdout:
                    delete_cmd = [
                        'netsh', 'advfirewall', 'firewall', 'delete', 'rule',
                        f'name={rule_name}'
                    ]
                    subprocess.run(
                        delete_cmd,
                        capture_output=True,
                        timeout=5,
                        creationflags=subprocess.CREATE_NO_WINDOW if hasattr(subprocess, 'CREATE_NO_WINDOW') else 0
                    )

                # Add new rule
                add_cmd = [
                    'netsh', 'advfirewall', 'firewall', 'add', 'rule',
                    f'name={rule_name}',
                    'dir=in',
                    'action=allow',
                    'protocol=UDP',
                    f'localport={port}'
                ]

                result = subprocess.run(
                    add_cmd,
                    capture_output=True,
                    text=True,
                    timeout=5,
                    creationflags=subprocess.CREATE_NO_WINDOW if hasattr(subprocess, 'CREATE_NO_WINDOW') else 0
                )

                if result.returncode == 0:
                    self.rules_added.append(rule_name)
                    success_count += 1

            except Exception as e:
                # Silently fail - will log in calling function
                pass

        self.enabled = success_count > 0
        return success_count, len(ports)

    def remove_firewall_rules(self):
        """Remove all firewall rules that were added"""
        if not self.rules_added:
            return 0

        removed_count = 0

        for rule_name in self.rules_added[:]:  # Copy list to iterate
            try:
                delete_cmd = [
                    'netsh', 'advfirewall', 'firewall', 'delete', 'rule',
                    f'name={rule_name}'
                ]

                result = subprocess.run(
                    delete_cmd,
                    capture_output=True,
                    text=True,
                    timeout=5,
                    creationflags=subprocess.CREATE_NO_WINDOW if hasattr(subprocess, 'CREATE_NO_WINDOW') else 0
                )

                if result.returncode == 0:
                    self.rules_added.remove(rule_name)
                    removed_count += 1

            except Exception as e:
                pass

        self.enabled = len(self.rules_added) > 0
        return removed_count


class RTPPacket:
    """RTP Packet Parser and Builder"""
    HEADER_SIZE = 12

    def __init__(self):
        self.version = 2
        self.padding = 0
        self.extension = 0
        self.cc = 0
        self.marker = 0
        self.pt = 0
        self.seqnum = 0
        self.timestamp = 0
        self.ssrc = 0
        self.payload = b''

    def decode(self, packet):
        """Decode RTP packet"""
        if len(packet) < self.HEADER_SIZE:
            return False

        header = packet[:self.HEADER_SIZE]
        self.payload = packet[self.HEADER_SIZE:]

        # Parse header
        byte0 = header[0]
        self.version = (byte0 >> 6) & 0x03
        self.padding = (byte0 >> 5) & 0x01
        self.extension = (byte0 >> 4) & 0x01
        self.cc = byte0 & 0x0F

        byte1 = header[1]
        self.marker = (byte1 >> 7) & 0x01
        self.pt = byte1 & 0x7F

        self.seqnum = struct.unpack('!H', header[2:4])[0]
        self.timestamp = struct.unpack('!I', header[4:8])[0]
        self.ssrc = struct.unpack('!I', header[8:12])[0]

        return True

    def encode(self, payload, seqnum, timestamp, marker=0, pt=0, ssrc=0):
        """Encode RTP packet for sending"""
        self.payload = payload
        self.seqnum = seqnum
        self.timestamp = timestamp
        self.marker = marker
        self.pt = pt
        self.ssrc = ssrc

        # Build header
        byte0 = (self.version << 6) | (self.padding << 5) | (self.extension << 4) | self.cc
        byte1 = (self.marker << 7) | self.pt

        header = struct.pack('!BBHII',
                           byte0,
                           byte1,
                           self.seqnum,
                           self.timestamp,
                           self.ssrc)

        return header + self.payload

    def get_payload(self):
        return self.payload


class RTSPClient:
    def __init__(self, root):
        self.root = root
        self.root.title("RTSP/RTP UDP Client - MJPEG Video & Two-Way Audio")

        self.root.minsize(1200, 400)
        self.root.resizable(True, True)

        # RTSP connection variables
        self.rtsp_url = ""
        self.session_id = None
        self.seq_number = 1
        self.rtsp_socket = None
        self.is_playing = False
        self.connection_state = "DISCONNECTED"

        # UDP RTP variables - using ports after 40000
        self.video_rtp_socket = None
        self.audio_rtp_socket = None
        self.backchannel_rtp_socket = None  # For sending audio to server

        self.video_rtp_port = 40000
        self.audio_rtp_port = 40002
        self.video_rtcp_port = 40001
        self.audio_rtcp_port = 40003
        self.backchannel_rtp_port = 40004  # Client port for sending audio
        self.backchannel_rtcp_port = 40005  # Client RTCP port

        # Video variables
        self.video_thread = None
        self.frame_buffer = []
        self.current_frame = None
        self.frame_count = 0
        self.last_sequence = -1

        # FPS Counters
        self.video_fps_counter = FPSCounter(window_size=1.0)
        self.audio_rx_fps_counter = FPSCounter(window_size=1.0)
        self.audio_tx_fps_counter = FPSCounter(window_size=1.0)

        # Audio receive variables
        self.audio_enabled = True
        self.audio_available = False
        self.audio_thread = None
        self.audio_stream = None
        self.pyaudio_instance = None

        # Audio format from SDP
        self.audio_codec = None
        self.audio_sample_rate = 8000
        self.audio_channels = 1
        self.audio_payload_type = None

        # Audio backchannel (send) variables
        self.backchannel_enabled = True  # Default enabled
        self.backchannel_available = False
        # Audio volume control - RX and TX
        self.audio_rx_volume = 2.0  # RX volume multiplier (2x amplification)
        self.audio_tx_volume = 2.0  # TX volume multiplier (2x amplification)
        self.max_volume = 10.0      # Maximum 10x amplification
        self.backchannel_thread = None
        self.backchannel_input_stream = None
        self.backchannel_codec = 'L16'  # Will be set from SDP
        self.backchannel_sample_rate = 8000
        self.backchannel_channels = 1
        self.backchannel_payload_type = 96
        self.backchannel_control_track = None

        # Generated audio mode
        self.use_generated_audio = False
        self.audio_generator = None
        self.generated_audio_buffer = None
        self.generated_audio_position = 0

        # RTP sending state
        self.send_seqnum = 0
        self.send_timestamp = 0
        self.send_ssrc = 0x12345678  # Random SSRC

        # Server destination for backchannel
        self.server_backchannel_ip = None
        self.server_backchannel_port = None

        # Server info
        self.server_address = None
        self.server_port = None
        self.video_control_track = None
        self.audio_control_track = None

        # Firewall Manager
        self.firewall_manager = FirewallManager()
        self.firewall_enabled_var = tk.BooleanVar(value=True)

        # Verbose Logging
        self.verbose_logging_var = tk.BooleanVar(value=False)

        # FPS Update Timer
        self.fps_update_timer = None

        # Register cleanup on exit
        atexit.register(self.cleanup_on_exit)

        # Setup GUI
        self.setup_gui()

        # Handle window close
        self.root.protocol("WM_DELETE_WINDOW", self.on_closing)

    def setup_gui(self):
        # Create main container with grid layout
        main_container = ttk.Frame(self.root)
        main_container.pack(fill=tk.BOTH, expand=True, padx=10, pady=5)

        # Configure grid weights for responsive layout
        main_container.rowconfigure(0, weight=0)
        main_container.rowconfigure(1, weight=1)
        main_container.rowconfigure(2, weight=0)
        main_container.columnconfigure(0, weight=1, minsize=500)
        main_container.columnconfigure(1, weight=2, minsize=660)

        # ===== LEFT COLUMN - TOP - Control Panel =====
        control_frame = ttk.LabelFrame(main_container, text="RTSP Control Panel", padding=10)
        control_frame.grid(row=0, column=0, sticky=(tk.N, tk.E, tk.W), padx=(0, 5), pady=(0, 5))

        control_frame.rowconfigure(0, weight=0)
        control_frame.rowconfigure(1, weight=0)
        control_frame.rowconfigure(2, weight=0)
        control_frame.rowconfigure(3, weight=0)

        # URL Input
        url_frame = ttk.Frame(control_frame)
        url_frame.grid(row=0, column=0, columnspan=6, sticky=tk.EW, pady=5)

        ttk.Label(url_frame, text="RTSP URL:").pack(side=tk.LEFT, padx=5)
        self.url_entry = ttk.Entry(url_frame, width=40)
        self.url_entry.pack(side=tk.LEFT, padx=5, fill=tk.X, expand=True)
        self.url_entry.insert(0, "rtsp://172.16.20.2:8554")

        # Port Configuration Frame
        port_frame = ttk.LabelFrame(control_frame, text="Client UDP Port Configuration (Ports > 40000)", padding=5)
        port_frame.grid(row=1, column=0, columnspan=6, sticky=tk.EW, pady=5)

        ttk.Label(port_frame, text="Video RTP:").grid(row=0, column=0, padx=5, pady=2, sticky=tk.W)
        self.video_port_entry = ttk.Entry(port_frame, width=8)
        self.video_port_entry.grid(row=0, column=1, padx=5, pady=2)
        self.video_port_entry.insert(0, "40000")

        ttk.Label(port_frame, text="Video RTCP:").grid(row=0, column=2, padx=5, pady=2, sticky=tk.W)
        self.video_rtcp_entry = ttk.Entry(port_frame, width=8)
        self.video_rtcp_entry.grid(row=0, column=3, padx=5, pady=2)
        self.video_rtcp_entry.insert(0, "40001")

        ttk.Label(port_frame, text="Audio RTP:").grid(row=1, column=0, padx=5, pady=2, sticky=tk.W)
        self.audio_port_entry = ttk.Entry(port_frame, width=8)
        self.audio_port_entry.grid(row=1, column=1, padx=5, pady=2)
        self.audio_port_entry.insert(0, "40002")

        ttk.Label(port_frame, text="Audio RTCP:").grid(row=1, column=2, padx=5, pady=2, sticky=tk.W)
        self.audio_rtcp_entry = ttk.Entry(port_frame, width=8)
        self.audio_rtcp_entry.grid(row=1, column=3, padx=5, pady=2)
        self.audio_rtcp_entry.insert(0, "40003")

        # Backchannel ports
        ttk.Label(port_frame, text="Backchannel RTP:").grid(row=2, column=0, padx=5, pady=2, sticky=tk.W)
        self.backchannel_port_entry = ttk.Entry(port_frame, width=8)
        self.backchannel_port_entry.grid(row=2, column=1, padx=5, pady=2)
        self.backchannel_port_entry.insert(0, "40004")

        ttk.Label(port_frame, text="Backchannel RTCP:").grid(row=2, column=2, padx=5, pady=2, sticky=tk.W)
        self.backchannel_rtcp_entry = ttk.Entry(port_frame, width=8)
        self.backchannel_rtcp_entry.grid(row=2, column=3, padx=5, pady=2)
        self.backchannel_rtcp_entry.insert(0, "40005")

        # Audio Enable Checkbox
        self.audio_var = tk.BooleanVar(value=True)
        self.audio_checkbox = ttk.Checkbutton(
            port_frame,
            text="Enable Audio Receive",
            variable=self.audio_var,
            command=self.toggle_audio
        )
        self.audio_checkbox.grid(row=3, column=0, columnspan=2, padx=5, pady=2, sticky=tk.W)

        # Backchannel Enable Checkbox
        self.backchannel_var = tk.BooleanVar(value=True)
        self.backchannel_checkbox = ttk.Checkbutton(
            port_frame,
            text="Enable Audio Backchannel (Send)",
            variable=self.backchannel_var,
            command=self.toggle_backchannel
        )
        self.backchannel_checkbox.grid(row=3, column=2, columnspan=2, padx=5, pady=2, sticky=tk.W)

        # Firewall Management Checkbox
        self.firewall_checkbox = ttk.Checkbutton(
            port_frame,
            text="Auto-manage Windows Firewall (requires Admin)",
            variable=self.firewall_enabled_var
        )
        self.firewall_checkbox.grid(row=4, column=0, columnspan=4, padx=5, pady=2, sticky=tk.W)

        # Verbose Logging Checkbox
        self.verbose_logging_var = tk.BooleanVar(value=False)
        self.verbose_checkbox = ttk.Checkbutton(
            port_frame,
            text="Verbose Logging (show all INFO/RTP messages)",
            variable=self.verbose_logging_var,
            command=self.toggle_verbose_logging
        )
        self.verbose_checkbox.grid(row=5, column=0, columnspan=4, padx=5, pady=2, sticky=tk.W)

        # Audio RX Volume Control Slider
        rx_volume_control_frame = ttk.Frame(port_frame)
        rx_volume_control_frame.grid(row=6, column=0, columnspan=4, padx=5, pady=5, sticky=tk.EW)

        ttk.Label(rx_volume_control_frame, text="Audio RX Volume:").pack(side=tk.LEFT, padx=(0, 5))

        self.rx_volume_slider = ttk.Scale(
            rx_volume_control_frame,
            from_=0.1,
            to=10.0,
            orient=tk.HORIZONTAL,
            length=200,
            command=self.on_rx_volume_change
        )
        self.rx_volume_slider.set(2.0)  # Default 2x amplification
        self.rx_volume_slider.pack(side=tk.LEFT, padx=5)

        # Audio TX Volume Control Slider
        tx_volume_control_frame = ttk.Frame(port_frame)
        tx_volume_control_frame.grid(row=7, column=0, columnspan=4, padx=5, pady=5, sticky=tk.EW)

        ttk.Label(tx_volume_control_frame, text="Audio TX Volume:").pack(side=tk.LEFT, padx=(0, 5))

        self.tx_volume_slider = ttk.Scale(
            tx_volume_control_frame,
            from_=0.1,
            to=10.0,
            orient=tk.HORIZONTAL,
            length=200,
            command=self.on_tx_volume_change
        )
        self.tx_volume_slider.set(2.0)  # Default 2x amplification
        self.tx_volume_slider.pack(side=tk.LEFT, padx=5)

        # Main Control Buttons
        button_frame = ttk.Frame(control_frame)
        button_frame.grid(row=2, column=0, columnspan=6, pady=10, sticky=tk.EW)

        self.play_btn = ttk.Button(button_frame, text="▶ PLAY", command=self.play_stream, width=15)
        self.play_btn.pack(side=tk.LEFT, padx=5)

        self.stop_btn = ttk.Button(button_frame, text="⏹ STOP", command=self.stop_stream, width=15, state=tk.DISABLED)
        self.stop_btn.pack(side=tk.LEFT, padx=5)

        # Progress Label
        progress_frame = ttk.Frame(control_frame)
        progress_frame.grid(row=3, column=0, columnspan=6, sticky=tk.EW, pady=(0, 5))

        self.progress_label = ttk.Label(progress_frame, text="", foreground="blue")
        self.progress_label.pack(side=tk.LEFT, padx=5)

        # ===== LEFT COLUMN - BOTTOM - Log Frame =====
        log_frame = ttk.LabelFrame(main_container, text="RTSP/RTP Messages Log", padding=10)
        log_frame.grid(row=1, column=0, sticky=(tk.N, tk.S, tk.E, tk.W), padx=(0, 5), pady=(5, 5))

        self.log_text = scrolledtext.ScrolledText(log_frame, height=12, wrap=tk.WORD, font=("Courier", 9))
        self.log_text.pack(fill=tk.BOTH, expand=True)

        # Configure log tags
        self.log_text.tag_config("ERROR", foreground="red")
        self.log_text.tag_config("SUCCESS", foreground="green")
        self.log_text.tag_config("INFO", foreground="black")
        self.log_text.tag_config("SENT", foreground="blue")
        self.log_text.tag_config("RECEIVED", foreground="purple")
        self.log_text.tag_config("RTP", foreground="orange")

        clear_btn = ttk.Button(log_frame, text="Clear Log", command=self.clear_log)
        clear_btn.pack(anchor=tk.E, pady=2)

        # ===== RIGHT COLUMN - Video Display (spanning rows 0-1) =====
        video_frame = ttk.LabelFrame(main_container, text="Video Display (MJPEG - Max VGA 640x480)", padding=10)
        video_frame.grid(row=0, column=1, rowspan=2, sticky=(tk.N, tk.S, tk.E, tk.W), padx=(5, 0), pady=(0, 5))

        # Create canvas with fixed VGA size
        self.video_canvas = tk.Canvas(video_frame, bg="black", width=640, height=480)
        self.video_canvas.pack()

        # Initial text on canvas
        self.video_canvas.create_text(
            320, 240,
            text="No video stream\n\nConfigure ports, enter RTSP URL and click PLAY\n\nSupported format: MJPEG\nMax resolution: VGA (640x480)",
            fill="white",
            font=("Arial", 11),
            tags="placeholder"
        )

        # ===== BOTTOM STATUS BAR (spanning both columns) =====
        status_bar_frame = ttk.Frame(main_container, relief=tk.SUNKEN, borderwidth=1)
        status_bar_frame.grid(row=2, column=0, columnspan=2, sticky=(tk.E, tk.W), pady=(5, 0))

        # Firewall Status
        firewall_frame = ttk.Frame(status_bar_frame)
        firewall_frame.pack(side=tk.LEFT, padx=10, pady=5)

        ttk.Label(firewall_frame, text="Firewall:", font=("Arial", 9, "bold")).pack(side=tk.LEFT, padx=(0, 5))
        self.firewall_status_label = ttk.Label(firewall_frame, text="Not configured", foreground="gray", font=("Arial", 9))
        self.firewall_status_label.pack(side=tk.LEFT)

        # Separator
        ttk.Separator(status_bar_frame, orient=tk.VERTICAL).pack(side=tk.LEFT, fill=tk.Y, padx=5)

        # Connection Status
        status_frame = ttk.Frame(status_bar_frame)
        status_frame.pack(side=tk.LEFT, padx=10, pady=5)

        ttk.Label(status_frame, text="Status:", font=("Arial", 9, "bold")).pack(side=tk.LEFT, padx=(0, 5))
        self.status_label = ttk.Label(status_frame, text="Disconnected", foreground="red", font=("Arial", 9, "bold"))
        self.status_label.pack(side=tk.LEFT)

        # Separator
        ttk.Separator(status_bar_frame, orient=tk.VERTICAL).pack(side=tk.LEFT, fill=tk.Y, padx=5)

        # Video FPS and Frame Counter
        video_stats_frame = ttk.Frame(status_bar_frame)
        video_stats_frame.pack(side=tk.LEFT, padx=10, pady=5)

        ttk.Label(video_stats_frame, text="Video:", font=("Arial", 9, "bold")).pack(side=tk.LEFT, padx=(0, 5))
        self.video_stats_label = ttk.Label(video_stats_frame, text="0 fps | 0 frames", foreground="green", font=("Arial", 9))
        self.video_stats_label.pack(side=tk.LEFT)

        # Separator
        ttk.Separator(status_bar_frame, orient=tk.VERTICAL).pack(side=tk.LEFT, fill=tk.Y, padx=5)

        # Audio RX FPS and Frame Counter
        audio_rx_stats_frame = ttk.Frame(status_bar_frame)
        audio_rx_stats_frame.pack(side=tk.LEFT, padx=10, pady=5)

        ttk.Label(audio_rx_stats_frame, text="Audio RX:", font=("Arial", 9, "bold")).pack(side=tk.LEFT, padx=(0, 5))
        self.audio_rx_stats_label = ttk.Label(audio_rx_stats_frame, text="0 fps | 0 pkts", foreground="blue", font=("Arial", 9))
        self.audio_rx_stats_label.pack(side=tk.LEFT)

        # Separator
        ttk.Separator(status_bar_frame, orient=tk.VERTICAL).pack(side=tk.LEFT, fill=tk.Y, padx=5)

        # Audio TX FPS and Frame Counter
        audio_tx_stats_frame = ttk.Frame(status_bar_frame)
        audio_tx_stats_frame.pack(side=tk.LEFT, padx=10, pady=5)

        ttk.Label(audio_tx_stats_frame, text="Audio TX:", font=("Arial", 9, "bold")).pack(side=tk.LEFT, padx=(0, 5))
        self.audio_tx_stats_label = ttk.Label(audio_tx_stats_frame, text="0 fps | 0 pkts", foreground="orange", font=("Arial", 9))
        self.audio_tx_stats_label.pack(side=tk.LEFT)

        # Separator
        ttk.Separator(status_bar_frame, orient=tk.VERTICAL).pack(side=tk.LEFT, fill=tk.Y, padx=5)

        # RX Volume Display on Status Bar
        rx_volume_frame = ttk.Frame(status_bar_frame)
        rx_volume_frame.pack(side=tk.LEFT, padx=10, pady=5)

        ttk.Label(rx_volume_frame, text="RX Vol:", font=("Arial", 9, "bold")).pack(side=tk.LEFT, padx=(0, 5))
        self.rx_volume_status_label = ttk.Label(rx_volume_frame, text="2.0x", foreground="purple", font=("Arial", 9))
        self.rx_volume_status_label.pack(side=tk.LEFT)

        # Separator
        ttk.Separator(status_bar_frame, orient=tk.VERTICAL).pack(side=tk.LEFT, fill=tk.Y, padx=5)

        # TX Volume Display on Status Bar
        tx_volume_frame = ttk.Frame(status_bar_frame)
        tx_volume_frame.pack(side=tk.LEFT, padx=10, pady=5)

        ttk.Label(tx_volume_frame, text="TX Vol:", font=("Arial", 9, "bold")).pack(side=tk.LEFT, padx=(0, 5))
        self.tx_volume_status_label = ttk.Label(tx_volume_frame, text="2.0x", foreground="brown", font=("Arial", 9))
        self.tx_volume_status_label.pack(side=tk.LEFT)


    def toggle_audio(self):
        """Handle audio checkbox toggle"""
        self.audio_enabled = self.audio_var.get()
        if self.audio_enabled:
            self.log_message("Audio receive ENABLED", "SUCCESS")
        else:
            self.log_message("Audio receive DISABLED", "INFO")

    def toggle_backchannel(self):
        """Handle backchannel checkbox toggle"""
        self.backchannel_enabled = self.backchannel_var.get()
        if self.backchannel_enabled:
            self.log_message("Audio backchannel (Send) ENABLED", "SUCCESS")
        else:
            self.log_message("Audio backchannel (Send) DISABLED", "INFO")

    def toggle_verbose_logging(self):
        """Handle verbose logging checkbox toggle"""
        verbose = self.verbose_logging_var.get()
        if verbose:
            self.log_message("Verbose logging ENABLED - showing all messages", "SUCCESS")
        else:
            self.log_message("Verbose logging DISABLED - showing only warnings and errors", "INFO")

    def on_rx_volume_change(self, value):
        """Handle RX volume slider change"""
        self.audio_rx_volume = float(value)

        # Update status bar label
        if hasattr(self, 'rx_volume_status_label'):
            self.rx_volume_status_label.config(text=f"{self.audio_rx_volume:.1f}x")

        # Only log on verbose mode
        if hasattr(self, 'verbose_logging_var') and self.verbose_logging_var.get():
            if self.audio_rx_volume > 1.0:
                self.log_message(f"Audio RX volume set to {self.audio_rx_volume:.1f}x (amplified)", "INFO")
            else:
                self.log_message(f"Audio RX volume set to {self.audio_rx_volume:.1f}x", "INFO")

    def on_tx_volume_change(self, value):
        """Handle TX volume slider change"""
        self.audio_tx_volume = float(value)

        # Update status bar label
        if hasattr(self, 'tx_volume_status_label'):
            self.tx_volume_status_label.config(text=f"{self.audio_tx_volume:.1f}x")

        # Only log on verbose mode
        if hasattr(self, 'verbose_logging_var') and self.verbose_logging_var.get():
            if self.audio_tx_volume > 1.0:
                self.log_message(f"Audio TX volume set to {self.audio_tx_volume:.1f}x (amplified)", "INFO")
            else:
                self.log_message(f"Audio TX volume set to {self.audio_tx_volume:.1f}x", "INFO")

    def update_fps_display(self):
        """Update FPS and frame count display on status bar"""
        if not self.is_playing:
            return

        # Update video stats
        video_fps = self.video_fps_counter.get_fps()
        video_frames = self.video_fps_counter.get_total_frames()
        self.video_stats_label.config(text=f"{video_fps:.1f} fps | {video_frames} frames")

        # Update audio RX stats
        audio_rx_fps = self.audio_rx_fps_counter.get_fps()
        audio_rx_packets = self.audio_rx_fps_counter.get_total_frames()
        self.audio_rx_stats_label.config(text=f"{audio_rx_fps:.1f} fps | {audio_rx_packets} pkts")

        # Update audio TX stats
        audio_tx_fps = self.audio_tx_fps_counter.get_fps()
        audio_tx_packets = self.audio_tx_fps_counter.get_total_frames()
        self.audio_tx_stats_label.config(text=f"{audio_tx_fps:.1f} fps | {audio_tx_packets} pkts")

        # Schedule next update (every 500ms)
        if self.is_playing:
            self.fps_update_timer = self.root.after(500, self.update_fps_display)

    def clear_log(self):
        self.log_text.delete(1.0, tk.END)

    def log_message(self, message, level="INFO"):
        """Log message with verbose mode filtering"""
        # If verbose mode is disabled, only show ERROR and WARNING messages
        if not self.verbose_logging_var.get():
            if level not in ["ERROR", "WARNING"]:
                return  # Skip INFO, SUCCESS, SENT, RECEIVED, RTP messages

        timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        self.log_text.insert(tk.END, f"[{timestamp}] [{level}] {message}\n", level)
        self.log_text.see(tk.END)

    def update_progress(self, message):
        self.progress_label.config(text=message)
        self.root.update_idletasks()

    def parse_rtsp_url(self, url):
        """Parse RTSP URL"""
        pattern = r'rtsp://([^:/]+):?(\d+)?(/.*)?'
        match = re.match(pattern, url)
        if match:
            host = match.group(1)
            port = int(match.group(2)) if match.group(2) else 554
            path = match.group(3) if match.group(3) else "/"
            return host, port, path
        return None, None, None

    def get_ports_from_ui(self):
        """Get port configuration from UI"""
        try:
            self.video_rtp_port = int(self.video_port_entry.get())
            self.video_rtcp_port = int(self.video_rtcp_entry.get())
            self.audio_rtp_port = int(self.audio_port_entry.get())
            self.audio_rtcp_port = int(self.audio_rtcp_entry.get())
            self.backchannel_rtp_port = int(self.backchannel_port_entry.get())
            self.backchannel_rtcp_port = int(self.backchannel_rtcp_entry.get())

            if (self.video_rtp_port < 40000 or self.video_rtcp_port < 40000 or
                self.audio_rtp_port < 40000 or self.audio_rtcp_port < 40000 or
                self.backchannel_rtp_port < 40000 or self.backchannel_rtcp_port < 40000):
                messagebox.showerror("Error", "All client UDP ports must be greater than 40000")
                return False

            return True
        except ValueError:
            messagebox.showerror("Error", "Invalid port number. Please enter valid integers.")
            return False

    def get_local_ip(self):
        """Get the local IP address"""
        try:
            # Create a temporary socket to determine the local IP
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            # Connect to the RTSP server (doesn't actually send data)
            s.connect((self.server_address, self.server_port))
            local_ip = s.getsockname()[0]
            s.close()
            return local_ip
        except:
            # Fallback method
            return socket.gethostbyname(socket.gethostname())

    def create_udp_sockets(self):
        """Create UDP sockets for RTP/RTCP"""
        try:
            self.video_rtp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.video_rtp_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.video_rtp_socket.bind(('0.0.0.0', self.video_rtp_port))
            self.video_rtp_socket.settimeout(5.0)
            self.log_message(f"Video RTP socket bound to port {self.video_rtp_port}", "SUCCESS")

            # Verify the socket is bound correctly
            actual_addr = self.video_rtp_socket.getsockname()
            self.log_message(f"✓ Video RTP socket bound to {actual_addr[0]}:{actual_addr[1]}", "SUCCESS")

            if self.audio_enabled and self.audio_available:
                self.audio_rtp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                self.audio_rtp_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                self.audio_rtp_socket.bind(('0.0.0.0', self.audio_rtp_port))
                self.audio_rtp_socket.settimeout(5.0)
                self.log_message(f"Audio RTP socket bound to port {self.audio_rtp_port}", "SUCCESS")

                actual_addr = self.audio_rtp_socket.getsockname()
                self.log_message(f"✓ Audio RTP socket bound to {actual_addr[0]}:{actual_addr[1]}", "SUCCESS")

            # Create backchannel socket for sending audio
            if self.backchannel_enabled and self.backchannel_available:
                self.backchannel_rtp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                self.backchannel_rtp_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                self.backchannel_rtp_socket.bind(('0.0.0.0', self.backchannel_rtp_port))
                self.log_message(f"Backchannel RTP socket bound to port {self.backchannel_rtp_port}", "SUCCESS")

                actual_addr = self.backchannel_rtp_socket.getsockname()
                self.log_message(f"✓ Backchannel RTP socket bound to {actual_addr[0]}:{actual_addr[1]}", "SUCCESS")

            return True
        except Exception as e:
            self.log_message(f"Failed to create UDP sockets: {str(e)}", "ERROR")
            messagebox.showerror("Error", f"Failed to bind UDP ports: {str(e)}")
            return False

    def send_rtsp_request(self, method, track_url=None, include_session=False):
        """Send RTSP request and receive response"""
        try:
            url = track_url if track_url else self.rtsp_url

            request = f"{method} {url} RTSP/1.0\r\n"
            request += f"CSeq: {self.seq_number}\r\n"
            request += "User-Agent: Python RTSP/RTP UDP Client (MJPEG + Two-Way Audio)\r\n"

            if include_session and self.session_id:
                request += f"Session: {self.session_id}\r\n"

            if method == "SETUP":
                # Determine which track we're setting up
                if track_url and "trackID=2" in track_url:
                    # Backchannel setup (sendonly)
                    rtp_port = self.backchannel_rtp_port
                    rtcp_port = self.backchannel_rtcp_port
                    request += f"Transport: RTP/AVP;unicast;client_port={rtp_port}-{rtcp_port};mode=record\r\n"
                elif track_url and "trackID=1" in track_url:
                    # Audio receive
                    rtp_port = self.audio_rtp_port
                    rtcp_port = self.audio_rtcp_port
                    request += f"Transport: RTP/AVP;unicast;client_port={rtp_port}-{rtcp_port}\r\n"
                else:
                    # Video
                    rtp_port = self.video_rtp_port
                    rtcp_port = self.video_rtcp_port
                    request += f"Transport: RTP/AVP;unicast;client_port={rtp_port}-{rtcp_port}\r\n"
            elif method == "DESCRIBE":
                request += "Accept: application/sdp\r\n"
                # Add ONVIF backchannel requirement if backchannel is enabled
                if self.backchannel_enabled:
                    request += "Require: www.onvif.org/ver20/backchannel\r\n"
                    self.log_message("Added ONVIF backchannel requirement to DESCRIBE request", "INFO")

            request += "\r\n"

            self.rtsp_socket.send(request.encode())
            self.log_message(f">>> SENT {method} REQUEST <<<\n{request}", "SENT")

            response = self.rtsp_socket.recv(4096).decode()
            self.log_message(f"<<< RECEIVED {method} RESPONSE <<<\n{response}", "RECEIVED")

            self.seq_number += 1
            return response

        except Exception as e:
            self.log_message(f"RTSP request error: {str(e)}", "ERROR")
            return None

    def parse_sdp(self, response):
        """Parse SDP from DESCRIBE response to find media tracks and audio format"""
        tracks = {'video': None, 'audio': None, 'backchannel': None}

        sdp_start = response.find('\r\n\r\n')
        if sdp_start == -1:
            return tracks

        sdp = response[sdp_start+4:]
        lines = sdp.split('\r\n')

        # Two-pass parsing: First collect all attributes per media, then process
        media_streams = []  # List of dicts, each containing attributes for one media stream
        current_stream = None

        # PASS 1: Collect all attributes for each media stream
        for line in lines:
            if line.startswith('m='):
                # Save previous stream if exists
                if current_stream is not None:
                    media_streams.append(current_stream)

                # Start new stream
                current_stream = {
                    'type': None,
                    'direction': None,
                    'control': None,
                    'rtpmap': None,
                    'payload_type': None
                }

                if 'video' in line:
                    current_stream['type'] = 'video'
                    self.log_message("Found video media description in SDP", "INFO")

                    # Parse video format: m=video <port> RTP/AVP <payload_type>
                    parts = line.split()
                    if len(parts) >= 4:
                        current_stream['payload_type'] = int(parts[3])

                elif 'audio' in line:
                    current_stream['type'] = 'audio'
                    self.log_message("Found audio media description in SDP", "INFO")

                    # Parse audio format: m=audio <port> RTP/AVP <payload_type>
                    parts = line.split()
                    if len(parts) >= 4:
                        current_stream['payload_type'] = int(parts[3])

            elif line.startswith('a=sendonly') and current_stream:
                current_stream['direction'] = 'sendonly'
                stream_type = current_stream.get('type', 'unknown')
                self.log_message(f"Found sendonly {stream_type} stream (client sends to server)", "INFO")

            elif line.startswith('a=recvonly') and current_stream:
                current_stream['direction'] = 'recvonly'
                stream_type = current_stream.get('type', 'unknown')
                self.log_message(f"Found recvonly {stream_type} stream (client receives from server)", "INFO")

            elif line.startswith('a=control:') and current_stream:
                current_stream['control'] = line.split(':', 1)[1]

            elif line.startswith('a=rtpmap:') and current_stream:
                # Store rtpmap for both audio and video streams
                current_stream['rtpmap'] = line

        # Don't forget the last stream
        if current_stream is not None:
            media_streams.append(current_stream)

        # PASS 2: Process collected attributes and build track URLs
        for stream in media_streams:
            media_type = stream['type']
            media_direction = stream['direction']
            control_attr = stream['control']

            # Validate required attributes
            if not media_type:
                self.log_message("⚠ Stream missing media type, skipping", "WARNING")
                continue

            if not control_attr:
                self.log_message(f"⚠ {media_type} stream missing control attribute (trackID), skipping", "WARNING")
                continue

            # Direction is required for all streams to determine their purpose
            if media_direction is None:
                self.log_message(
                    f"⚠ {media_type} stream missing direction attribute (sendonly/recvonly), skipping: {control_attr}",
                    "WARNING"
                )
                continue

            # Build track URL from control attribute
            if control_attr.startswith('rtsp://'):
                # Already a complete URL
                track_url = control_attr
            else:
                # Relative path - combine with base URL
                base_url = self.rtsp_url.rstrip('/')
                # Control attribute should already contain trackID (e.g., "trackID=0" or "track0")
                # Just append it to base URL
                track_url = f"{base_url}/{control_attr}"

            # Store track based on type and direction
            if media_type == 'video':
                if media_direction == 'recvonly':
                    # Normal video stream (server sends to client)
                    tracks['video'] = track_url
                    self.log_message(f"Found video receive track: {track_url}", "SUCCESS")
                elif media_direction == 'sendonly':
                    # Video backchannel (client sends to server) - rare but possible
                    self.log_message(f"Found video sendonly track (not supported): {track_url}", "WARNING")

                # Process video rtpmap if present
                if stream['rtpmap']:
                    line = stream['rtpmap']
                    parts = line.split()
                    if len(parts) >= 2:
                        payload_info = parts[0].split(':')[1]
                        codec_info = parts[1].split('/')

                        if len(codec_info) >= 1:
                            video_codec = codec_info[0].upper()
                            clock_rate = int(codec_info[1]) if len(codec_info) >= 2 else 90000
                            self.log_message(
                                f"Video format from SDP: {video_codec}, clock rate={clock_rate}Hz, PT={payload_info}",
                                "SUCCESS"
                            )

            elif media_type == 'audio':
                if media_direction == 'sendonly':
                    # This is backchannel (client sends audio to server)
                    tracks['backchannel'] = track_url
                    self.backchannel_available = True
                    self.log_message(f"Found audio backchannel track: {track_url}", "SUCCESS")
                elif media_direction == 'recvonly':
                    # This is receive audio (server sends audio to client)
                    tracks['audio'] = track_url
                    self.audio_available = True
                    self.log_message(f"Found audio receive track: {track_url}", "SUCCESS")

                # Process audio rtpmap for codec information
                if stream['rtpmap']:
                    line = stream['rtpmap']
                    parts = line.split()
                    if len(parts) >= 2:
                        payload_info = parts[0].split(':')[1]
                        codec_info = parts[1].split('/')

                        if len(codec_info) >= 2:
                            codec = codec_info[0].upper()
                            sample_rate = int(codec_info[1])
                            channels = int(codec_info[2]) if len(codec_info) >= 3 else 1

                            if media_direction == 'sendonly':
                                # Backchannel format
                                self.backchannel_codec = codec
                                self.backchannel_sample_rate = sample_rate
                                self.backchannel_channels = channels
                                self.backchannel_payload_type = int(payload_info)
                                self.log_message(
                                    f"Backchannel format from SDP: {codec}, {sample_rate}Hz, {channels} channel(s), PT={payload_info}",
                                    "SUCCESS"
                                )
                            elif media_direction == 'recvonly':
                                # Receive audio format
                                self.audio_codec = codec
                                self.audio_sample_rate = sample_rate
                                self.audio_channels = channels
                                self.audio_payload_type = int(payload_info)
                                self.log_message(
                                    f"Audio receive format from SDP: {codec}, {sample_rate}Hz, {channels} channel(s), PT={payload_info}",
                                    "SUCCESS"
                                )

                # Handle payload type if rtpmap wasn't present
                elif stream['payload_type'] is not None:
                    if media_direction == 'recvonly':
                        self.audio_payload_type = stream['payload_type']
                        self.log_message(f"Audio receive payload type: {stream['payload_type']}", "INFO")
                    elif media_direction == 'sendonly':
                        self.backchannel_payload_type = stream['payload_type']
                        self.log_message(f"Backchannel payload type: {stream['payload_type']}", "INFO")

        # Final summary logging
        if tracks['video']:
            self.log_message("✓ Video stream available", "SUCCESS")
        else:
            self.log_message("✗ No video stream found", "ERROR")

        if tracks['audio']:
            self.log_message("✓ Audio receive stream available", "SUCCESS")
        else:
            self.log_message("✗ No audio receive stream found in SDP description", "INFO")
            self.audio_available = False

        if tracks['backchannel']:
            self.log_message("✓ Audio backchannel available", "SUCCESS")
        else:
            self.log_message("✗ No audio backchannel found in SDP description", "INFO")
            self.backchannel_available = False

        return tracks

    def send_options(self):
        """Send OPTIONS request"""
        response = self.send_rtsp_request("OPTIONS")
        if response and "200 OK" in response:
            self.log_message("OPTIONS command successful", "SUCCESS")
            return True
        return False

    def send_describe(self):
        """Send DESCRIBE request"""
        response = self.send_rtsp_request("DESCRIBE")
        if response and "200 OK" in response:
            self.log_message("DESCRIBE command successful", "SUCCESS")

            tracks = self.parse_sdp(response)
            self.video_control_track = tracks['video']
            self.audio_control_track = tracks['audio']
            self.backchannel_control_track = tracks['backchannel']

            if self.video_control_track:
                self.log_message(f"Video track URL: {self.video_control_track}", "INFO")
            if self.audio_control_track:
                self.log_message(f"Audio track URL: {self.audio_control_track}", "INFO")
            if self.backchannel_control_track:
                self.log_message(f"Backchannel track URL: {self.backchannel_control_track}", "INFO")

            return True
        return False

    def send_setup(self):
        """Send SETUP request for video, audio receive, and optionally backchannel"""
        # Setup video (trackID=0)
        video_url = self.video_control_track if self.video_control_track else f"{self.rtsp_url}/trackID=0"
        response = self.send_rtsp_request("SETUP", track_url=video_url)

        if response and "200 OK" in response:
            session_match = re.search(r'Session:\s*(\S+)', response)
            if session_match:
                self.session_id = session_match.group(1).split(';')[0]
                self.log_message(f"Session ID: {self.session_id}", "SUCCESS")

            self.log_message("Video SETUP successful (trackID=0)", "SUCCESS")

            # Parse server's RTP port for video (optional, for logging)
            transport_match = re.search(r'server_port=(\d+)-(\d+)', response)
            if transport_match:
                server_video_rtp = transport_match.group(1)
                server_video_rtcp = transport_match.group(2)
                self.log_message(f"Server video ports: RTP={server_video_rtp}, RTCP={server_video_rtcp}", "INFO")

            # Setup audio receive (trackID=1)
            if self.audio_available and self.audio_enabled and self.audio_control_track:
                self.log_message("Setting up audio receive stream (trackID=1)...", "INFO")
                audio_url = self.audio_control_track
                audio_response = self.send_rtsp_request("SETUP", track_url=audio_url, include_session=True)
                if audio_response and "200 OK" in audio_response:
                    self.log_message("Audio receive SETUP successful (trackID=1)", "SUCCESS")

                    # Parse server's RTP port for audio receive (server sends FROM these ports)
                    transport_match = re.search(r'server_port=(\d+)-(\d+)', audio_response)
                    if transport_match:
                        server_audio_rtp = transport_match.group(1)
                        server_audio_rtcp = transport_match.group(2)
                        self.log_message(
                            f"Server audio ports (server sends FROM): RTP={server_audio_rtp}, RTCP={server_audio_rtcp}",
                            "INFO"
                        )
                else:
                    self.log_message("Audio receive SETUP failed, continuing without audio", "ERROR")
                    self.audio_available = False
            elif not self.audio_available:
                self.log_message("Skipping audio receive SETUP - no audio stream in SDP", "INFO")
            elif not self.audio_enabled:
                self.log_message("Skipping audio receive SETUP - disabled by user", "INFO")

            # Setup backchannel (trackID=2) - SEPARATE SETUP REQUEST
            if self.backchannel_available and self.backchannel_enabled and self.backchannel_control_track:
                self.log_message("Setting up audio backchannel stream (trackID=2)...", "INFO")
                backchannel_url = self.backchannel_control_track
                backchannel_response = self.send_rtsp_request("SETUP", track_url=backchannel_url, include_session=True)

                if backchannel_response and "200 OK" in backchannel_response:
                    self.log_message("Audio backchannel SETUP successful (trackID=2)", "SUCCESS")

                    # Parse server's destination port for backchannel (client sends TO these ports)
                    transport_match = re.search(r'server_port=(\d+)-(\d+)', backchannel_response)
                    if transport_match:
                        self.server_backchannel_port = int(transport_match.group(1))  # RTP port
                        self.server_backchannel_rtcp_port = int(transport_match.group(2))  # RTCP port
                        self.server_backchannel_ip = self.server_address
                        self.log_message(
                            f"✓ Backchannel destination (client sends TO): {self.server_backchannel_ip}:{self.server_backchannel_port} (RTP), {self.server_backchannel_rtcp_port} (RTCP)",
                            "SUCCESS"
                        )
                        self.log_message(
                            f"Client will send audio FROM local port {self.backchannel_rtp_port} TO server port {self.server_backchannel_port}",
                            "INFO"
                        )
                    else:
                        # Server didn't provide server_port for backchannel - CRITICAL ERROR
                        self.log_message("✗ Server did not provide server_port in backchannel (trackID=2) SETUP response", "ERROR")
                        self.log_message("Backchannel SETUP failed - no destination port available", "ERROR")
                        self.backchannel_available = False
                        self.server_backchannel_port = None
                        self.server_backchannel_ip = None
                else:
                    self.log_message("Audio backchannel SETUP request failed (trackID=2)", "ERROR")
                    self.backchannel_available = False
                    self.server_backchannel_port = None
                    self.server_backchannel_ip = None
            elif not self.backchannel_available:
                self.log_message("Skipping backchannel SETUP - no backchannel track in SDP", "INFO")
            elif not self.backchannel_enabled:
                self.log_message("Skipping backchannel SETUP - disabled by user", "INFO")

            return True
        return False

    def send_play(self):
        """Send PLAY request"""
        response = self.send_rtsp_request("PLAY", include_session=True)
        if response and "200 OK" in response:
            self.log_message("PLAY command successful", "SUCCESS")
            return True
        return False

    def send_teardown(self):
        """Send TEARDOWN request"""
        response = self.send_rtsp_request("TEARDOWN", include_session=True)
        if response and "200 OK" in response:
            self.log_message("TEARDOWN command successful", "SUCCESS")
            return True
        return False

    def stop_stream(self):
        """Stop streaming and cleanup"""
        self.log_message("Stopping stream...", "INFO")

        # Set flag first to stop all threads
        self.is_playing = False

        # Cancel FPS update timer
        if self.fps_update_timer:
            self.root.after_cancel(self.fps_update_timer)
            self.fps_update_timer = None

        # Send TEARDOWN before closing sockets
        if self.rtsp_socket:
            try:
                self.send_teardown()
            except:
                pass

        # Give threads time to exit gracefully
        time.sleep(0.3)

        # Close audio streams
        if self.audio_stream:
            try:
                self.audio_stream.stop_stream()
                self.audio_stream.close()
            except:
                pass
            self.audio_stream = None

        if self.backchannel_input_stream:
            try:
                self.backchannel_input_stream.stop_stream()
                self.backchannel_input_stream.close()
            except:
                pass
            self.backchannel_input_stream = None

        if self.pyaudio_instance:
            try:
                self.pyaudio_instance.terminate()
            except:
                pass
            self.pyaudio_instance = None

        # Close sockets
        if self.video_rtp_socket:
            try:
                self.video_rtp_socket.close()
            except:
                pass
            self.video_rtp_socket = None

        if self.audio_rtp_socket:
            try:
                self.audio_rtp_socket.close()
            except:
                pass
            self.audio_rtp_socket = None

        if self.backchannel_rtp_socket:
            try:
                self.backchannel_rtp_socket.close()
            except:
                pass
            self.backchannel_rtp_socket = None

        if self.rtsp_socket:
            try:
                self.rtsp_socket.close()
            except:
                pass
            self.rtsp_socket = None

        # Wait for threads to finish (with timeout)
        if self.video_thread and self.video_thread.is_alive():
            self.video_thread.join(timeout=1.0)

        if self.audio_thread and self.audio_thread.is_alive():
            self.audio_thread.join(timeout=1.0)

        if self.backchannel_thread and self.backchannel_thread.is_alive():
            self.backchannel_thread.join(timeout=1.0)

        # Remove firewall rules
        if self.firewall_enabled_var.get() and self.firewall_manager:
            removed_count = self.firewall_manager.remove_firewall_rules()
            if removed_count > 0:
                self.log_message(f"✓ Removed {removed_count} firewall rule(s)", "SUCCESS")
                self.firewall_status_label.config(text="Rules removed", foreground="gray")

        # Reset FPS counters
        self.video_fps_counter.reset()
        self.audio_rx_fps_counter.reset()
        self.audio_tx_fps_counter.reset()

        self.frame_buffer = []
        self.frame_count = 0
        self.last_sequence = -1
        self.use_generated_audio = False
        self.generated_audio_buffer = None
        self.generated_audio_position = 0

        self.reset_buttons()
        self.status_label.config(text="Disconnected", foreground="red")
        self.progress_label.config(text="")
        self.video_stats_label.config(text="0 fps | 0 frames")
        self.audio_rx_stats_label.config(text="0 fps | 0 pkts")
        self.audio_tx_stats_label.config(text="0 fps | 0 pkts")

        self.video_canvas.delete("all")
        self.video_canvas.create_text(
            320, 240,
            text="No video stream\n\nConfigure ports, enter RTSP URL and click PLAY\n\nSupported format: MJPEG\nMax resolution: VGA (640x480)",
            fill="white",
            font=("Arial", 11),
            tags="placeholder"
        )

        self.log_message("Stream stopped and cleanup completed", "INFO")

    def play_stream(self):
        """Start RTSP/RTP streaming sequence"""
        if not self.url_entry.get().strip():
            messagebox.showwarning("Warning", "Please enter an RTSP URL")
            return

        if not self.get_ports_from_ui():
            return

        self.play_btn.config(state=tk.DISABLED)
        self.audio_checkbox.config(state=tk.DISABLED)
        self.backchannel_checkbox.config(state=tk.DISABLED)

        thread = threading.Thread(target=self._play_stream_sequence, daemon=True)
        thread.start()

    def _play_stream_sequence(self):
        """Execute complete RTSP handshake and start RTP reception"""
        try:
            self.log_message("=" * 70, "INFO")
            self.log_message("Starting RTSP/RTP UDP connection sequence...", "INFO")
            self.log_message("=" * 70, "INFO")

            self.frame_count = 0
            self.last_sequence = -1
            self.frame_buffer = []
            self.audio_available = False
            self.backchannel_available = False
            self.audio_codec = None
            self.audio_sample_rate = 8000
            self.audio_channels = 1
            self.audio_payload_type = None
            self.backchannel_codec = 'L16'
            self.backchannel_sample_rate = 8000
            self.backchannel_channels = 1
            self.backchannel_payload_type = 96
            self.use_generated_audio = False

            # Reset FPS counters
            self.video_fps_counter.reset()
            self.audio_rx_fps_counter.reset()
            self.audio_tx_fps_counter.reset()

            self.rtsp_url = self.url_entry.get().strip()
            host, port, path = self.parse_rtsp_url(self.rtsp_url)

            if not host:
                self.log_message("Invalid RTSP URL format", "ERROR")
                self.reset_buttons()
                return

            self.server_address = host
            self.server_port = port

            # Get ports from UI
            if not self.get_ports_from_ui():
                self.reset_buttons()
                return

            # Add firewall management
            if self.firewall_enabled_var.get():
                self.update_progress("Configuring Windows Firewall...")
                ports_to_open = [
                    self.video_rtp_port,
                    self.video_rtcp_port,
                    self.audio_rtp_port,
                    self.audio_rtcp_port,
                    self.backchannel_rtp_port,
                    self.backchannel_rtcp_port
                ]

                success_count, total_count = self.firewall_manager.add_firewall_rules(ports_to_open)

                if success_count == total_count:
                    self.log_message(f"✓ Added firewall rules for {success_count} UDP ports", "SUCCESS")
                    self.root.after(0, lambda: self.firewall_status_label.config(
                        text=f"{success_count} ports opened",
                        foreground="green"
                    ))
                elif success_count > 0:
                    self.log_message(f"⚠ Partially added firewall rules ({success_count}/{total_count})", "ERROR")
                    self.root.after(0, lambda: self.firewall_status_label.config(
                        text=f"{success_count}/{total_count} ports opened",
                        foreground="orange"
                    ))
                else:
                    self.log_message("✗ Failed to add firewall rules - run as Administrator", "WARNING")
                    self.root.after(0, lambda: self.firewall_status_label.config(
                        text="Failed (run as Admin)",
                        foreground="red"
                    ))

                    # Ask user if they want to continue
                    def ask_continue():
                        response = messagebox.askyesno(
                            "Firewall Rules Failed",
                            "Failed to add Windows Firewall rules.\n\n"
                            "This may be because:\n"
                            "1. Script is not running as Administrator\n"
                            "2. Firewall is disabled\n\n"
                            "Packets may be blocked by firewall.\n\n"
                            "Continue anyway?",
                            icon='warning'
                        )
                        return response

                    # Must ask on main thread
                    continue_anyway = [False]
                    def check_continue():
                        continue_anyway[0] = ask_continue()

                    self.root.after(0, check_continue)
                    time.sleep(0.5)  # Wait for dialog

                    if not continue_anyway[0]:
                        self.reset_buttons()
                        return
            else:
                self.log_message("Firewall auto-management disabled by user", "INFO")
                self.root.after(0, lambda: self.firewall_status_label.config(
                    text="Manual mode",
                    foreground="gray"
                ))

            self.update_progress("Connecting to RTSP server...")

            self.rtsp_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.rtsp_socket.settimeout(10)
            self.rtsp_socket.connect((host, port))
            self.log_message(f"Connected to RTSP server {host}:{port}", "SUCCESS")

            self.update_progress("Step 1/4: Sending OPTIONS...")
            if not self.send_options():
                self.log_message("OPTIONS failed", "ERROR")
                self.reset_buttons()
                return
            time.sleep(0.2)

            self.update_progress("Step 2/4: Sending DESCRIBE...")
            if not self.send_describe():
                self.log_message("DESCRIBE failed", "ERROR")
                self.reset_buttons()
                return
            time.sleep(0.2)

            self.update_progress("Creating UDP sockets...")
            if not self.create_udp_sockets():
                self.reset_buttons()
                return

            self.update_progress("Step 3/4: Sending SETUP...")
            if not self.send_setup():
                self.log_message("SETUP failed", "ERROR")
                self.reset_buttons()
                return
            time.sleep(0.2)

            self.update_progress("Step 4/4: Sending PLAY...")
            if not self.send_play():
                self.log_message("PLAY failed", "ERROR")
                self.reset_buttons()
                return

            # PLAY response received successfully - NOW start streaming
            self.is_playing = True
            self.connection_state = "PLAYING"

            self.update_progress("Streaming via UDP...")
            self.root.after(0, self.enable_playback_controls)
            self.root.after(0, lambda: self.video_canvas.delete("placeholder"))

            # Start FPS update timer
            self.root.after(0, self.update_fps_display)

            # Start video RTP reception
            self.start_video_rtp_reception()

            # Start audio RTP reception (if available)
            if self.audio_available and self.audio_enabled and self.audio_rtp_socket:
                self.start_audio_rtp_reception()
            else:
                if not self.audio_available:
                    self.root.after(0, lambda: self.audio_rx_stats_label.config(text="Not available"))

            # Start backchannel transmission (if available) - AFTER PLAY response
            if self.backchannel_available and self.backchannel_enabled:
                self.log_message("=" * 70, "INFO")
                self.log_message("PLAY response received - starting backchannel transmission", "SUCCESS")
                self.start_backchannel()
            else:
                if not self.backchannel_available:
                    self.root.after(0, lambda: self.audio_tx_stats_label.config(text="Not available"))

            self.log_message("=" * 70, "SUCCESS")
            stream_info = "MJPEG video"
            if self.audio_available and self.audio_enabled:
                stream_info += f" + audio RX ({self.audio_codec} {self.audio_sample_rate}Hz {self.audio_channels}ch)"
            if self.backchannel_available and self.backchannel_enabled:
                stream_info += f" + audio TX ({self.backchannel_codec} {self.backchannel_sample_rate}Hz {self.backchannel_channels}ch)"
            self.log_message(f"RTSP/RTP connection established - {stream_info}", "SUCCESS")
            self.log_message("=" * 70, "SUCCESS")

        except Exception as e:
            self.log_message(f"Connection sequence failed: {str(e)}", "ERROR")
            messagebox.showerror("Error", f"Connection failed: {str(e)}")
            self.reset_buttons()

    def start_video_rtp_reception(self):
        """Start video RTP packet reception thread"""
        if self.video_thread is None or not self.video_thread.is_alive():
            self.video_thread = threading.Thread(target=self.receive_video_rtp, daemon=True)
            self.video_thread.start()

    def receive_video_rtp(self):
        """Receive and process video RTP packets via UDP (MJPEG format)"""
        self.log_message("Starting video RTP reception (trackID=0, MJPEG format)...", "INFO")
        packet_count = 0

        try:
            while self.is_playing:
                try:
                    data, addr = self.video_rtp_socket.recvfrom(65536)
                    packet_count += 1

                    rtp_packet = RTPPacket()
                    if rtp_packet.decode(data):
                        payload = rtp_packet.get_payload()

                        if packet_count % 50 == 0:
                            self.log_message(
                                f"Video RTP: seq={rtp_packet.seqnum}, ts={rtp_packet.timestamp}, "
                                f"marker={rtp_packet.marker}, payload={len(payload)} bytes",
                                "RTP"
                            )

                        self.process_mjpeg_payload(payload, rtp_packet)

                except socket.timeout:
                    if packet_count == 0:
                        self.log_message("No video packets received - check server is streaming", "WARNING")
                    continue
                except Exception as e:
                    if self.is_playing:
                        self.log_message(f"Video RTP error: {str(e)}", "ERROR")
                    break

            self.log_message(f"Video RTP reception stopped. Total packets: {packet_count}", "INFO")

        except Exception as e:
            self.log_message(f"Video RTP thread error: {str(e)}", "ERROR")

    def process_mjpeg_payload(self, payload, rtp_packet):
        """Process MJPEG RTP payload - simple PDU concatenation"""
        try:
            if len(payload) < 8:
                self.log_message(f"Payload too short: {len(payload)} bytes", "ERROR")
                return

            # Parse RFC 2435 JPEG header (8 bytes)
            type_specific = payload[0]
            fragment_offset = (payload[1] << 16) | (payload[2] << 8) | payload[3]
            jpeg_type = payload[4]
            q_value = payload[5]
            width = payload[6] * 8  # Convert to actual pixels
            height = payload[7] * 8

            # PDU data starts after the 8-byte JPEG header
            pdu_data = payload[8:]

            # Log first packet info
            if fragment_offset == 0 and self.frame_count % 10 == 0:
                self.log_message(
                    f"JPEG Header: Type={jpeg_type}, Q={q_value}, "
                    f"Size={width}x{height}, Fragment offset={fragment_offset}, PDU size={len(pdu_data)}",
                    "INFO"
                )

            # For fragment offset 0, start new frame
            if fragment_offset == 0:
                # Clear buffer and start collecting PDUs
                self.frame_buffer = []
                self.log_message(f"Starting new frame collection (seq={rtp_packet.seqnum})", "RTP")

            # Append PDU data to buffer
            self.frame_buffer.append(pdu_data)

            # Check if this is the last packet (marker bit = 1)
            if rtp_packet.marker == 1:
                # Combine all PDUs into complete MJPEG frame
                complete_frame = b''.join(self.frame_buffer)

                self.log_message(
                    f"Complete MJPEG frame assembled: {len(complete_frame)} bytes from {len(self.frame_buffer)} PDUs",
                    "SUCCESS"
                )

                # Clear buffer
                self.frame_buffer = []

                # Decode and display the complete MJPEG frame
                self.decode_and_display_frame(complete_frame)

        except Exception as e:
            self.log_message(f"MJPEG processing error: {str(e)}", "ERROR")
            import traceback
            self.log_message(f"Traceback: {traceback.format_exc()}", "ERROR")
            self.frame_buffer = []

    def decode_and_display_frame(self, frame_data):
        """Decode MJPEG frame and display it"""
        try:
            if len(frame_data) == 0:
                self.log_message("Empty frame data received", "ERROR")
                return

            # Verify JPEG markers
            if frame_data[:2] != b'\xff\xd8':
                self.log_message(f"Invalid JPEG SOI marker, starts with: {frame_data[:4].hex()}", "ERROR")
                return

            if frame_data[-2:] != b'\xff\xd9':
                self.log_message(f"Invalid JPEG EOI marker, ends with: {frame_data[-4:].hex()}", "ERROR")
                return

            # Decode JPEG frame using OpenCV
            nparr = np.frombuffer(frame_data, np.uint8)
            frame = cv2.imdecode(nparr, cv2.IMREAD_COLOR)

            if frame is not None:
                self.frame_count += 1

                # Update FPS counter
                self.video_fps_counter.update()

                # Log only every 30 frames to reduce spam
                if self.frame_count % 30 == 0:
                    self.log_message(
                        f"✓ Frame {self.frame_count} decoded: {frame.shape[1]}x{frame.shape[0]}",
                        "SUCCESS"
                    )

                frame_rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)

                orig_height, orig_width = frame_rgb.shape[:2]
                max_width = 640
                max_height = 480

                scale = min(max_width / orig_width, max_height / orig_height)
                new_width = int(orig_width * scale)
                new_height = int(orig_height * scale)

                frame_resized = cv2.resize(frame_rgb, (new_width, new_height), interpolation=cv2.INTER_AREA)
                img = Image.fromarray(frame_resized)
                photo = ImageTk.PhotoImage(img)

                # Schedule display update on main thread
                self.root.after(0, self.update_video_display, photo)

            else:
                self.log_message("✗ cv2.imdecode failed - invalid MJPEG data", "ERROR")

        except Exception as e:
            self.log_message(f"Frame decode error: {str(e)}", "ERROR")
            import traceback
            self.log_message(f"Traceback: {traceback.format_exc()}", "ERROR")

    def update_video_display(self, photo):
        """Update video display canvas with new frame"""
        try:
            # Delete all existing items
            self.video_canvas.delete("all")

            canvas_width = 640
            canvas_height = 480
            img_width = photo.width()
            img_height = photo.height()

            # Center the image
            x = (canvas_width - img_width) // 2
            y = (canvas_height - img_height) // 2

            # Create image on canvas
            self.video_canvas.create_image(x, y, anchor=tk.NW, image=photo)

            # IMPORTANT: Keep a reference to prevent garbage collection
            self.video_canvas.image = photo

            # Force canvas update
            self.video_canvas.update_idletasks()

            # Update status
            self.status_label.config(text="Streaming", foreground="green")

        except Exception as e:
            self.log_message(f"Display update error: {str(e)}", "ERROR")
            import traceback
            self.log_message(f"Traceback: {traceback.format_exc()}", "ERROR")

    def amplify_audio(self, pcm_data, volume):
        """
        Amplify audio data with volume control and soft clipping

        Args:
            pcm_data: PCM audio data as bytes (16-bit little-endian)
            volume: Volume multiplier

        Returns:
            bytes: Amplified PCM audio data
        """
        try:
            if volume == 1.0:
                return pcm_data  # No amplification needed

            # Convert bytes to numpy array of 16-bit integers
            num_samples = len(pcm_data) // 2
            samples = np.frombuffer(pcm_data, dtype=np.int16)

            # Convert to float for processing
            samples_float = samples.astype(np.float32)

            # Apply volume multiplication
            amplified = samples_float * volume

            # Soft clipping to prevent harsh distortion
            # Use tanh for smooth saturation
            max_val = 32767.0
            amplified = np.tanh(amplified / max_val) * max_val

            # Convert back to int16
            amplified_int = amplified.astype(np.int16)

            return amplified_int.tobytes()

        except Exception as e:
            self.log_message(f"Audio amplification error: {str(e)}", "ERROR")
            return pcm_data  # Return original data on error

    def convert_l16_to_pcm(self, data, channels=2):
        """
        Convert L16 (big-endian 16-bit PCM) to little-endian PCM
        Also handle stereo to mono conversion if needed

        L16 format: Big-endian 16-bit signed PCM
        Server sends: 2 channels (stereo), 8000 Hz, 20ms packets
        20ms @ 8000Hz = 160 samples per channel = 320 samples total = 640 bytes
        """
        try:
            # Convert big-endian to little-endian
            num_samples = len(data) // 2
            samples = struct.unpack(f'>{num_samples}h', data)  # Big-endian signed shorts

            if channels == 2:
                # Convert stereo to mono by averaging left and right channels
                mono_samples = []
                for i in range(0, len(samples), 2):
                    if i + 1 < len(samples):
                        # Average left and right channels
                        mono_sample = (samples[i] + samples[i + 1]) // 2
                        mono_samples.append(mono_sample)

                # Pack as little-endian
                pcm_data = struct.pack(f'<{len(mono_samples)}h', *mono_samples)
            else:
                # Already mono, just convert endianness
                pcm_data = struct.pack(f'<{len(samples)}h', *samples)

            return pcm_data

        except Exception as e:
            self.log_message(f"L16 conversion error: {str(e)}", "ERROR")
            return data

    def convert_pcm_to_l16(self, data, channels=1):
        """
        Convert little-endian PCM to L16 (big-endian)
        Also handle mono to stereo conversion if needed

        For sending to server
        """
        try:
            # Convert little-endian to big-endian
            num_samples = len(data) // 2
            samples = struct.unpack(f'<{num_samples}h', data)  # Little-endian signed shorts

            if channels == 2 and num_samples > 0:
                # Convert mono to stereo by duplicating samples
                stereo_samples = []
                for sample in samples:
                    stereo_samples.append(sample)  # Left channel
                    stereo_samples.append(sample)  # Right channel (duplicate)

                # Pack as big-endian
                l16_data = struct.pack(f'>{len(stereo_samples)}h', *stereo_samples)
            else:
                # Already correct channels, just convert endianness
                l16_data = struct.pack(f'>{len(samples)}h', *samples)

            return l16_data

        except Exception as e:
            self.log_message(f"PCM to L16 conversion error: {str(e)}", "ERROR")
            return data

    def decode_pcmu(self, data):
        """Decode G.711 μ-law (PCMU) audio data to PCM"""
        # μ-law decompression table
        BIAS = 0x84
        CLIP = 32635

        pcm_data = bytearray()

        for byte in data:
            # Invert bits
            byte = ~byte & 0xFF

            # Extract sign, exponent, and mantissa
            sign = (byte & 0x80)
            exponent = (byte >> 4) & 0x07
            mantissa = byte & 0x0F

            # Calculate linear value
            linear = ((mantissa << 3) + BIAS) << exponent

            # Apply sign
            if sign != 0:
                linear = -linear

            # Clamp to 16-bit range
            linear = max(-32768, min(32767, linear))

            # Convert to 16-bit PCM (little-endian)
            pcm_data.extend(struct.pack('<h', linear))

        return bytes(pcm_data)

    def encode_pcmu(self, data):
        """Encode PCM to G.711 μ-law (PCMU) for sending"""
        BIAS = 0x84
        CLIP = 32635

        pcmu_data = bytearray()

        # Convert bytes to 16-bit samples
        num_samples = len(data) // 2
        samples = struct.unpack(f'<{num_samples}h', data)

        for sample in samples:
            # Clamp sample
            sample = max(-32768, min(32767, sample))

            # Get sign
            sign = 0x80 if sample < 0 else 0x00
            if sample < 0:
                sample = -sample

            # Add bias
            sample = sample + BIAS
            if sample > CLIP:
                sample = CLIP

            # Find exponent
            exponent = 7
            for i in range(7, -1, -1):
                if sample <= (0x1F << (i + 3)):
                    exponent = i
                    break

            # Get mantissa
            mantissa = (sample >> (exponent + 3)) & 0x0F

            # Compose byte
            byte = sign | (exponent << 4) | mantissa

            # Invert bits
            byte = ~byte & 0xFF

            pcmu_data.append(byte)

        return bytes(pcmu_data)

    def decode_pcma(self, data):
        """Decode G.711 A-law (PCMA) audio data to PCM"""
        pcm_data = bytearray()

        for byte in data:
            # Invert even bits
            byte = byte ^ 0x55

            # Extract sign, exponent, and mantissa
            sign = (byte & 0x80)
            exponent = (byte >> 4) & 0x07
            mantissa = byte & 0x0F

            # Calculate linear value
            if exponent == 0:
                linear = (mantissa << 4) + 8
            else:
                linear = ((mantissa << 4) + 0x108) << (exponent - 1)

            # Apply sign
            if sign == 0:
                linear = -linear

            # Clamp to 16-bit range
            linear = max(-32768, min(32767, linear))

            # Convert to 16-bit PCM (little-endian)
            pcm_data.extend(struct.pack('<h', linear))

        return bytes(pcm_data)

    def encode_pcma(self, data):
        """Encode PCM to G.711 A-law (PCMA) for sending"""
        pcma_data = bytearray()

        # Convert bytes to 16-bit samples
        num_samples = len(data) // 2
        samples = struct.unpack(f'<{num_samples}h', data)

        for sample in samples:
            # Clamp sample
            sample = max(-32768, min(32767, sample))

            # Get sign
            sign = 0x80 if sample >= 0 else 0x00
            if sample < 0:
                sample = -sample

            # Compress
            if sample > 32635:
                sample = 32635

            # Find exponent
            exponent = 7
            for i in range(7, -1, -1):
                if sample <= (0x0F << (i + 4)):
                    exponent = i
                    break

            # Get mantissa
            if exponent == 0:
                mantissa = (sample >> 4) & 0x0F
            else:
                mantissa = (sample >> (exponent + 3)) & 0x0F

            # Compose byte
            byte = sign | (exponent << 4) | mantissa

            # Invert even bits
            byte = byte ^ 0x55

            pcma_data.append(byte)

        return bytes(pcma_data)

    def start_audio_rtp_reception(self):
        """Start audio RTP packet reception thread"""
        try:
            import pyaudio

            # Determine audio format
            if self.audio_codec is None:
                # Try to determine from payload type
                if self.audio_payload_type == 0:
                    self.audio_codec = 'PCMU'
                    self.log_message("Audio codec detected as PCMU (G.711 μ-law) from payload type 0", "INFO")
                elif self.audio_payload_type == 8:
                    self.audio_codec = 'PCMA'
                    self.log_message("Audio codec detected as PCMA (G.711 A-law) from payload type 8", "INFO")
                elif self.audio_payload_type == 11:
                    self.audio_codec = 'L16'
                    self.audio_sample_rate = 44100
                    self.audio_channels = 2
                    self.log_message("Audio codec detected as L16 (44.1kHz stereo) from payload type 11", "INFO")
                elif self.audio_payload_type == 10:
                    self.audio_codec = 'L16'
                    self.audio_sample_rate = 44100
                    self.audio_channels = 1
                    self.log_message("Audio codec detected as L16 (44.1kHz mono) from payload type 10", "INFO")
                else:
                    self.log_message(f"Unknown audio payload type: {self.audio_payload_type}, assuming L16", "WARNING")
                    self.audio_codec = 'L16'

            self.log_message(
                f"Initializing audio receive: Codec={self.audio_codec}, Rate={self.audio_sample_rate}Hz, Channels={self.audio_channels}",
                "INFO"
            )

            self.pyaudio_instance = pyaudio.PyAudio()

            # Always output as 16-bit PCM mono after decoding/conversion
            output_channels = 1  # Always output mono

            self.audio_stream = self.pyaudio_instance.open(
                format=pyaudio.paInt16,
                channels=output_channels,
                rate=self.audio_sample_rate,
                output=True,
                frames_per_buffer=1024
            )

            self.log_message("Audio output stream initialized successfully", "SUCCESS")

            if self.audio_thread is None or not self.audio_thread.is_alive():
                self.audio_thread = threading.Thread(target=self.receive_audio_rtp, daemon=True)
                self.audio_thread.start()

        except ImportError:
            self.log_message("PyAudio not installed - audio playback disabled", "ERROR")
            self.audio_available = False
            self.root.after(0, lambda: self.audio_rx_stats_label.config(text="PyAudio missing"))
        except Exception as e:
            self.log_message(f"Audio initialization error: {str(e)}", "ERROR")
            import traceback
            self.log_message(f"Traceback: {traceback.format_exc()}", "ERROR")
            self.audio_available = False
            self.root.after(0, lambda: self.audio_rx_stats_label.config(text="Error"))

    def receive_audio_rtp(self):
        """Receive and process audio RTP packets via UDP"""
        self.log_message(f"Starting audio RTP reception (trackID=1, codec={self.audio_codec})...", "INFO")
        self.log_message(f"Audio socket listening on port {self.audio_rtp_port}", "INFO")
        self.log_message(f"Expected format: {self.audio_codec}, {self.audio_sample_rate}Hz, {self.audio_channels} channel(s)", "INFO")
        self.log_message(f"Audio RX volume amplification: {self.audio_rx_volume:.1f}x", "INFO")

        packet_count = 0
        decode_errors = 0
        first_packet_received = False

        try:
            while self.is_playing and self.audio_stream:
                try:
                    data, addr = self.audio_rtp_socket.recvfrom(65536)

                    if not first_packet_received:
                        first_packet_received = True
                        self.log_message(f"✓ First audio packet received from {addr}", "SUCCESS")

                    packet_count += 1

                    rtp_packet = RTPPacket()
                    if rtp_packet.decode(data):
                        payload = rtp_packet.get_payload()

                        # Update FPS counter
                        self.audio_rx_fps_counter.update()

                        # Log first few packets with details
                        if packet_count <= 5 or packet_count % 100 == 0:
                            self.log_message(
                                f"Audio RTP #{packet_count}: seq={rtp_packet.seqnum}, ts={rtp_packet.timestamp}, "
                                f"PT={rtp_packet.pt}, payload={len(payload)} bytes",
                                "RTP"
                            )

                        if self.audio_stream and len(payload) > 0:
                            try:
                                # Decode audio based on codec
                                if self.audio_codec == 'PCMU':
                                    pcm_data = self.decode_pcmu(payload)
                                    if packet_count == 1:
                                        self.log_message(f"Decoded PCMU: {len(payload)} bytes → {len(pcm_data)} bytes PCM", "INFO")

                                elif self.audio_codec == 'PCMA':
                                    pcm_data = self.decode_pcma(payload)
                                    if packet_count == 1:
                                        self.log_message(f"Decoded PCMA: {len(payload)} bytes → {len(pcm_data)} bytes PCM", "INFO")

                                elif self.audio_codec == 'L16':
                                    # L16 is big-endian PCM, need to convert to little-endian
                                    # Also convert stereo to mono if needed
                                    pcm_data = self.convert_l16_to_pcm(payload, self.audio_channels)
                                    if packet_count == 1:
                                        self.log_message(
                                            f"Converted L16: {len(payload)} bytes ({self.audio_channels}ch) → {len(pcm_data)} bytes PCM (1ch)",
                                            "INFO"
                                        )

                                elif self.audio_codec in ['PCM']:
                                    # Already PCM, no decoding needed
                                    pcm_data = payload
                                    if packet_count == 1:
                                        self.log_message(f"Using raw PCM: {len(payload)} bytes", "INFO")
                                else:
                                    # Unknown codec, try playing raw
                                    if packet_count == 1:
                                        self.log_message(f"⚠ Unknown codec '{self.audio_codec}', playing raw data", "WARNING")
                                    pcm_data = payload

                                # Apply RX volume amplification
                                pcm_data = self.amplify_audio(pcm_data, self.audio_rx_volume)

                                # Play decoded audio
                                self.audio_stream.write(pcm_data)

                            except Exception as e:
                                decode_errors += 1
                                if decode_errors <= 5 or decode_errors % 50 == 1:  # Log first 5 errors and every 50th
                                    self.log_message(f"Audio decode/playback error #{decode_errors}: {str(e)}", "ERROR")

                except socket.timeout:
                    if packet_count == 0 and self.is_playing:
                        self.log_message("⚠ No audio packets received yet - checking firewall/server...", "WARNING")
                    continue
                except Exception as e:
                    if self.is_playing:
                        self.log_message(f"Audio RTP error: {str(e)}", "ERROR")
                    break

            self.log_message(
                f"Audio RTP reception stopped. Packets: {packet_count}, Decode errors: {decode_errors}",
                "INFO"
            )

        except Exception as e:
            self.log_message(f"Audio RTP thread error: {str(e)}", "ERROR")
            import traceback
            self.log_message(f"Traceback: {traceback.format_exc()}", "ERROR")

    def start_backchannel(self):
        """Start audio backchannel transmission - called AFTER PLAY response"""
        try:
            # CRITICAL: Verify we have server destination before starting
            if self.server_backchannel_port is None or self.server_backchannel_ip is None:
                self.log_message("✗ Cannot start backchannel - no server destination port/IP from SETUP response", "ERROR")
                self.backchannel_available = False
                self.root.after(0, lambda: self.audio_tx_stats_label.config(
                    text="No server destination",
                    foreground="red"
                ))
                return

            import pyaudio

            if self.pyaudio_instance is None:
                self.pyaudio_instance = pyaudio.PyAudio()

            self.log_message(
                f"Initializing audio backchannel: Codec={self.backchannel_codec}, "
                f"Rate={self.backchannel_sample_rate}Hz, Channels={self.backchannel_channels}",
                "INFO"
            )
            self.log_message(
                f"Destination: {self.server_backchannel_ip}:{self.server_backchannel_port}",
                "INFO"
            )
            self.log_message(f"Audio TX volume amplification: {self.audio_tx_volume:.1f}x", "INFO")

            # Initialize RTP sending state
            self.send_seqnum = 0
            self.send_timestamp = 0

            # Calculate frames per buffer for 20ms chunks
            frames_per_buffer = int(self.backchannel_sample_rate * 0.020)  # 20ms

            # Try to open microphone input stream
            try:
                # Check if there are any input devices
                device_count = self.pyaudio_instance.get_device_count()
                has_input_device = False

                for i in range(device_count):
                    device_info = self.pyaudio_instance.get_device_info_by_index(i)
                    if device_info['maxInputChannels'] > 0:
                        has_input_device = True
                        self.log_message(f"Found input device: {device_info['name']}", "INFO")
                        break

                if not has_input_device:
                    raise Exception("No microphone input device found")

                self.backchannel_input_stream = self.pyaudio_instance.open(
                    format=pyaudio.paInt16,
                    channels=1,  # Capture mono
                    rate=self.backchannel_sample_rate,
                    input=True,
                    frames_per_buffer=frames_per_buffer
                )

                self.use_generated_audio = False
                self.log_message(f"✓ Microphone opened successfully (buffer={frames_per_buffer} frames, 20ms)", "SUCCESS")

            except Exception as mic_error:
                # No microphone found - use generated audio
                self.log_message(f"⚠ Microphone not available: {str(mic_error)}", "WARNING")
                self.log_message("🎵 Switching to generated audio mode (simple melody)", "SUCCESS")

                self.use_generated_audio = True
                self.audio_generator = AudioGenerator(sample_rate=self.backchannel_sample_rate)

                # Generate the melody buffer
                self.generated_audio_buffer = self.audio_generator.generate_simple_melody()
                self.generated_audio_position = 0

                self.log_message(f"Generated audio buffer: {len(self.generated_audio_buffer)} bytes", "INFO")

            # Start sending thread - will begin transmitting immediately
            if self.backchannel_thread is None or not self.backchannel_thread.is_alive():
                self.backchannel_thread = threading.Thread(target=self.send_audio_rtp, daemon=True)
                self.backchannel_thread.start()
                self.log_message("✓ Backchannel RTP transmission thread started", "SUCCESS")

        except ImportError:
            self.log_message("PyAudio not installed - backchannel disabled", "ERROR")
            self.backchannel_available = False
            self.root.after(0, lambda: self.audio_tx_stats_label.config(text="PyAudio missing"))
        except Exception as e:
            self.log_message(f"Backchannel initialization error: {str(e)}", "ERROR")
            import traceback
            self.log_message(f"Traceback: {traceback.format_exc()}", "ERROR")
            self.backchannel_available = False
            self.root.after(0, lambda: self.audio_tx_stats_label.config(text="Error"))

    def send_audio_rtp(self):
        """Send audio RTP packets to server (backchannel) - runs continuously while playing"""

        # Double-check we have a valid destination
        if self.server_backchannel_port is None or self.server_backchannel_ip is None:
            self.log_message("✗ Cannot start backchannel transmission - no server destination", "ERROR")
            return

        self.log_message(f"Starting audio RTP transmission (trackID=2, codec={self.backchannel_codec})...", "INFO")
        self.log_message(f"Sending to {self.server_backchannel_ip}:{self.server_backchannel_port}", "INFO")
        self.log_message(f"Audio TX volume amplification: {self.audio_tx_volume:.1f}x", "INFO")

        if self.use_generated_audio:
            self.log_message("Using generated audio (simple melody)", "INFO")
        else:
            self.log_message("Using microphone input", "INFO")

        packet_count = 0
        encode_errors = 0

        try:
            while self.is_playing:
                try:
                    # Get audio data - either from microphone or generated
                    if self.use_generated_audio:
                        # Use generated audio
                        frames_to_read = int(self.backchannel_sample_rate * 0.020)  # 20ms
                        bytes_to_read = frames_to_read * 2  # 16-bit samples

                        # Check if we need to loop the buffer
                        if self.generated_audio_position + bytes_to_read > len(self.generated_audio_buffer):
                            self.generated_audio_position = 0  # Loop back to start
                            if packet_count > 0:
                                self.log_message("🔄 Looping generated audio melody", "INFO")

                        # Extract chunk from buffer
                        audio_data = self.generated_audio_buffer[
                            self.generated_audio_position:self.generated_audio_position + bytes_to_read
                        ]
                        self.generated_audio_position += bytes_to_read

                        # Sleep to maintain timing (20ms per packet)
                        time.sleep(0.020)

                    else:
                        # Read from microphone
                        if self.backchannel_input_stream is None:
                            break

                        frames_to_read = int(self.backchannel_sample_rate * 0.020)  # 20ms
                        audio_data = self.backchannel_input_stream.read(frames_to_read, exception_on_overflow=False)

                    if len(audio_data) > 0:
                        try:
                            # Apply TX volume amplification BEFORE encoding
                            audio_data = self.amplify_audio(audio_data, self.audio_tx_volume)

                            # Encode audio based on codec from SDP
                            if self.backchannel_codec == 'PCMU':
                                encoded_data = self.encode_pcmu(audio_data)
                                if packet_count == 0:
                                    self.log_message(f"Encoding PCMU: {len(audio_data)} bytes PCM → {len(encoded_data)} bytes", "INFO")

                            elif self.backchannel_codec == 'PCMA':
                                encoded_data = self.encode_pcma(audio_data)
                                if packet_count == 0:
                                    self.log_message(f"Encoding PCMA: {len(audio_data)} bytes PCM → {len(encoded_data)} bytes", "INFO")

                            elif self.backchannel_codec == 'L16':
                                # Convert little-endian PCM to big-endian L16
                                # Also convert mono to stereo if needed
                                encoded_data = self.convert_pcm_to_l16(audio_data, self.backchannel_channels)
                                if packet_count == 0:
                                    self.log_message(
                                        f"Converting to L16: {len(audio_data)} bytes PCM (1ch) → {len(encoded_data)} bytes ({self.backchannel_channels}ch)",
                                        "INFO"
                                    )

                            elif self.backchannel_codec in ['PCM']:
                                # Already PCM, no encoding needed
                                encoded_data = audio_data
                                if packet_count == 0:
                                    self.log_message(f"Using raw PCM: {len(audio_data)} bytes", "INFO")
                            else:
                                # Unknown codec, send raw
                                if packet_count == 0:
                                    self.log_message(f"⚠ Unknown codec '{self.backchannel_codec}', sending raw data", "WARNING")
                                encoded_data = audio_data

                            # Create RTP packet
                            rtp_packet = RTPPacket()
                            rtp_data = rtp_packet.encode(
                                payload=encoded_data,
                                seqnum=self.send_seqnum,
                                timestamp=self.send_timestamp,
                                marker=0,
                                pt=self.backchannel_payload_type,
                                ssrc=self.send_ssrc
                            )

                            # Send RTP packet to server
                            self.backchannel_rtp_socket.sendto(
                                rtp_data,
                                (self.server_backchannel_ip, self.server_backchannel_port)
                            )

                            packet_count += 1

                            # Update FPS counter
                            self.audio_tx_fps_counter.update()

                            # Log every 50 packets
                            if packet_count % 50 == 0:
                                source = "GEN" if self.use_generated_audio else "MIC"
                                self.log_message(
                                    f"Sent audio RTP #{packet_count} ({source}): seq={self.send_seqnum}, ts={self.send_timestamp}, "
                                    f"payload={len(encoded_data)} bytes, vol={self.audio_tx_volume:.1f}x",
                                    "RTP"
                                )

                            # Update RTP state
                            self.send_seqnum = (self.send_seqnum + 1) & 0xFFFF
                            self.send_timestamp += frames_to_read  # Increment by samples sent

                        except Exception as e:
                            encode_errors += 1
                            if encode_errors <= 5 or encode_errors % 50 == 1:
                                self.log_message(f"Audio encode/send error #{encode_errors}: {str(e)}", "ERROR")

                except Exception as e:
                    if self.is_playing:
                        self.log_message(f"Audio read error: {str(e)}", "ERROR")
                    break

            source = "generated audio" if self.use_generated_audio else "microphone"
            self.log_message(
                f"Audio RTP transmission stopped ({source}). Packets sent: {packet_count}, Encode errors: {encode_errors}",
                "INFO"
            )

        except Exception as e:
            self.log_message(f"Audio RTP send thread error: {str(e)}", "ERROR")
            import traceback
            self.log_message(f"Traceback: {traceback.format_exc()}", "ERROR")

    def enable_playback_controls(self):
        """Enable playback control buttons"""
        self.play_btn.config(state=tk.DISABLED)
        self.stop_btn.config(state=tk.NORMAL)
        self.status_label.config(text="Streaming", foreground="green")

    def reset_buttons(self):
        """Reset button states"""
        self.play_btn.config(state=tk.NORMAL)
        self.stop_btn.config(state=tk.DISABLED)
        self.audio_checkbox.config(state=tk.NORMAL)
        self.backchannel_checkbox.config(state=tk.NORMAL)

    def cleanup_on_exit(self):
        """Cleanup firewall rules on application exit"""
        try:
            # Stop streaming if active
            if self.is_playing:
                self.is_playing = False
                time.sleep(0.2)

            # Remove firewall rules
            if self.firewall_manager and self.firewall_enabled_var.get():
                removed_count = self.firewall_manager.remove_firewall_rules()
                if removed_count > 0:
                    print(f"Cleaned up {removed_count} firewall rule(s)")
        except:
            pass

    def on_closing(self):
        """Handle window close event"""
        try:
            # Stop streaming first
            if self.is_playing:
                self.stop_stream()
                time.sleep(0.3)

            # Cleanup firewall rules
            self.cleanup_on_exit()

            # Destroy the window
            self.root.quit()  # Exit mainloop
            self.root.destroy()  # Destroy window

        except Exception as e:
            print(f"Error during cleanup: {e}")
            self.root.destroy()


def main():
    root = tk.Tk()
    app = RTSPClient(root)

    try:
        root.mainloop()
    except KeyboardInterrupt:
        print("\nInterrupted by user")
    finally:
        # Ensure cleanup happens
        try:
            app.cleanup_on_exit()
        except:
            pass

        # Force exit if needed
        import sys
        sys.exit(0)


if __name__ == "__main__":
    main()
