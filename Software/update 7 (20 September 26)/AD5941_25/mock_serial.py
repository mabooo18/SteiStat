import time
import math
import random

class MockSerial:
    """
    A software simulator for the AD5941 Potentiostat Firmware.
    This class mimics the behavior of the pyserial `serial.Serial` object,
    allowing the GUI and test scripts to run without physical hardware connected.
    """
    def __init__(self, port, baudrate=1000000, timeout=1):
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self.is_open = True
        
        self.state = "IDLE"
        self.buffer = b""
        self.start_time = 0
        
        # CA params
        self.ca_vzero = 1100.0
        self.ca_sbias = 0.0
        self.ca_amp = 500.0
        self.ca_len = 5000
        self.ca_last_time = 0
        
        # CV params
        self.cv_start = -200.0
        self.cv_stop = 600.0
        self.cv_step = 2.0
        self.cv_rate = 100.0
        self.cv_cycles = 2
        self.cv_current_v = -200.0
        self.cv_dir = 1
        self.cv_cycle_count = 0
        self.cv_last_time = 0
        
        # EIS params
        self.eis_freqlo = 1.0
        self.eis_freqhi = 10000.0
        self.eis_nfreqs = 50
        self.eis_bias = 0.0
        self.eis_amp = 50.0
        self.eis_current_step = 0
        self.eis_last_time = 0
        
    def write(self, data):
        cmd = data.decode('ascii', errors='ignore').strip()
        if not cmd:
            return len(data)
            
        if cmd.startswith('K'):
            parts = cmd[2:].split(',')
            if len(parts) >= 4:
                self.ca_vzero, self.ca_sbias, self.ca_amp, self.ca_len = map(float, parts)
        elif cmd == 'J':
            self.state = "CA"
            self.start_time = time.time()
            self.ca_last_time = self.start_time
        elif cmd.startswith('D ') and ',' in cmd:
            parts = cmd[2:].split(',')
            if len(parts) >= 5:
                self.cv_start, self.cv_stop, self.cv_step, self.cv_rate, self.cv_cycles = map(float, parts)
        elif cmd == 'M':
            self.state = "CV"
            self.start_time = time.time()
            self.cv_current_v = self.cv_start
            self.cv_dir = 1 if self.cv_stop > self.cv_start else -1
            self.cv_cycle_count = 0
            self.cv_last_time = self.start_time
        elif cmd.startswith('W'): self.eis_freqlo = float(cmd[2:])
        elif cmd.startswith('X'): self.eis_freqhi = float(cmd[2:])
        elif cmd.startswith('y'): self.eis_nfreqs = int(cmd[2:])
        elif cmd == 'P':
            self.state = "EIS"
            self.eis_current_step = 0
            self.eis_last_time = time.time()
        elif cmd == 'O':
            self.state = "OCP"
            self.start_time = time.time()
            
        return len(data)
            
    def reset_input_buffer(self):
        self.buffer = b""
        
    @property
    def in_waiting(self):
        self._generate_data()
        return len(self.buffer)
        
    def readline(self):
        self._generate_data()
        if b'\n' in self.buffer:
            line, self.buffer = self.buffer.split(b'\n', 1)
            return line + b'\n'
        return b""
        
    def _generate_data(self):
        now = time.time()
        if self.state == "CA":
            if now - self.start_time > (self.ca_len / 1000.0):
                self.state = "IDLE"
            elif now - self.ca_last_time > 0.01: # 100Hz output
                t = now - self.start_time
                # Simulate a Cottrell-like exponential decay response
                current = (self.ca_amp / 10.0) * math.exp(-t / 0.5) + random.uniform(-0.2, 0.2)
                self.buffer += f"{current:.2f},\n".encode('ascii')
                self.ca_last_time = now
                
        elif self.state == "CV":
            if self.cv_cycle_count >= self.cv_cycles:
                self.state = "IDLE"
            elif now - self.cv_last_time > (self.cv_step / self.cv_rate):
                self.cv_last_time = now
                v = self.cv_current_v
                
                # Simulate a better Randles-Sevcik duck curve (Gaussian peaks)
                E_p = (self.cv_start + self.cv_stop) / 2.0
                width = abs(self.cv_stop - self.cv_start) / 5.0
                if width == 0: width = 1.0
                if self.cv_dir > 0:
                    i = 50.0 * math.exp(-((v - E_p)/width)**2) + 10.0 + random.uniform(-1, 1)
                else:
                    i = -40.0 * math.exp(-((v - (E_p - 60.0))/width)**2) - 10.0 + random.uniform(-1, 1)
                
                self.buffer += f"{v:.2f},{i:.2f},\n".encode('ascii')
                
                self.cv_current_v += self.cv_dir * self.cv_step
                
                if (self.cv_dir > 0 and self.cv_current_v >= self.cv_stop) or \
                   (self.cv_dir < 0 and self.cv_current_v <= self.cv_start):
                    self.cv_dir *= -1
                    if self.cv_dir == (1 if self.cv_stop > self.cv_start else -1):
                        self.cv_cycle_count += 1

        elif self.state == "EIS":
            if self.eis_current_step >= self.eis_nfreqs * 2:
                self.state = "IDLE"
            elif now - self.eis_last_time > 0.05: # Fast simulation
                self.eis_last_time = now
                step = self.eis_current_step % self.eis_nfreqs
                t = (self.eis_nfreqs - step) / self.eis_nfreqs * math.pi
                
                # Simulate a Nyquist semi-circle (Randles circuit)
                real = 100 + 80 * math.cos(t) + random.uniform(-2, 2)
                imag = -80 * math.sin(t) + random.uniform(-2, 2)
                
                idx = step + 1
                if self.eis_current_step < self.eis_nfreqs:
                    self.buffer += f"{idx}={real:.2f},{imag:.2f},\n".encode('ascii')
                else:
                    # Calibration run simulation
                    self.buffer += f"{idx}={10000.0:.2f},{0.0:.2f},\n".encode('ascii')
                self.eis_current_step += 1
                
        elif self.state == "OCP":
            if now - self.start_time > 1.0: # Takes 1 second
                ocp = 10.5 + random.uniform(-0.1, 0.1)
                self.buffer += f"{ocp:.2f} \n".encode('ascii')
                self.state = "IDLE"

    def close(self):
        self.is_open = False
