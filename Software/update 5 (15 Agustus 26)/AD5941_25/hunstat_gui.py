import tkinter as tk
from tkinter import ttk, messagebox
import serial
import serial.tools.list_ports
import threading
import time
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg, NavigationToolbar2Tk
import numpy as np
import tkinter.filedialog

class HunStatGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("HunStat 2.0 - Python Interface")
        self.root.geometry("1000x700")
        self.root.protocol("WM_DELETE_WINDOW", self.on_closing)

        self.ser = None
        self.is_measuring = False
        
        # State for post-measurement toggling
        self.last_cv_data = None   # (time_axis, volts, currents)
        self.last_eis_data = None  # (freqs, reals, imags, mags, phases)
        self.last_ca_data = None   # (time_axis, currents)
        
        self.setup_ui()
    
    def on_closing(self):
        self.is_measuring = False
        if self.ser and self.ser.is_open:
            self.ser.close()
        self.root.quit()
        self.root.destroy()

    def setup_ui(self):
        # Top Frame for Connection
        top_frame = ttk.Frame(self.root, padding=10)
        top_frame.pack(side=tk.TOP, fill=tk.X)
        
        ttk.Label(top_frame, text="COM Port:").pack(side=tk.LEFT, padx=5)
        self.port_var = tk.StringVar()
        self.port_cb = ttk.Combobox(top_frame, textvariable=self.port_var, width=15)
        self.port_cb.pack(side=tk.LEFT, padx=5)
        self.refresh_ports()
        
        ttk.Button(top_frame, text="Refresh", command=self.refresh_ports).pack(side=tk.LEFT, padx=5)
        
        self.connect_btn = ttk.Button(top_frame, text="Connect", command=self.toggle_connection)
        self.connect_btn.pack(side=tk.LEFT, padx=5)
        
        self.status_lbl = ttk.Label(top_frame, text="Disconnected", foreground="red")
        self.status_lbl.pack(side=tk.LEFT, padx=20)
        
        # Main Content PanedWindow (Left: Controls, Right: Plot)
        self.paned = ttk.PanedWindow(self.root, orient=tk.HORIZONTAL)
        self.paned.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)
        
        # Left Panel (Notebook for Methods)
        left_frame = ttk.Frame(self.paned)
        self.paned.add(left_frame, weight=1)
        
        self.notebook = ttk.Notebook(left_frame)
        self.notebook.pack(fill=tk.BOTH, expand=True)
        
        self.setup_ocp_tab()
        self.setup_cv_tab()
        self.setup_eis_tab()
        self.setup_ca_tab()
        
        # Right Panel (Plot)
        right_frame = ttk.Frame(self.paned)
        self.paned.add(right_frame, weight=3)
        
        self.fig, self.ax = plt.subplots(figsize=(6, 4))
        self.canvas = FigureCanvasTkAgg(self.fig, master=right_frame)
        self.canvas.get_tk_widget().pack(side=tk.TOP, fill=tk.BOTH, expand=True)
        
        self.toolbar = NavigationToolbar2Tk(self.canvas, right_frame)
        self.toolbar.update()
        self.toolbar.pack(side=tk.BOTTOM, fill=tk.X)

        # Bottom Console
        bottom_frame = ttk.Frame(self.root, padding=10)
        bottom_frame.pack(side=tk.BOTTOM, fill=tk.X)
        ttk.Label(bottom_frame, text="Log:").pack(anchor=tk.W)
        self.log_text = tk.Text(bottom_frame, height=5, state=tk.DISABLED)
        self.log_text.pack(fill=tk.X)
        
    def refresh_ports(self):
        ports = [port.device for port in serial.tools.list_ports.comports()]
        ports.insert(0, "MOCK") # Add Software Simulation Mode
        self.port_cb['values'] = ports
        if ports:
            self.port_cb.current(0)
            
    def toggle_connection(self):
        if self.ser and self.ser.is_open:
            self.ser.close()
            self.ser = None
            self.connect_btn.config(text="Connect")
            self.status_lbl.config(text="Disconnected", foreground="red")
            self.log("Disconnected.")
        else:
            port = self.port_var.get()
            if not port:
                messagebox.showerror("Error", "Please select a COM port.")
                return
            try:
                if port == "MOCK":
                    from mock_serial import MockSerial
                    self.ser = MockSerial(port, 1000000, timeout=1)
                else:
                    self.ser = serial.Serial(port, 1000000, timeout=1)
                    
                time.sleep(1.5) # Wait for boot
                self.connect_btn.config(text="Disconnect")
                self.status_lbl.config(text=f"Connected ({port})", foreground="green")
                self.log(f"Connected to {port} at 1000000 baud.")
            except Exception as e:
                messagebox.showerror("Connection Error", str(e))
                
    def log(self, msg):
        self.root.after(0, self._log_ui, msg)

    def _log_ui(self, msg):
        self.log_text.config(state=tk.NORMAL)
        self.log_text.insert(tk.END, msg + "\n")
        self.log_text.see(tk.END)
        self.log_text.config(state=tk.DISABLED)

    # --- TAB SETUPS ---
    def setup_ocp_tab(self):
        frame = ttk.Frame(self.notebook, padding=10)
        self.notebook.add(frame, text="OCP")
        
        ttk.Label(frame, text="WE mV:").grid(row=0, column=0, sticky=tk.W, pady=5)
        self.ocp_we = tk.StringVar(value="0.0")
        ttk.Entry(frame, textvariable=self.ocp_we).grid(row=0, column=1, pady=5)
        
        ttk.Button(frame, text="Run OCP", command=lambda: self.run_measurement("OCP")).grid(row=1, column=0, columnspan=2, pady=15)
        
        ttk.Label(frame, text="Measured OCP (mV):").grid(row=2, column=0, sticky=tk.W, pady=5)
        self.ocp_result = tk.StringVar(value="---")
        ttk.Entry(frame, textvariable=self.ocp_result, state='readonly').grid(row=2, column=1, pady=5)
        
        ttk.Button(frame, text="Copy to EIS DC Bias", command=self.copy_ocp_to_eis).grid(row=3, column=0,columnspan=2, pady=5)
        
    def copy_ocp_to_eis(self):
        val = self.ocp_result.get()
        if val and val != "---":
            self.eis_vars["eis_bias"].set(val)
            self.log(f"Copied {val} mV to EIS DC Bias.")
            self.notebook.select(2)

    def setup_cv_tab(self):
        frame = ttk.Frame(self.notebook, padding=10)
        self.notebook.add(frame, text="CV")
        
        params = [("Start Voltage (mV):", "cv_start", "-200.0"),
                  ("Stop Voltage (mV):", "cv_stop", "600.0"),
                  ("Step Voltage (mV):", "cv_step", "2.0"),
                  ("Scan Rate (mV/s):", "cv_rate", "100.0"),
                  ("Cycles:", "cv_cycles", "2")]
        
        self.cv_vars = {}
        for i, (label, key, val) in enumerate(params):
            ttk.Label(frame, text=label).grid(row=i, column=0, sticky=tk.W, pady=2)
            var = tk.StringVar(value=val)
            ttk.Entry(frame, textvariable=var).grid(row=i, column=1, pady=2)
            self.cv_vars[key] = var
            
        ttk.Label(frame, text="Plot Type:").grid(row=len(params), column=0, sticky=tk.W, pady=2)
        self.cv_plot_type = tk.StringVar(value="V-I (Duck Curve)")
        
        rb_frame = ttk.Frame(frame)
        rb_frame.grid(row=len(params), column=1, sticky=tk.W, pady=2)
        ttk.Radiobutton(rb_frame, text="V-I", variable=self.cv_plot_type, value="V-I (Duck Curve)", command=self.redraw_cv).pack(side=tk.LEFT)
        ttk.Radiobutton(rb_frame, text="V/I vs. t(s)", variable=self.cv_plot_type, value="V/I vs. t(s)", command=self.redraw_cv).pack(side=tk.LEFT)
            
        btn_frame = ttk.Frame(frame)
        btn_frame.grid(row=len(params)+1, column=0, columnspan=2, pady=15)
        ttk.Button(btn_frame, text="Run CV", command=lambda: self.run_measurement("CV")).pack(side=tk.LEFT, padx=5)
        ttk.Button(btn_frame, text="Save to CSV", command=self.save_cv_csv).pack(side=tk.LEFT, padx=5)

    def setup_eis_tab(self):
        frame = ttk.Frame(self.notebook, padding=10)
        self.notebook.add(frame, text="EIS")
        
        params = [("Low Freq (Hz):", "eis_freqlo", "1.0"),
                  ("High Freq (Hz):", "eis_freqhi", "10000.0"),
                  ("Number of Freqs:", "eis_nfreqs", "50"),
                  ("DC Bias (mV):", "eis_bias", "0.0"),
                  ("AC Amplitude (mV):", "eis_amp", "50.0")]
        
        self.eis_vars = {}
        for i, (label, key, val) in enumerate(params):
            ttk.Label(frame, text=label).grid(row=i, column=0, sticky=tk.W, pady=2)
            var = tk.StringVar(value=val)
            ttk.Entry(frame, textvariable=var).grid(row=i, column=1, pady=2)
            self.eis_vars[key] = var
            
        ttk.Label(frame, text="Plot Type:").grid(row=len(params), column=0, sticky=tk.W, pady=2)
        self.eis_plot_type = tk.StringVar(value="Nyquist")
        
        rb_frame = ttk.Frame(frame)
        rb_frame.grid(row=len(params), column=1, sticky=tk.W, pady=2)
        ttk.Radiobutton(rb_frame, text="Nyquist", variable=self.eis_plot_type, value="Nyquist", command=self.redraw_eis).pack(side=tk.LEFT)
        ttk.Radiobutton(rb_frame, text="Bode", variable=self.eis_plot_type, value="Bode", command=self.redraw_eis).pack(side=tk.LEFT)
            
        btn_frame = ttk.Frame(frame)
        btn_frame.grid(row=len(params)+1, column=0, columnspan=2, pady=15)
        ttk.Button(btn_frame, text="Run EIS", command=lambda: self.run_measurement("EIS")).pack(side=tk.LEFT, padx=5)
        ttk.Button(btn_frame, text="Save to CSV", command=self.save_eis_csv).pack(side=tk.LEFT, padx=5)
        
    def setup_ca_tab(self):
        frame = ttk.Frame(self.notebook, padding=10)
        self.notebook.add(frame, text="CA")
        
        params = [("Vzero (mV):", "ca_vzero", "1100.0"),
                  ("Sensor Bias (mV):", "ca_sbias", "0.0"),
                  ("Pulse Amplitude (mV):", "ca_amp", "500.0"),
                  ("Pulse Length (ms):", "ca_len", "5000")]
        
        self.ca_vars = {}
        for i, (label, key, val) in enumerate(params):
            ttk.Label(frame, text=label).grid(row=i, column=0, sticky=tk.W, pady=2)
            var = tk.StringVar(value=val)
            ttk.Entry(frame, textvariable=var).grid(row=i, column=1, pady=2)
            self.ca_vars[key] = var
            
        btn_frame = ttk.Frame(frame)
        btn_frame.grid(row=len(params), column=0, columnspan=2, pady=15)
        ttk.Button(btn_frame, text="Run CA", command=lambda: self.run_measurement("CA")).pack(side=tk.LEFT, padx=5)
        ttk.Button(btn_frame, text="Save to CSV", command=self.save_ca_csv).pack(side=tk.LEFT, padx=5)

    # --- CSV EXPORT LOGIC ---
    def save_cv_csv(self):
        if not self.last_cv_data:
            messagebox.showinfo("No Data", "No CV data to save.")
            return
        filepath = tk.filedialog.asksaveasfilename(defaultextension=".csv", filetypes=[("CSV Files", "*.csv")])
        if filepath:
            try:
                import csv
                t, v, i = self.last_cv_data
                with open(filepath, 'w', newline='') as f:
                    writer = csv.writer(f)
                    writer.writerow(["Time (s)", "Voltage (mV)", "Current (uA)"])
                    for row in zip(t, v, i):
                        writer.writerow(row)
                self.log(f"Saved CV data to {filepath}")
            except Exception as e:
                messagebox.showerror("Save Error", str(e))

    def save_eis_csv(self):
        if not self.last_eis_data:
            messagebox.showinfo("No Data", "No EIS data to save.")
            return
        filepath = tk.filedialog.asksaveasfilename(defaultextension=".csv", filetypes=[("CSV Files", "*.csv")])
        if filepath:
            try:
                import csv
                f_arr, r, i, m, p = self.last_eis_data
                with open(filepath, 'w', newline='') as f:
                    writer = csv.writer(f)
                    writer.writerow(["Frequency (Hz)", "Z' (Ohm)", "-Z'' (Ohm)", "|Z| (Ohm)", "Phase (deg)"])
                    # Restore imaginary orientation (we store raw 'i' directly now in read_and_plot_eis, but it's plotted as -i)
                    for row in zip(f_arr, r, [-x for x in i], m, p):
                        writer.writerow(row)
                self.log(f"Saved EIS data to {filepath}")
            except Exception as e:
                messagebox.showerror("Save Error", str(e))

    def save_ca_csv(self):
        if not self.last_ca_data:
            messagebox.showinfo("No Data", "No CA data to save.")
            return
        filepath = tk.filedialog.asksaveasfilename(defaultextension=".csv", filetypes=[("CSV Files", "*.csv")])
        if filepath:
            try:
                import csv
                t, i = self.last_ca_data
                with open(filepath, 'w', newline='') as f:
                    writer = csv.writer(f)
                    writer.writerow(["Time (s)", "Current (uA)"])
                    for row in zip(t, i):
                        writer.writerow(row)
                self.log(f"Saved CA data to {filepath}")
            except Exception as e:
                messagebox.showerror("Save Error", str(e))

    # --- MEASUREMENT EXECUTION ---
    def run_measurement(self, mode):
        if not self.ser or not self.ser.is_open:
            messagebox.showerror("Error", "Not connected to a COM port.")
            return
            
        if self.is_measuring:
            messagebox.showwarning("Warning", "A measurement is already in progress.")
            return
            
        self.is_measuring = True
        
        # Clear previously cached data
        self.last_cv_data = None
        self.last_eis_data = None
        self.last_ca_data = None
        
        if mode != "OCP":
            self.root.after(0, self.clear_plot_ui)
        
        threading.Thread(target=self.measurement_thread, args=(mode,), daemon=True).start()
        
    def clear_plot_ui(self):
        self.fig.clf()
        self.ax = self.fig.add_subplot(111)
        self.canvas.draw_idle()
        
    def measurement_thread(self, mode):
        try:
            self.ser.reset_input_buffer()
            if mode == "OCP":
                we = 0.0
                self.ser.write(b"S 1\n") # Enable SeeedStat (ASCII) mode
                time.sleep(0.1)
                self.ser.write(f"M {we}\n".encode('ascii'))
                time.sleep(0.1)
                self.ser.write(b"O\n")
                self.log("Started OCP...")

                time.sleep(1)
                if self.ser.in_waiting:
                    line = self.ser.readline().decode('ascii', errors='ignore').strip()
                    if line == 'Z':
                        line = self.ser.readline().decode('ascii', errors='ignore').strip()
                    self.log(f"OCP Result: {line}")
                    try:
                        if ',' in line:
                            val = float(line.split(',')[-1])
                        else:
                            val = float(line)
                        self.root.after(0, self.ocp_result.set, f"{val:.2f}")
                    except ValueError:
                        pass
                
            elif mode == "CV":
                start = float(self.cv_vars["cv_start"].get())
                stop = float(self.cv_vars["cv_stop"].get())
                step = float(self.cv_vars["cv_step"].get())
                rate = float(self.cv_vars["cv_rate"].get())
                cycles = int(self.cv_vars["cv_cycles"].get())
                
                cmd = f"D {start},{stop},{step},{rate},{cycles}\n"
                self.ser.write(cmd.encode('ascii'))
                time.sleep(0.1)
                self.ser.write(b"M\n")
                self.log("Started CV...")
                self.read_and_plot_cv(cycles, start, stop, step, rate)
                
            elif mode == "EIS":
                flo = float(self.eis_vars["eis_freqlo"].get())
                fhi = float(self.eis_vars["eis_freqhi"].get())
                nfreqs = int(self.eis_vars["eis_nfreqs"].get())
                bias = float(self.eis_vars["eis_bias"].get())
                amp = float(self.eis_vars["eis_amp"].get())
                
                self.ser.write(f"W {flo}\n".encode('ascii'))
                time.sleep(0.05)
                self.ser.write(f"X {fhi}\n".encode('ascii'))
                time.sleep(0.05)
                self.ser.write(f"y {nfreqs}\n".encode('ascii'))
                time.sleep(0.05)
                self.ser.write(f"B {bias}\n".encode('ascii'))
                time.sleep(0.05)
                self.ser.write(f"Y {amp}\n".encode('ascii'))
                time.sleep(0.05)
                
                self.ser.write(b"D 1,1\n") # SeeedStatMode
                time.sleep(0.05)
                self.ser.write(b"P\n") # Start EIS
                self.log("Started EIS...")
                self.read_and_plot_eis(nfreqs) # Calibrated output yields 1 sweep
                
            elif mode == "CA":
                vz = float(self.ca_vars["ca_vzero"].get())
                sb = float(self.ca_vars["ca_sbias"].get())
                amp = float(self.ca_vars["ca_amp"].get())
                length = int(self.ca_vars["ca_len"].get())
                
                cmd = f"K {vz},{sb},{amp},{length}\n"
                self.ser.write(cmd.encode('ascii'))
                time.sleep(0.1)
                self.ser.write(b"J\n")
                self.log("Started CA...")
                self.read_and_plot_ca(length)
                
        except Exception as e:
            self.log(f"Measurement Error: {e}")
        finally:
            self.is_measuring = False
            self.log(f"{mode} Measurement Finished.")
            
    # --- REDRAW LOGIC ---
    def redraw_cv(self):
        if not self.last_cv_data: return
        t, v, i = self.last_cv_data
        is_time_plot = self.cv_plot_type.get() == "V/I vs. t(s)"
        
        self.fig.clf()
        if is_time_plot:
            ax1 = self.fig.add_subplot(111)
            ax2 = ax1.twinx()
            ax1.plot(t, v, '-', markersize=2, color='blue', label='Voltage')
            ax2.plot(t, i, '-', markersize=2, color='red', label='Current')
            ax1.set_xlabel("Time (s)")
            ax1.set_ylabel('Voltage (mV)', color='blue')
            ax2.set_ylabel('Current (µA)', color='red')
            ax1.set_title("CV: V/I vs. t(s)")
            ax1.grid(True)
            self.ax = ax1
        else:
            self.ax = self.fig.add_subplot(111)
            self.ax.plot(v, i, '-', markersize=2, color='blue')
            self.ax.set_xlabel("Voltage (mV)")
            self.ax.set_ylabel("Current (µA)")
            self.ax.set_title("Cyclic Voltammetry")
            self.ax.grid(True)
            
        self.fig.tight_layout()
        self.canvas.draw_idle()

    def redraw_eis(self):
        if not self.last_eis_data: return
        f, r, i, m, p = self.last_eis_data
        is_bode = self.eis_plot_type.get() == "Bode"
        
        self.fig.clf()
        if is_bode:
            ax1 = self.fig.add_subplot(111)
            ax2 = ax1.twinx()
            ax1.plot(f, m, 'o-', markersize=5, color='blue', label='|Z|')
            ax2.plot(f, p, 'o-', markersize=5, color='red', label='Phase')
            ax1.set_xscale('log')
            ax1.set_yscale('log')
            ax1.set_xlabel("Frequency (Hz)")
            ax1.set_ylabel('|Z| (Ohm)', color='blue')
            ax2.set_ylabel('Phase (deg)', color='red')
            ax1.set_title("EIS (Bode Plot)")
            ax1.grid(True)
            self.ax = ax1
        else:
            self.ax = self.fig.add_subplot(111)
            # Nyquist plots -Imaginary on Y
            neg_imags = [-x for x in i]
            self.ax.plot(r, neg_imags, 'o-', markersize=5, color='blue')
            self.ax.set_xlabel("Z' (Real)")
            self.ax.set_ylabel("-Z'' (Imaginary)")
            self.ax.set_title("EIS (Nyquist Plot)")
            self.ax.grid(True)
            self.ax.set_aspect('equal', adjustable='datalim')
            
        self.fig.tight_layout()
        self.canvas.draw_idle()

    # --- DATA PARSING & LIVE PLOTTING ---
    def schedule_live_plot(self, mode):
        if mode == "CV":
            self.root.after(0, self.redraw_cv)
        elif mode == "EIS":
            self.root.after(0, self.redraw_eis)

    def schedule_ca_plot(self, t, i):
        import copy
        t_c = list(t) if isinstance(t, (list, np.ndarray)) else t
        i_c = list(i)
        self.root.after(0, self.update_ca_ui, t_c, i_c)

    def update_ca_ui(self, t, i):
        self.fig.clf()
        self.ax = self.fig.add_subplot(111)
        self.ax.plot(t, i, '-', markersize=2, color='blue')
        self.ax.set_xlabel("Time (s)")
        self.ax.set_ylabel("Current (µA)")
        self.ax.set_title("Chronoamperometry")
        self.ax.grid(True)
        self.fig.tight_layout()
        self.canvas.draw_idle()

    def read_and_plot_cv(self, cycles, start, stop, step, rate):
        time_axis = []
        volts = []
        currents = []
        
        v_range = abs(stop - start)
        time_per_half_cycle = v_range / rate
        total_time = time_per_half_cycle * 2 * cycles
        
        start_time = time.time()
        
        while (time.time() - start_time) < total_time + 5:
            if self.ser.in_waiting:
                line = self.ser.readline().decode('ascii', errors='ignore').strip()
                if line and ',' in line:
                    # Firmware prefixes every CV sample with "CV," (see cv.cpp
                    # RampShowResult), so strip that tag before parsing numbers.
                    if line.upper().startswith("CV,"):
                        line = line[3:]
                    parts = line.split(',')
                    if len(parts) >= 2:
                        try:
                            v = float(parts[0])
                            i = float(parts[1])
                            
                            t = time.time() - start_time
                            time_axis.append(t)
                            volts.append(v)
                            currents.append(i)
                            self.last_cv_data = (time_axis, volts, currents)
                            
                            # Throttle drawing to every 10 data points
                            if len(volts) % 10 == 0:
                                self.schedule_live_plot("CV")
                        except:
                            pass
                            
        self.schedule_live_plot("CV")

    def read_and_plot_ca(self, length_ms):
        currents = []
        start_time = time.time()
        
        while (time.time() - start_time) < (length_ms / 1000.0) + 2.0:
            if self.ser.in_waiting:
                line = self.ser.readline().decode('ascii', errors='ignore').strip()
                if line:
                    try:
                        val = float(line.replace(',', ''))
                        currents.append(val)
                        
                        # Throttle drawing to every 10 data points
                        if len(currents) % 10 == 0:
                            t_axis = np.linspace(0, (time.time()-start_time), len(currents))
                            self.last_ca_data = (t_axis, currents)
                            self.schedule_ca_plot(t_axis, currents)
                    except:
                        pass
                        
        if currents:
            t_axis = np.linspace(0, length_ms/1000.0, len(currents))
            self.last_ca_data = (t_axis, currents)
            self.schedule_ca_plot(t_axis, currents)

    def read_and_plot_eis(self, expected_lines):
        reals = []
        imags = []
        freqs = []
        mags = []
        phases = []
        
        start_time = time.time()
        lines_read = 0
        
        flo = float(self.eis_vars["eis_freqlo"].get())
        fhi = float(self.eis_vars["eis_freqhi"].get())
        nfreqs = int(self.eis_vars["eis_nfreqs"].get())
        
        denom = max(1, nfreqs - 1)
        self.is_second_sweep = False
        
        while lines_read < expected_lines and (time.time() - start_time) < 120:
            if self.ser.in_waiting:
                line = self.ser.readline().decode('ascii', errors='ignore').strip()
                if '=' in line:
                    parts = line.split('=')
                    idx_str = parts[0]
                    data_parts = parts[1].split(',')
                    if len(data_parts) >= 2:
                        try:
                            idx = int(idx_str) - 1
                            if idx == 0 and len(reals) > 0:
                                self.is_second_sweep = True
                                
                            if self.is_second_sweep:
                                lines_read += 1
                                continue
                                
                            r = float(data_parts[0])
                            i = float(data_parts[1])
                            
                            freq = flo * (10 ** (idx * (np.log10(fhi) - np.log10(flo)) / denom))
                            mag = np.sqrt(r**2 + i**2)
                            phase = np.arctan2(i, r) * 180 / np.pi
                            
                            reals.append(r)
                            imags.append(i)
                            freqs.append(freq)
                            mags.append(mag)
                            phases.append(phase)
                            self.last_eis_data = (freqs, reals, imags, mags, phases)
                            lines_read += 1
                            
                            # Update graph every 2 points or on the last point
                            if len(reals) % 2 == 0 or lines_read == nfreqs:
                                self.schedule_live_plot("EIS")
                        except:
                            pass
        
        self.schedule_live_plot("EIS")

if __name__ == "__main__":
    root = tk.Tk()
    app = HunStatGUI(root)
    root.mainloop()