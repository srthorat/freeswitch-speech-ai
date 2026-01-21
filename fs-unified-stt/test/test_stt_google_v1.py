#!/usr/bin/env python3
"""
Reusable Test Script for Speech Services - Streams WAV files via WebSocket.

Usage:
    ./test_stt.py                           # Test with defaults
    ./test_stt.py --provider google-v2      # Specific provider
    ./test_stt.py --url ws://host:port/path # Custom endpoint
    ./test_stt.py --wav /path/to/file.wav   # Custom WAV file
"""

import asyncio
import json
import wave
import argparse
import uuid
import sys
from datetime import datetime
from pathlib import Path

try:
    import websockets
except ImportError:
    print("ERROR: websockets not installed. Run: pip install websockets")
    sys.exit(1)

# Default Configuration
DEFAULT_SIDECAR_URL = "ws://127.0.0.1:9088/audio-fork"
DEFAULT_WAV_FILE = "response_call_60s.wav"
CHUNK_DURATION_MS = 20

class STTTester:
    def __init__(self, url: str, wav_file: str, provider: str, lang: str = "en-US", model: str = "telephony"):
        self.url = url
        self.wav_file = wav_file
        self.provider = provider
        self.lang = lang
        self.model = model
        self.results = []
        
    def read_wav(self):
        """Read WAV file and return info + raw audio data."""
        with wave.open(self.wav_file, 'rb') as wf:
            return {
                "sample_rate": wf.getframerate(),
                "channels": wf.getnchannels(),
                "sample_width": wf.getsampwidth(),
                "n_frames": wf.getnframes(),
                "audio_data": wf.readframes(wf.getnframes())
            }
    
    async def stream(self, max_duration_sec: int = None):
        """Stream WAV to sidecar and collect transcription results."""
        wav = self.read_wav()
        
        print(f"\n{'='*60}")
        print(f"STT Service Test")
        print(f"{'='*60}")
        print(f"URL:         {self.url}")
        print(f"Provider:    {self.provider}")
        print(f"WAV:         {Path(self.wav_file).name}")
        print(f"Sample Rate: {wav['sample_rate']} Hz")
        print(f"Channels:    {wav['channels']}")
        print(f"Duration:    {wav['n_frames']/wav['sample_rate']:.2f}s")
        print(f"{'='*60}\n")
        
        # Calculate chunk size
        bytes_per_sample = wav['sample_width'] * wav['channels']
        samples_per_chunk = int(wav['sample_rate'] * CHUNK_DURATION_MS / 1000)
        chunk_size = samples_per_chunk * bytes_per_sample
        
        session_id = str(uuid.uuid4())[:8]
        
        try:
            async with websockets.connect(self.url, max_size=10*1024*1024) as ws:
                # 1. Send start metadata
                start_meta = {
                    "uuid": f"test-{session_id}",
                    "sip_call_id": f"sip-{session_id}",
                    "sample_rate": wav['sample_rate'],
                    "channels": wav['channels'],
                    "lang": self.lang,
                    "model": self.model,
                    "provider": self.provider,
                    "location": "global",
                    "stereo_swap": False,
                    "caller": {"name": "Test Caller", "phone": "+1234567890"},
                    "callee": {"name": "Test Callee", "phone": "+0987654321"}
                }
                
                print(f"[{self._ts()}] → Sending START metadata")
                await ws.send(json.dumps(start_meta))
                
                # 2. Start receiver task
                receiver = asyncio.create_task(self._receive_results(ws))
                
                # 3. Stream audio
                print(f"[{self._ts()}] → Streaming audio...")
                audio_data = wav['audio_data']
                max_bytes = len(audio_data)
                if max_duration_sec:
                    max_bytes = min(max_bytes, int(max_duration_sec * wav['sample_rate'] * bytes_per_sample))
                
                offset = 0
                chunks_sent = 0
                start_time = asyncio.get_event_loop().time()
                
                while offset < max_bytes:
                    chunk = audio_data[offset:offset + chunk_size]
                    if chunk:
                        await ws.send(chunk)
                        chunks_sent += 1
                        offset += chunk_size
                        await asyncio.sleep(CHUNK_DURATION_MS / 1000)
                        
                        if chunks_sent % 250 == 0:
                            pct = (offset / max_bytes) * 100
                            print(f"[{self._ts()}]   Progress: {pct:.1f}%")
                
                elapsed = asyncio.get_event_loop().time() - start_time
                print(f"[{self._ts()}] ✓ Streaming complete ({chunks_sent} chunks, {elapsed:.1f}s)")
                
                # 4. Send end metadata
                await ws.send(json.dumps({"action": "stop"}))
                print(f"[{self._ts()}] → Sent STOP metadata")
                
                # Wait for final results
                await asyncio.sleep(3)
                receiver.cancel()
                
        except (ConnectionRefusedError, OSError):
            print(f"[ERROR] Connection refused to {self.url}")
            print("        Is the sidecar running?")
            return False
        except Exception as e:
            print(f"[ERROR] {type(e).__name__}: {e}")
            return False
        
        self._print_summary()
        return True
    
    async def _receive_results(self, ws):
        """Receive and print transcription results."""
        try:
            async for message in ws:
                try:
                    data = json.loads(message)
                    text = data.get("text", "")
                    is_final = data.get("is_final", False)
                    channel = data.get("channel", 0)
                    
                    marker = "✓" if is_final else "…"
                    print(f"[CH{channel}] {marker} {text}")
                    
                    if is_final:
                        self.results.append({"channel": channel, "text": text})
                        
                except json.JSONDecodeError:
                    pass
        except asyncio.CancelledError:
            pass
        except websockets.exceptions.ConnectionClosed:
            pass
    
    def _print_summary(self):
        print(f"\n{'='*60}")
        print(f"RESULTS SUMMARY ({len(self.results)} final transcriptions)")
        print(f"{'='*60}")
        for r in self.results:
            print(f"[CH{r['channel']}] {r['text']}")
        print(f"{'='*60}\n")
    
    @staticmethod
    def _ts():
        return datetime.now().strftime('%H:%M:%S')


def main():
    parser = argparse.ArgumentParser(
        description="Test Speech-to-Text services via WebSocket",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  Test Google V2:  ./test_stt.py --provider google-v2
  Test Google V1:  ./test_stt.py --provider google-v1
  Test AWS:        ./test_stt.py --provider aws
  Quick test:      ./test_stt.py --max-duration 30
        """
    )
    parser.add_argument("--provider", default="google-v1",
                        choices=["google-v2", "google-v1", "aws"],
                        help="STT provider to use (default: google-v1)")
    parser.add_argument("--url", default=DEFAULT_SIDECAR_URL,
                        help=f"WebSocket URL (default: {DEFAULT_SIDECAR_URL})")
    parser.add_argument("--wav", default=DEFAULT_WAV_FILE,
                        help="WAV file to stream")
    parser.add_argument("--lang", default="en-US",
                        help="Language code (default: en-US)")
    parser.add_argument("--model", default="telephony",
                        help="Model to use (default: telephony)")
    parser.add_argument("--max-duration", type=int, default=None,
                        help="Max seconds to stream (for quick tests)")
    
    args = parser.parse_args()
    
    if not Path(args.wav).exists():
        print(f"ERROR: WAV file not found: {args.wav}")
        sys.exit(1)
    
    tester = STTTester(args.url, args.wav, args.provider, args.lang)
    # Monkey patch or modify STTTester to accept model if I can, or just modify start_meta
    # Since I cannot easily modify STTTester constructor without editing the class definition above,
    # I will edit the class definition in the next tool call or do a larger replace here.
    # To keep it simple, I'll pass model via a global or modify the class. 
    # Actually, I'll modify the class __init__ and stream method in this file.
    
    # Wait, simple replace is cleaner. I will edit STTTester.__init__ and stream separately. 
    # Here I just added the arg.
    
    # I need to update the STTTester instantiation too.
    tester = STTTester(args.url, args.wav, args.provider, args.lang, args.model)
    success = asyncio.run(tester.stream(args.max_duration))
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
