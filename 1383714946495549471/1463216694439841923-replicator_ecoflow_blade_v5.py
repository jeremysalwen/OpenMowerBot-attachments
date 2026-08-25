#!/usr/bin/env python3
"""
EcoFlow BLADE Replicator v5 - Silent Operation with Movement Controls

Based on v4 with:
- Silent heartbeat logging (no console spam from heartbeats)
- Movement controls (WASD keys)
- All other functionality identical to v4
"""

import asyncio
from bleak import BleakClient
import time
import json
from datetime import datetime
from typing import Optional, Dict, List, Tuple
import select

class EcoFlowBladeReplicatorV5:
    def __init__(self):
        # Mac UUID connection (identical to v4)
        self.DEVICE_UUID = "MAC ADDRESS HERE"
        self.WRITE_CHAR_UUID = "ABF1"
        self.NOTIFY_CHAR_UUID = "ABF2"
        
        # Complete handshake commands from v4 analysis
        self.HANDSHAKE_COMMANDS = {
            # Step 1: Phone → INIT_HANDSHAKE
            'INIT_HANDSHAKE': bytes.fromhex("aa020000b50d00000000000021353589c584"),
            
            # Step 3: Phone → DEVICE_INFO
            'DEVICE_INFO': bytes.fromhex("aa0220001b0d00000000000021353586464643434345373945303736333836373230393835343631383342334437463391f1"),
            
            # Step 6: Phone → UNKNOWN_03
            'UNKNOWN_03_1': bytes.fromhex("aa020100a00d00000000000021454503011004f0"),
            
            # Step 7: Phone → UNKNOWN_03
            'UNKNOWN_03_2': bytes.fromhex("aa020100a00d00000000000021454503011004f0"),
            
            # Step 10: Phone → CONFIG
            'CONFIG': bytes.fromhex("aa0208001d0d000000000000214545282800000000000000a6f2"),
            
            # Step 11: Phone → URL_CONFIG
            'URL_CONFIG': bytes.fromhex("aa021e00340d00000000000021454520fa190068747470733a2f2f6170692d612e65636f666c6f772e636f6d0500e670"),
            
            # Step 13: Phone → AUTH/STATUS
            'AUTH_STATUS': bytes.fromhex("aa020100a00d000000000000214545260110ed"),
            
            # Step 14: Phone → SETUP #1
            'SETUP_1': bytes.fromhex("aa0208001d0d0000000000002145453333000000010a0101b913"),
            
            # Step 16: Phone → SETUP #2
            'SETUP_2': bytes.fromhex("aa0208001d0d000000000000214545333300000001065553c62d"),
            
            # Step 19: Phone → FINAL_INIT
            'FINAL_INIT': bytes.fromhex("aa020100a00d000000000000214545110106dd"),
        }
        
        # Movement commands (updated with proven working commands from original replicator)
        self.MOVEMENT_COMMANDS = {
            'w': bytes.fromhex("aa020900080d000000000000214545010c0000000c5300100088e3"),  # Forward + Right Turn (X=83, Y=16) - WORKING
            'a': bytes.fromhex("aa020900080d000000000000214545010c0000000c0000b0ffa7e7"),  # Turn left - WORKING
            's': bytes.fromhex("aa020900080d000000000000214545010c0000000c9cff00008c07"),  # Backward - WORKING  
            'd': bytes.fromhex("aa020900080d000000000000214545010c0000000c5600feff83cf"),  # Forward + Left Turn (X=86, Y=-2) - WORKING
            ' ': bytes.fromhex("aa020900080d000000000000214545010c0000000c000000009267"),  # Stop (spacebar)
        }
        
        # Session state and response tracking (identical to v4)
        self.client = None
        self.handshake_step = 0
        self.handshake_complete = False
        self.session_active = False
        
        # Response tracking and handshake state
        self.received_responses = []
        self.pending_responses = []  # Track expected responses
        self._response_event = asyncio.Event()
        
        # BLE write synchronization (v5 fix for concurrent writes)
        self.write_lock = asyncio.Lock()
        
        # Session logging
        self.log_file = f"blade_v5_session_{datetime.now().strftime('%Y%m%d_%H%M%S')}.jsonl"
        self.message_count = 0
        
        # Response timeout settings
        self.RESPONSE_TIMEOUT = 5.0  # 5 seconds max wait for response
        self.STEP_DELAY = 0.2        # 200ms between steps
        
        # Heartbeat mechanism (identical to v4)
        self.AUTH_START_RESPONSE = bytes.fromhex("aa020100a00d000000000000214545260110ed")
        self.heartbeat_count = 0
        self.last_heartbeat_time = 0
    
    def log_message(self, direction: str, data: bytes, step: int, description: str = ""):
        """Log all handshake messages with step tracking (identical to v4)"""
        self.message_count += 1
        
        log_entry = {
            "id": self.message_count,
            "timestamp": datetime.now().isoformat(),
            "direction": direction,
            "handshake_step": step,
            "hex_data": data.hex().upper(),
            "length": len(data),
            "description": description,
            "session_state": {
                "handshake_complete": self.handshake_complete,
                "session_active": self.session_active,
                "pending_responses": len(self.pending_responses)
            }
        }
        
        # Parse protocol if AA02 packet
        if len(data) >= 14 and data[0:2] == bytes([0xAA, 0x02]):
            log_entry["protocol"] = {
                "header": "AA02",
                "cmd_type": f"0x{data[2]:02X}",
                "proto_bytes": f"{data[12]:02X},{data[13]:02X}" if len(data) >= 14 else "unknown"
            }
        
        try:
            with open(self.log_file, 'a', encoding='utf-8') as f:
                f.write(json.dumps(log_entry) + '\n')
        except Exception as e:
            print(f"⚠️  Logging error: {e}")
    
    def is_robot_heartbeat(self, data: bytes) -> bool:
        """Detect robot heartbeat requests (identical to v4)"""
        return (
            len(data) == 105 and                   # Actual heartbeat length
            data[0:2] == bytes([0xAA, 0x02]) and   # AA02 header
            data[2] == 0x57 and                    # Length field = 87 bytes
            data[12] == 0x45 and data[13] == 0x21  # Protocol bytes 45,21
        )
    
    async def send_heartbeat_response(self):
        """Send AUTH_START response to robot heartbeat (SILENT VERSION)"""
        try:
            # Use write_lock to prevent conflicts with movement commands
            async with self.write_lock:
                # Small delay to avoid flooding
                await asyncio.sleep(0.05)
                
                await self.client.write_gatt_char(self.WRITE_CHAR_UUID, self.AUTH_START_RESPONSE)
                # NOTE: v5 change - NO console output for heartbeat responses (silent operation)
                
                # Log heartbeat response (still logged to JSONL)
                self.log_message("phone_to_robot", self.AUTH_START_RESPONSE, 0, 
                               f"Heartbeat response #{self.heartbeat_count}")
            
        except Exception as e:
            print(f"⚠️  Heartbeat response failed: {self.heartbeat_count}")
    
    def notification_handler(self, sender, data):
        """Handle robot responses - MODIFIED for silent heartbeat operation"""
        timestamp = time.strftime("%H:%M:%S")
        hex_data = data.hex().lower()
        
        # Check for robot heartbeat request (discovered pattern)
        if self.is_robot_heartbeat(data):
            self.heartbeat_count += 1
            self.last_heartbeat_time = time.time()
            
            # Extract counter from heartbeat for tracking
            counter_hex = hex_data[12:14] if len(hex_data) > 14 else "??"
            # NOTE: v5 change - NO console output for heartbeats (silent operation)
            # But show heartbeats during handshake for debugging
            if not self.handshake_complete:
                print(f"💓 Robot heartbeat #{self.heartbeat_count} during handshake (step {self.handshake_step})")
            
            # Schedule heartbeat response (v5 fix: better task handling)
            loop = asyncio.get_event_loop()
            if loop.is_running():
                loop.create_task(self.send_heartbeat_response())
            
            # Log heartbeat (still logged to JSONL)
            self.log_message("robot_to_phone", data, 0, 
                           f"Robot heartbeat #{self.heartbeat_count} (counter: 0x{counter_hex})")
            return
        
        # For non-heartbeat messages, show full output (identical to v4)
        print(f"📥 [{timestamp}] Robot→Phone ({len(data)} bytes):")
        print(f"   Full Hex: {hex_data}")
        
        # Store response for handshake processing
        self.received_responses.append({
            'timestamp': time.time(),
            'data': data,
            'hex': hex_data,
            'processed': False
        })
        
        # Notify waiting functions of new response
        self._response_event.set()
        
        # Show protocol analysis for debugging
        if len(data) >= 14 and data[0:2] == bytes([0xAA, 0x02]):
            cmd_type = f"0x{data[2]:02X}" if len(data) > 2 else "??"
            proto_bytes = f"{data[12]:02X},{data[13]:02X}" if len(data) >= 14 else "??,??"
            payload_len = len(data) - 14 if len(data) > 14 else 0
            print(f"   Protocol: AA02 | Type: {cmd_type} | Proto: {proto_bytes} | Payload: {payload_len} bytes")
    
    async def wait_for_robot_response(self, expected_proto: Tuple[int, int], description: str, timeout: float = 5.0) -> bool:
        """Wait for specific robot response (identical to v4)"""
        start_time = time.time()
        
        while time.time() - start_time < timeout:
            # Check existing responses
            for response in self.received_responses:
                if (not response['processed'] and 
                    len(response['data']) >= 14 and
                    response['data'][12] == expected_proto[0] and 
                    response['data'][13] == expected_proto[1]):
                    
                    # Mark as processed
                    response['processed'] = True
                    
                    # Log the matched response
                    self.log_message("robot_to_phone", response['data'], self.handshake_step, 
                                   f"Robot response: {description}")
                    
                    print(f"✅ Got expected response: {description}")
                    return True
            
            # Wait for new data
            try:
                await asyncio.wait_for(self._response_event.wait(), timeout=0.5)
                self._response_event.clear()
            except asyncio.TimeoutError:
                continue
        
        print(f"⚠️  Timeout waiting for {description} (after {timeout}s)")
        return False
    
    async def perform_complete_handshake(self) -> bool:
        """Complete 19-step handshake sequence (identical to v4)"""
        print("🤝 Starting complete 19-step handshake...")
        
        try:
            # Step 1: Phone → INIT_HANDSHAKE
            print("🔄 Step 1: Phone → INIT_HANDSHAKE")
            self.handshake_step = 1
            await self.client.write_gatt_char(self.WRITE_CHAR_UUID, self.HANDSHAKE_COMMANDS['INIT_HANDSHAKE'])
            self.log_message("phone_to_robot", self.HANDSHAKE_COMMANDS['INIT_HANDSHAKE'], 1, "Phone → INIT_HANDSHAKE")
            
            # Wait for robot init response (35,21)
            if not await self.wait_for_robot_response((0x35, 0x21), "Robot INIT_HANDSHAKE"):
                print("❌ Step 1 failed: No robot init response")
                return False
            
            await asyncio.sleep(self.STEP_DELAY)
            
            # Step 3: Phone → DEVICE_INFO
            print("🔄 Step 3: Phone → DEVICE_INFO")
            self.handshake_step = 3
            await self.client.write_gatt_char(self.WRITE_CHAR_UUID, self.HANDSHAKE_COMMANDS['DEVICE_INFO'])
            self.log_message("phone_to_robot", self.HANDSHAKE_COMMANDS['DEVICE_INFO'], 3, "Phone → DEVICE_INFO")
            
            # Wait for both AUTH/STATUS and DEVICE_INFO responses (flexible order)
            self.handshake_step = 4
            print("⏳ Waiting for AUTH/STATUS and DEVICE_INFO responses...")
            
            responses_needed = {'AUTH_STATUS': False, 'DEVICE_INFO': False}
            timeout_start = time.time()
            
            while time.time() - timeout_start < 10.0 and not all(responses_needed.values()):
                # Check for AUTH/STATUS response (45,21)
                if not responses_needed['AUTH_STATUS']:
                    for response in self.received_responses:
                        if (not response['processed'] and 
                            len(response['data']) >= 14 and
                            response['data'][12] == 0x45 and response['data'][13] == 0x21):
                            response['processed'] = True
                            responses_needed['AUTH_STATUS'] = True
                            self.log_message("robot_to_phone", response['data'], 4, "Robot AUTH/STATUS")
                            print("✅ Got AUTH/STATUS response")
                            break
                
                # Check for DEVICE_INFO response (35,21 with type 0x01)
                if not responses_needed['DEVICE_INFO']:
                    for response in self.received_responses:
                        if (not response['processed'] and 
                            len(response['data']) >= 14 and
                            response['data'][12] == 0x35 and response['data'][13] == 0x21 and
                            response['data'][2] == 0x01):
                            response['processed'] = True
                            responses_needed['DEVICE_INFO'] = True
                            self.log_message("robot_to_phone", response['data'], 4, "Robot DEVICE_INFO")
                            print("✅ Got DEVICE_INFO response")
                            break
                
                # Wait for responses
                try:
                    await asyncio.wait_for(self._response_event.wait(), timeout=0.5)
                    self._response_event.clear()
                except:
                    await asyncio.sleep(0.5)
            
            if not all(responses_needed.values()):
                # Show what's missing but continue anyway (like v4)
                missing = [k for k, v in responses_needed.items() if not v]
                for missing_response in missing:
                    print(f"⚠️  Missing {missing_response} response - but continuing...")
            
            # Continue with remaining handshake steps
            await asyncio.sleep(0.5)
            
            # Steps 6-7: Phone → UNKNOWN_03 commands
            for i, cmd_name in enumerate(['UNKNOWN_03_1', 'UNKNOWN_03_2'], 6):
                print(f"🔄 Step {i}: Phone → {cmd_name}")
                self.handshake_step = i
                await self.client.write_gatt_char(self.WRITE_CHAR_UUID, self.HANDSHAKE_COMMANDS[cmd_name])
                self.log_message("phone_to_robot", self.HANDSHAKE_COMMANDS[cmd_name], i, f"Phone → {cmd_name}")
                await asyncio.sleep(self.STEP_DELAY)
            
            # Step 10: Phone → CONFIG
            print("🔄 Step 10: Phone → CONFIG")
            self.handshake_step = 10
            await self.client.write_gatt_char(self.WRITE_CHAR_UUID, self.HANDSHAKE_COMMANDS['CONFIG'])
            self.log_message("phone_to_robot", self.HANDSHAKE_COMMANDS['CONFIG'], 10, "Phone → CONFIG")
            await self.wait_for_robot_response((0x45, 0x21), "Robot CONFIG")
            
            # Step 11: Phone → URL_CONFIG
            print("🔄 Step 11: Phone → URL_CONFIG")
            self.handshake_step = 11
            await self.client.write_gatt_char(self.WRITE_CHAR_UUID, self.HANDSHAKE_COMMANDS['URL_CONFIG'])
            self.log_message("phone_to_robot", self.HANDSHAKE_COMMANDS['URL_CONFIG'], 11, "Phone → URL_CONFIG")
            await self.wait_for_robot_response((0x45, 0x21), "Robot URL_CONFIG")
            
            # Step 14: Phone → SETUP #1
            print("🔄 Step 14: Phone → SETUP #1")
            self.handshake_step = 14
            await self.client.write_gatt_char(self.WRITE_CHAR_UUID, self.HANDSHAKE_COMMANDS['SETUP_1'])
            self.log_message("phone_to_robot", self.HANDSHAKE_COMMANDS['SETUP_1'], 14, "Phone → SETUP #1")
            await asyncio.sleep(0.5)  # Don't wait for response
            
            # Step 16: Phone → SETUP #2
            print("🔄 Step 16: Phone → SETUP #2")
            self.handshake_step = 16
            await self.client.write_gatt_char(self.WRITE_CHAR_UUID, self.HANDSHAKE_COMMANDS['SETUP_2'])
            self.log_message("phone_to_robot", self.HANDSHAKE_COMMANDS['SETUP_2'], 16, "Phone → SETUP #2")
            await asyncio.sleep(0.5)  # Don't wait for response
            
            # Step 19: Phone → FINAL_INIT
            print("🔄 Step 19: Phone → FINAL_INIT")
            self.handshake_step = 19
            await self.client.write_gatt_char(self.WRITE_CHAR_UUID, self.HANDSHAKE_COMMANDS['FINAL_INIT'])
            self.log_message("phone_to_robot", self.HANDSHAKE_COMMANDS['FINAL_INIT'], 19, "Phone → FINAL_INIT")
            await asyncio.sleep(0.5)  # Don't wait for response
            
            # Step 20: Phone → AUTH/STATUS (FINAL - triggers heartbeat mode)
            print("🔄 Step 20: Phone → AUTH/STATUS (triggers heartbeat)")
            self.handshake_step = 20
            await self.client.write_gatt_char(self.WRITE_CHAR_UUID, self.HANDSHAKE_COMMANDS['AUTH_STATUS'])
            self.log_message("phone_to_robot", self.HANDSHAKE_COMMANDS['AUTH_STATUS'], 20, "Phone → AUTH/STATUS (final)")
            
            # Wait for robot to start sending heartbeats (indicates success)
            print("⏳ Waiting for robot heartbeat (indicates session ready)...")
            await asyncio.sleep(2.0)  # Give robot time to start heartbeats
            
            # Mark handshake complete
            self.handshake_complete = True
            self.session_active = True
            
            print("✅ Complete 19-step handshake successful!")
            print("🔄 Session active - robot will now send heartbeat requests")
            print("🎯 About to start interactive session...")
            return True
            
        except Exception as e:
            print(f"❌ Handshake failed: {e}")
            return False
    
    async def send_movement_command(self, key: str) -> bool:
        """Send movement command based on key press"""
        if not self.session_active:
            print("⚠️  Session not active - cannot send movement commands")
            return False
        
        if key not in self.MOVEMENT_COMMANDS:
            print(f"⚠️  Unknown movement key: {key}")
            return False
        
        try:
            # Use write_lock to prevent conflicts with heartbeat responses
            async with self.write_lock:
                command = self.MOVEMENT_COMMANDS[key]
                await self.client.write_gatt_char(self.WRITE_CHAR_UUID, command)
                
                # Show which movement was sent (updated descriptions)
                movement_names = {
                    'w': 'FORWARD + RIGHT TURN',
                    'a': 'TURN LEFT', 
                    's': 'BACKWARD',
                    'd': 'FORWARD + LEFT TURN',
                    ' ': 'STOP'
                }
                print(f"🎮 Movement: {movement_names.get(key, key.upper())}")
                
                # Log movement command
                self.log_message("phone_to_robot", command, 0, f"Movement: {movement_names.get(key, key)}")
                return True
            
        except Exception as e:
            print(f"❌ Movement command failed: {e}")
            return False
    
    async def interactive_session(self):
        """Interactive session with movement controls"""
        print("\n🎮 MOVEMENT CONTROLS:")
        print("   W = Forward + Right Turn")
        print("   A = Turn Left")
        print("   S = Backward") 
        print("   D = Forward + Left Turn")
        print("   SPACE = Stop")
        print("   Q = Quit")
        print("\n(Press keys to control robot, no need to press Enter)")
        print("🔄 Session active:", self.session_active)
        
        old_settings = None  # Initialize outside try block
        try:
            import sys, tty, termios
            print("✅ Terminal control imports successful")
            
            # Set terminal to raw mode for immediate key capture
            old_settings = termios.tcgetattr(sys.stdin)
            tty.setcbreak(sys.stdin.fileno())
            print("✅ Terminal set to raw mode")
            
            print("\n🤖 Robot ready! Press movement keys...")
            print("   (Press 'q' to quit)")
            
            # Run for a maximum of 60 seconds to avoid hanging
            start_time = time.time()
            max_session_time = 60
            
            while self.session_active and (time.time() - start_time) < max_session_time:
                # Check for key press (non-blocking)
                if sys.stdin in select.select([sys.stdin], [], [], 0)[0]:
                    key = sys.stdin.read(1).lower()
                    
                    if key == 'q':
                        print("\n👋 Quitting...")
                        break
                    elif key in self.MOVEMENT_COMMANDS:
                        await self.send_movement_command(key)
                        # Small delay after movement command to prevent conflicts
                        await asyncio.sleep(0.2)
                
                # Small delay to prevent CPU spinning
                await asyncio.sleep(0.1)
            
            if time.time() - start_time >= max_session_time:
                print(f"\n⏱️  Session timeout after {max_session_time} seconds")
                
        except ImportError as e:
            print(f"⚠️  Terminal control import error: {e}")
            print("⚠️  Advanced key capture not available, using simple input mode")
            
            # Fallback to simple input mode
            while self.session_active:
                try:
                    key = input("Enter movement key (w/a/s/d/space/q): ").lower().strip()
                    if key == 'q':
                        break
                    elif key == 'space':
                        key = ' '
                    
                    if key in self.MOVEMENT_COMMANDS:
                        await self.send_movement_command(key)
                    
                except KeyboardInterrupt:
                    break
                    
        except Exception as e:
            print(f"❌ Interactive session error: {e}")
        finally:
            # Restore terminal settings
            try:
                if old_settings:
                    termios.tcsetattr(sys.stdin, termios.TCSADRAIN, old_settings)
                    print("✅ Terminal settings restored")
            except:
                pass
    
    async def run(self):
        """Main execution flow (modified from v4 to add movement controls)"""
        try:
            print("🤖 EcoFlow BLADE Replicator v5")
            print("=" * 40)
            print("🔍 Scanning for EcoFlow BLADE robot...")
            
            # Connect using UUID (identical to v4)
            self.client = BleakClient(self.DEVICE_UUID)
            await self.client.connect()
            print(f"📱 Connected to {self.DEVICE_UUID}")
            
            # Enable notifications
            await self.client.start_notify(self.NOTIFY_CHAR_UUID, self.notification_handler)
            print("🔔 Notifications enabled")
            
            # Perform complete handshake
            if await self.perform_complete_handshake():
                print(f"📄 Session log: {self.log_file}")
                print("🚀 Starting interactive movement session...")
                
                # Start interactive movement session (NEW in v5)
                await self.interactive_session()
            else:
                print("❌ Handshake failed - session terminated")
                
        except Exception as e:
            print(f"❌ Connection failed: {e}")
        finally:
            if self.client and self.client.is_connected:
                await self.client.stop_notify(self.NOTIFY_CHAR_UUID)
                await self.client.disconnect()
                print("📴 Disconnected from robot")

async def main():
    controller = EcoFlowBladeReplicatorV5()
    await controller.run()

if __name__ == "__main__":
    asyncio.run(main())