import csv
import math
import random
import queue
import re
import threading
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

try:
    import serial
    import serial.tools.list_ports
except Exception:  # pragma: no cover - optional dependency
    serial = None


class SerialReader(threading.Thread):
    def __init__(self, port_name, baudrate, output_queue, stop_event):
        super().__init__(daemon=True)
        self.port_name = port_name
        self.baudrate = baudrate
        self.output_queue = output_queue
        self.stop_event = stop_event
        self.serial_port = None
        self._io_lock = threading.Lock()

    def run(self):
        if serial is None:
            self.output_queue.put(("status", "pyserial belum terpasang"))
            return

        try:
            self.serial_port = serial.Serial(self.port_name, self.baudrate, timeout=0.1)
            self.output_queue.put(("status", f"Terhubung ke {self.port_name}"))
        except Exception as exc:  # pragma: no cover - hardware dependent
            self.output_queue.put(("status", f"Gagal membuka port: {exc}"))
            return

        try:
            while not self.stop_event.is_set():
                line = self.serial_port.readline().decode("utf-8", errors="replace").strip()
                if line:
                    self.output_queue.put(("line", line))
        finally:
            try:
                if self.serial_port is not None and self.serial_port.is_open:
                    self.serial_port.close()
            except Exception:
                pass
            self.output_queue.put(("status", "Port serial ditutup"))

    def send_line(self, line):
        if self.serial_port is None or not self.serial_port.is_open:
            raise RuntimeError("Port belum terhubung")
        payload = (line.rstrip("\r\n") + "\n").encode("utf-8")
        with self._io_lock:
            self.serial_port.write(payload)
            self.serial_port.flush()


class TestUI(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("HunStat2 Test Console")
        self.geometry("1280x820")
        self.minsize(1040, 680)

        self.queue = queue.Queue()
        self.stop_event = threading.Event()
        self.reader = None
        self.data_points = []
        self.mode_points = {
            "OCP": [],
            "EIS": [],
            "CV": [],
            "CA": [],
            "SWV": [],
            "DPV": [],
            "EIS_RAW": [],
            "NYQUIST": [],
        }

        self._build_styles()
        self._build_layout()
        self._refresh_ports()
        self.after(100, self._poll_queue)

    def _build_styles(self):
        self.configure(bg="#101418")
        style = ttk.Style(self)
        try:
            style.theme_use("clam")
        except tk.TclError:
            pass
        style.configure("TFrame", background="#101418")
        style.configure("TLabel", background="#101418", foreground="#E8EEF2", font=("Segoe UI", 10))
        style.configure("Header.TLabel", font=("Segoe UI Semibold", 15), foreground="#FFFFFF")
        style.configure("TButton", font=("Segoe UI", 10), padding=6)
        style.configure("TEntry", fieldbackground="#1A2230", foreground="#E8EEF2")
        style.configure("TCombobox", fieldbackground="#1A2230", foreground="#E8EEF2")
        style.configure("TLabelframe", background="#101418", foreground="#E8EEF2")
        style.configure("TLabelframe.Label", background="#101418", foreground="#E8EEF2")

    def _build_layout(self):
        page_host = ttk.Frame(self)
        page_host.pack(fill="both", expand=True)
        page_host.rowconfigure(0, weight=1)
        page_host.columnconfigure(0, weight=1)

        page_canvas = tk.Canvas(page_host, bg="#101418", highlightthickness=0)
        page_canvas.grid(row=0, column=0, sticky="nsew")
        page_scrollbar = ttk.Scrollbar(page_host, orient="vertical", command=page_canvas.yview)
        page_scrollbar.grid(row=0, column=1, sticky="ns")
        page_canvas.configure(yscrollcommand=page_scrollbar.set)

        content = ttk.Frame(page_canvas, padding=0)
        content_window = page_canvas.create_window((0, 0), window=content, anchor="nw")

        def _sync_page_scrollregion(_event=None):
            page_canvas.configure(scrollregion=page_canvas.bbox("all"))

        def _sync_page_width(event):
            page_canvas.itemconfigure(content_window, width=event.width)

        content.bind("<Configure>", _sync_page_scrollregion)
        page_canvas.bind("<Configure>", _sync_page_width)
        page_canvas.bind_all(
            "<MouseWheel>",
            lambda event: page_canvas.yview_scroll(int(-1 * (event.delta / 120)), "units")
            if page_canvas.winfo_containing(event.x_root, event.y_root) is not None
            else None,
        )

        top = ttk.Frame(content, padding=12)
        top.pack(fill="x")
        ttk.Label(top, text="HunStat2 Test Console", style="Header.TLabel").pack(anchor="w")
        ttk.Label(top, text="Set parameter, kirim command, monitor serial, dan plot data seperti workflow HunStat2 EXE.").pack(anchor="w", pady=(2, 0))

        controls = ttk.LabelFrame(content, text="Koneksi", padding=12)
        controls.pack(fill="x", padx=12, pady=(0, 12))

        self.port_var = tk.StringVar()
        self.baud_var = tk.StringVar(value="1000000")
        self.status_var = tk.StringVar(value="Siap")
        self.filter_var = tk.StringVar(value="ALL")
        self.raw_command_var = tk.StringVar()
        self.source_var = tk.StringVar(value="BOARD")

        self.freq_lo_var = tk.StringVar(value="1")
        self.freq_hi_var = tk.StringVar(value="10000")
        self.nfreq_var = tk.StringVar(value="50")
        self.rcal_var = tk.StringVar(value="10000")
        self.amp_mv_var = tk.StringVar(value="50")
        self.bias_mv_var = tk.StringVar(value="0")
        self.offset_mv_var = tk.StringVar(value="0")
        self.eis_mode_var = tk.StringVar(value="0")
        self.seeedstat_var = tk.BooleanVar(value=True)
        self.method_var = tk.StringVar(value="EIS")
        self.cv_start_var = tk.StringVar(value="0.5")
        self.cv_stop_var = tk.StringVar(value="-0.22")
        self.cv_step_var = tk.StringVar(value="0.002")
        self.cv_scan_var = tk.StringVar(value="0.8")
        self.cv_cycle_var = tk.StringVar(value="1")

        self.ca_voltage_var = tk.StringVar(value="100")
        self.ca_duration_var = tk.StringVar(value="10")
        self.ca_rate_var = tk.StringVar(value="20")

        self.swv_start_var = tk.StringVar(value="0")
        self.swv_end_var = tk.StringVar(value="1400")
        self.swv_step_var = tk.StringVar(value="5")
        self.swv_amp_var = tk.StringVar(value="25")
        self.swv_freq_var = tk.StringVar(value="25")

        self.dpv_start_var = tk.StringVar(value="0")
        self.dpv_end_var = tk.StringVar(value="1400")
        self.dpv_step_var = tk.StringVar(value="5")
        self.dpv_amp_var = tk.StringVar(value="25")

        ttk.Label(controls, text="Port").grid(row=0, column=0, sticky="w")
        self.port_box = ttk.Combobox(controls, textvariable=self.port_var, width=28, state="readonly")
        self.port_box.grid(row=0, column=1, padx=(8, 12), sticky="w")
        ttk.Button(controls, text="Refresh", command=self._refresh_ports).grid(row=0, column=2, padx=(0, 12))

        ttk.Label(controls, text="Baud").grid(row=0, column=3, sticky="w")
        ttk.Entry(controls, textvariable=self.baud_var, width=16).grid(row=0, column=4, padx=(8, 12), sticky="w")

        ttk.Button(controls, text="Connect", command=self._connect).grid(row=0, column=5, padx=(0, 8))
        ttk.Button(controls, text="Disconnect", command=self._disconnect).grid(row=0, column=6, padx=(0, 12))
        ttk.Button(controls, text="Load Log", command=self._load_log).grid(row=0, column=7, padx=(0, 8))
        ttk.Button(controls, text="Load CV", command=self._load_cv_file).grid(row=0, column=8, padx=(0, 8))
        ttk.Button(controls, text="Dummy CV", command=self._load_dummy_cv).grid(row=0, column=9, padx=(0, 8))
        ttk.Button(controls, text="Dummy All", command=self._load_dummy_all).grid(row=0, column=10, padx=(0, 8))
        ttk.Button(controls, text="Clear", command=self._clear).grid(row=0, column=11)

        ttk.Label(controls, text="Sumber data").grid(row=1, column=0, sticky="w", pady=(8, 0))
        ttk.Radiobutton(
            controls,
            text="Board",
            variable=self.source_var,
            value="BOARD",
            command=self._on_source_changed,
        ).grid(row=1, column=1, sticky="w", padx=(8, 0), pady=(8, 0))
        ttk.Radiobutton(
            controls,
            text="Dummy",
            variable=self.source_var,
            value="DUMMY",
            command=self._on_source_changed,
        ).grid(row=1, column=2, sticky="w", pady=(8, 0))

        controls.columnconfigure(12, weight=1)
        ttk.Label(controls, textvariable=self.status_var).grid(row=2, column=0, columnspan=12, sticky="w", pady=(10, 0))

        cmd_box = ttk.LabelFrame(content, text="HunStat2 Commands", padding=12)
        cmd_box.pack(fill="x", padx=12, pady=(0, 12))

        ttk.Label(cmd_box, text="Raw command").grid(row=0, column=0, sticky="w")
        ttk.Entry(cmd_box, textvariable=self.raw_command_var, width=52).grid(row=0, column=1, padx=(8, 8), sticky="ew")
        ttk.Button(cmd_box, text="Send", command=self._send_raw).grid(row=0, column=2, padx=(0, 8))
        ttk.Button(cmd_box, text="Apply EIS Params", command=self._apply_eis_params).grid(row=0, column=3)

        ttk.Label(cmd_box, text="Method").grid(row=1, column=0, sticky="w", pady=(10, 0))
        ttk.Combobox(
            cmd_box,
            textvariable=self.method_var,
            values=["EIS", "EIS SeeedStat", "CV", "SWV", "DPV", "CA", "OCP"],
            state="readonly",
            width=22,
        ).grid(row=1, column=1, sticky="w", padx=(8, 8), pady=(10, 0))
        ttk.Button(cmd_box, text="Run Selected", command=self._run_selected_method).grid(row=1, column=2, sticky="w", pady=(10, 0))
        ttk.Button(cmd_box, text="Apply Selected Params", command=self._apply_selected_method_params).grid(row=1, column=3, sticky="w", pady=(10, 0))

        quick = ttk.Frame(cmd_box)
        quick.grid(row=2, column=0, columnspan=4, sticky="w", pady=(10, 6))
        # E (eisScan) and P (SeeedStatScan) only stream parseable ASCII when the
        # board's SeeedStat mode (S) and verbose (@) flags are both on; force them
        # here so a bare quick-command click can't fall back to raw binary output.
        quick_command_overrides = {"E": "S1;@1;E", "P": "S1;@1;P"}
        for label in ["?", "!", "C", "Z", "E", "P", "A", "W", "D"]:
            cmd = quick_command_overrides.get(label, label)
            ttk.Button(quick, text=label, command=lambda cmd=cmd: self._send_command(cmd), width=4).pack(side="left", padx=(0, 6))

        eis = ttk.Frame(cmd_box)
        eis.grid(row=3, column=0, columnspan=4, sticky="ew", pady=(8, 0))
        cmd_box.columnconfigure(1, weight=1)

        ttk.Label(eis, text="Freq Lo (Hz)").grid(row=0, column=0, sticky="w")
        ttk.Entry(eis, textvariable=self.freq_lo_var, width=10).grid(row=0, column=1, padx=(6, 12))
        ttk.Label(eis, text="Freq Hi (Hz)").grid(row=0, column=2, sticky="w")
        ttk.Entry(eis, textvariable=self.freq_hi_var, width=10).grid(row=0, column=3, padx=(6, 12))
        ttk.Label(eis, text="NFreq").grid(row=0, column=4, sticky="w")
        ttk.Entry(eis, textvariable=self.nfreq_var, width=8).grid(row=0, column=5, padx=(6, 12))
        ttk.Label(eis, text="Rcal").grid(row=0, column=6, sticky="w")
        ttk.Entry(eis, textvariable=self.rcal_var, width=10).grid(row=0, column=7, padx=(6, 12))

        ttk.Label(eis, text="Amp (mV)").grid(row=1, column=0, sticky="w", pady=(8, 0))
        ttk.Entry(eis, textvariable=self.amp_mv_var, width=10).grid(row=1, column=1, padx=(6, 12), pady=(8, 0))
        ttk.Label(eis, text="Bias (mV)").grid(row=1, column=2, sticky="w", pady=(8, 0))
        ttk.Entry(eis, textvariable=self.bias_mv_var, width=10).grid(row=1, column=3, padx=(6, 12), pady=(8, 0))
        ttk.Label(eis, text="Offset (mV)").grid(row=1, column=4, sticky="w", pady=(8, 0))
        ttk.Entry(eis, textvariable=self.offset_mv_var, width=10).grid(row=1, column=5, padx=(6, 12), pady=(8, 0))
        ttk.Label(eis, text="EIS Mode").grid(row=1, column=6, sticky="w", pady=(8, 0))
        ttk.Combobox(eis, textvariable=self.eis_mode_var, values=["0", "1"], width=8, state="readonly").grid(row=1, column=7, padx=(6, 12), pady=(8, 0))

        ttk.Checkbutton(eis, text="SeeedStat mode (S1)", variable=self.seeedstat_var).grid(row=1, column=8, sticky="w", pady=(8, 0))
        ttk.Button(eis, text="Run EIS", command=self._run_eis).grid(row=1, column=9, padx=(10, 0), pady=(8, 0))
        ttk.Button(eis, text="Run SeeedStat", command=self._run_seeedstat).grid(row=1, column=10, padx=(8, 0), pady=(8, 0))

        method_scroll_host = ttk.Frame(cmd_box)
        method_scroll_host.grid(row=4, column=0, columnspan=4, sticky="nsew", pady=(10, 0))
        method_scroll_host.columnconfigure(0, weight=1)

        method_canvas = tk.Canvas(method_scroll_host, height=260, bg="#101418", highlightthickness=0)
        method_canvas.grid(row=0, column=0, sticky="nsew")
        method_scrollbar = ttk.Scrollbar(method_scroll_host, orient="vertical", command=method_canvas.yview)
        method_scrollbar.grid(row=0, column=1, sticky="ns")
        method_canvas.configure(yscrollcommand=method_scrollbar.set)

        method_area = ttk.Frame(method_canvas)
        method_window = method_canvas.create_window((0, 0), window=method_area, anchor="nw")

        def _sync_method_scrollregion(_event=None):
            method_canvas.configure(scrollregion=method_canvas.bbox("all"))

        def _sync_method_width(event):
            method_canvas.itemconfigure(method_window, width=event.width)

        method_area.bind("<Configure>", _sync_method_scrollregion)
        method_canvas.bind("<Configure>", _sync_method_width)
        method_canvas.bind_all(
            "<MouseWheel>",
            lambda event: method_canvas.yview_scroll(int(-1 * (event.delta / 120)), "units")
            if method_canvas.winfo_containing(event.x_root, event.y_root) is not None
            else None,
        )

        for col in range(3):
            method_area.columnconfigure(col, weight=1)

        eis_panel = ttk.LabelFrame(method_area, text="EIS", padding=8)
        eis_panel.grid(row=0, column=0, sticky="nsew", padx=(0, 8), pady=(0, 8))
        ttk.Label(eis_panel, text="Lo/Hi/NFreq").grid(row=0, column=0, sticky="w")
        ttk.Entry(eis_panel, textvariable=self.freq_lo_var, width=9).grid(row=0, column=1, padx=(6, 4))
        ttk.Entry(eis_panel, textvariable=self.freq_hi_var, width=9).grid(row=0, column=2, padx=(0, 4))
        ttk.Entry(eis_panel, textvariable=self.nfreq_var, width=8).grid(row=0, column=3)
        ttk.Label(eis_panel, text="Rcal/Amp/Bias/Off").grid(row=1, column=0, sticky="w", pady=(8, 0))
        ttk.Entry(eis_panel, textvariable=self.rcal_var, width=9).grid(row=1, column=1, padx=(6, 4), pady=(8, 0))
        ttk.Entry(eis_panel, textvariable=self.amp_mv_var, width=9).grid(row=1, column=2, padx=(0, 4), pady=(8, 0))
        ttk.Entry(eis_panel, textvariable=self.bias_mv_var, width=9).grid(row=1, column=3, padx=(0, 4), pady=(8, 0))
        ttk.Entry(eis_panel, textvariable=self.offset_mv_var, width=9).grid(row=1, column=4, pady=(8, 0))

        cv_panel = ttk.LabelFrame(method_area, text="CV", padding=8)
        cv_panel.grid(row=0, column=1, sticky="nsew", padx=(0, 8), pady=(0, 8))
        ttk.Label(cv_panel, text="Start/Stop").grid(row=0, column=0, sticky="w")
        ttk.Entry(cv_panel, textvariable=self.cv_start_var, width=9).grid(row=0, column=1, padx=(6, 4))
        ttk.Entry(cv_panel, textvariable=self.cv_stop_var, width=9).grid(row=0, column=2, padx=(0, 4))
        ttk.Label(cv_panel, text="Step/Scan/Cycle").grid(row=1, column=0, sticky="w", pady=(8, 0))
        ttk.Entry(cv_panel, textvariable=self.cv_step_var, width=9).grid(row=1, column=1, padx=(6, 4), pady=(8, 0))
        ttk.Entry(cv_panel, textvariable=self.cv_scan_var, width=9).grid(row=1, column=2, padx=(0, 4), pady=(8, 0))
        ttk.Entry(cv_panel, textvariable=self.cv_cycle_var, width=9).grid(row=1, column=3, pady=(8, 0))

        ca_panel = ttk.LabelFrame(method_area, text="CA", padding=8)
        ca_panel.grid(row=0, column=2, sticky="nsew", pady=(0, 8))
        ttk.Label(ca_panel, text="Voltage/Duration/Rate").grid(row=0, column=0, sticky="w")
        ttk.Entry(ca_panel, textvariable=self.ca_voltage_var, width=9).grid(row=0, column=1, padx=(6, 4))
        ttk.Entry(ca_panel, textvariable=self.ca_duration_var, width=9).grid(row=0, column=2, padx=(0, 4))
        ttk.Entry(ca_panel, textvariable=self.ca_rate_var, width=9).grid(row=0, column=3)

        swv_panel = ttk.LabelFrame(method_area, text="SWV", padding=8)
        swv_panel.grid(row=1, column=0, sticky="nsew", padx=(0, 8), pady=(0, 8))
        ttk.Label(swv_panel, text="Start/End/Step").grid(row=0, column=0, sticky="w")
        ttk.Entry(swv_panel, textvariable=self.swv_start_var, width=9).grid(row=0, column=1, padx=(6, 4))
        ttk.Entry(swv_panel, textvariable=self.swv_end_var, width=9).grid(row=0, column=2, padx=(0, 4))
        ttk.Entry(swv_panel, textvariable=self.swv_step_var, width=9).grid(row=0, column=3)
        ttk.Label(swv_panel, text="Amp/Freq").grid(row=1, column=0, sticky="w", pady=(8, 0))
        ttk.Entry(swv_panel, textvariable=self.swv_amp_var, width=9).grid(row=1, column=1, padx=(6, 4), pady=(8, 0))
        ttk.Entry(swv_panel, textvariable=self.swv_freq_var, width=9).grid(row=1, column=2, padx=(0, 4), pady=(8, 0))

        dpv_panel = ttk.LabelFrame(method_area, text="DPV", padding=8)
        dpv_panel.grid(row=1, column=1, sticky="nsew", padx=(0, 8), pady=(0, 8))
        ttk.Label(dpv_panel, text="Start/End/Step").grid(row=0, column=0, sticky="w")
        ttk.Entry(dpv_panel, textvariable=self.dpv_start_var, width=9).grid(row=0, column=1, padx=(6, 4))
        ttk.Entry(dpv_panel, textvariable=self.dpv_end_var, width=9).grid(row=0, column=2, padx=(0, 4))
        ttk.Entry(dpv_panel, textvariable=self.dpv_step_var, width=9).grid(row=0, column=3)
        ttk.Label(dpv_panel, text="Amp").grid(row=1, column=0, sticky="w", pady=(8, 0))
        ttk.Entry(dpv_panel, textvariable=self.dpv_amp_var, width=9).grid(row=1, column=1, padx=(6, 4), pady=(8, 0))

        ocp_panel = ttk.LabelFrame(method_area, text="OCP / Run", padding=8)
        ocp_panel.grid(row=1, column=2, sticky="nsew", pady=(0, 8))
        ttk.Label(ocp_panel, text="Method").grid(row=0, column=0, sticky="w")
        ttk.Combobox(
            ocp_panel,
            textvariable=self.method_var,
            values=["EIS", "EIS SeeedStat", "CV", "SWV", "DPV", "CA", "OCP"],
            state="readonly",
            width=18,
        ).grid(row=0, column=1, padx=(6, 4))
        ttk.Button(ocp_panel, text="Run Selected", command=self._run_selected_method).grid(row=0, column=2, padx=(0, 4))
        ttk.Button(ocp_panel, text="Apply Params", command=self._apply_selected_method_params).grid(row=1, column=0, columnspan=3, sticky="ew", pady=(8, 0))

        body = ttk.Frame(content, padding=(12, 0, 12, 12))
        body.pack(fill="both", expand=True)
        body.columnconfigure(0, weight=2)
        body.columnconfigure(1, weight=3)
        body.rowconfigure(0, weight=1)

        left = ttk.LabelFrame(body, text="Log Mentah", padding=10)
        left.grid(row=0, column=0, sticky="nsew", padx=(0, 10))
        left.rowconfigure(0, weight=1)
        left.columnconfigure(0, weight=1)

        self.log_text = tk.Text(left, wrap="none", bg="#0B0F14", fg="#D9E2EC", insertbackground="#FFFFFF", relief="flat")
        self.log_text.grid(row=0, column=0, sticky="nsew")
        log_scroll = ttk.Scrollbar(left, command=self.log_text.yview)
        log_scroll.grid(row=0, column=1, sticky="ns")
        self.log_text.configure(yscrollcommand=log_scroll.set)

        right = ttk.Frame(body)
        right.grid(row=0, column=1, sticky="nsew")
        right.rowconfigure(1, weight=1)
        right.columnconfigure(0, weight=1)

        stats = ttk.LabelFrame(right, text="Ringkasan", padding=10)
        stats.grid(row=0, column=0, sticky="ew")
        self.summary_var = tk.StringVar(value="Belum ada data")
        ttk.Label(stats, textvariable=self.summary_var).pack(anchor="w")

        plot_frame = ttk.LabelFrame(right, text="Plot", padding=10)
        plot_frame.grid(row=1, column=0, sticky="nsew", pady=(10, 0))
        plot_frame.rowconfigure(0, weight=1)
        plot_frame.columnconfigure(0, weight=1)

        self.canvas = tk.Canvas(plot_frame, bg="#0B0F14", highlightthickness=0)
        self.canvas.grid(row=0, column=0, sticky="nsew")

        bottom = ttk.Frame(self, padding=(12, 0, 12, 12))
        bottom.pack(fill="x")
        ttk.Label(bottom, text="Filter mode").pack(side="left")
        self.filter_box = ttk.Combobox(
            bottom,
            textvariable=self.filter_var,
            values=["ALL", "OCP", "EIS", "CV", "CA", "SWV", "DPV", "EIS_RAW", "NYQUIST"],
            state="readonly",
            width=12,
        )
        self.filter_box.pack(side="left", padx=8)
        self.filter_box.bind("<<ComboboxSelected>>", lambda _event: self._redraw_plot())
        ttk.Button(bottom, text="Export CSV", command=self._export_csv).pack(side="left", padx=(8, 0))

        self.page_canvas = page_canvas

    def _refresh_ports(self):
        ports = []
        if serial is not None:
            ports = [port.device for port in serial.tools.list_ports.comports()]
        self.port_box["values"] = ports
        if ports and not self.port_var.get():
            self.port_var.set(ports[0])
        elif not ports:
            self.port_var.set("")
        self.status_var.set("Port diperbarui")

    def _connect(self):
        if serial is None:
            messagebox.showerror("pyserial tidak tersedia", "Install paket pyserial untuk memakai koneksi serial.")
            return

        if self.reader and self.reader.is_alive():
            return

        port_name = self.port_var.get().strip()
        if not port_name:
            messagebox.showwarning("Port kosong", "Pilih port serial terlebih dahulu.")
            return

        try:
            baudrate = int(self.baud_var.get().strip())
        except ValueError:
            messagebox.showwarning("Baud tidak valid", "Masukkan baud rate yang valid.")
            return

        self.stop_event.clear()
        self.reader = SerialReader(port_name, baudrate, self.queue, self.stop_event)
        self.reader.start()
        self.status_var.set(f"Mencoba koneksi ke {port_name} ...")

    def _disconnect(self):
        self.stop_event.set()
        self.status_var.set("Memutus koneksi ...")

    def _send_raw(self):
        command = self.raw_command_var.get().strip()
        if not command:
            return
        self._send_command(command)

    def _send_command(self, command):
        if self.source_var.get() == "DUMMY":
            self.status_var.set(f"Dummy mode: command tidak dikirim ({command})")
            self.log_text.insert(tk.END, f"[DUMMY] > {command}\n")
            self.log_text.see(tk.END)
            return

        if not self.reader or not self.reader.is_alive():
            messagebox.showwarning("Belum terhubung", "Hubungkan serial terlebih dahulu.")
            return
        try:
            self.reader.send_line(command)
            self.status_var.set(f"TX: {command}")
            self.log_text.insert(tk.END, f"> {command}\n")
            self.log_text.see(tk.END)
        except Exception as exc:
            messagebox.showerror("Gagal kirim command", str(exc))

    def _apply_eis_params(self):
        try:
            freq_lo = float(self.freq_lo_var.get())
            freq_hi = float(self.freq_hi_var.get())
            nfreq = int(float(self.nfreq_var.get()))
            rcal = float(self.rcal_var.get())
            amp = float(self.amp_mv_var.get())
            bias = float(self.bias_mv_var.get())
            offset = float(self.offset_mv_var.get())
            mode = int(float(self.eis_mode_var.get()))
        except ValueError:
            messagebox.showwarning("Parameter tidak valid", "Periksa nilai parameter EIS.")
            return

        sequence = [
            "@1",  # verbose=1: firmware only prints ASCII EIS data when this bit is set
            f"W{freq_lo}",
            f"X{freq_hi}",
            f"y{nfreq}",
            f"c{rcal}",
            f"Y{amp}",
            f"B{bias}",
            f"V{offset}",
            f"m{mode}",
            f"S{1 if self.seeedstat_var.get() else 0}",
        ]
        self._send_command(";".join(sequence))

    def _run_eis(self):
        if self.source_var.get() == "DUMMY":
            self._generate_dummy_for_method("EIS")
            return
        self._apply_eis_params()
        self._send_command("E")

    def _run_seeedstat(self):
        if self.source_var.get() == "DUMMY":
            self._generate_dummy_for_method("EIS SeeedStat")
            return
        self.seeedstat_var.set(True)
        self._apply_eis_params()
        self._send_command("P")

    def _apply_selected_method_params(self):
        if self.source_var.get() == "DUMMY":
            self.status_var.set("Dummy mode: parameter disimpan di UI, tidak dikirim ke board")
            return

        method = self.method_var.get().strip()
        if not method:
            return

        if method in ("EIS", "EIS SeeedStat"):
            self._apply_eis_params()
            return

        try:
            if method == "CV":
                cmd = f"D {float(self.cv_start_var.get())},{float(self.cv_stop_var.get())},{float(self.cv_step_var.get())},{float(self.cv_scan_var.get())},{int(float(self.cv_cycle_var.get()))}"
                self._send_command(cmd)
            elif method == "CA":
                cmd = ";".join([
                    f"1{float(self.ca_voltage_var.get())}",
                    f"2{float(self.ca_duration_var.get())}",
                    f"3{float(self.ca_rate_var.get())}",
                ])
                self._send_command(cmd)
            elif method == "SWV":
                cmd = ";".join([
                    f"4{float(self.swv_start_var.get())}",
                    f"5{float(self.swv_end_var.get())}",
                    f"6{float(self.swv_step_var.get())}",
                    f"7{float(self.swv_amp_var.get())}",
                    f"8{float(self.swv_freq_var.get())}",
                ])
                self._send_command(cmd)
            elif method == "DPV":
                cmd = ";".join([
                    f"9{float(self.dpv_start_var.get())}",
                    f"0{float(self.dpv_end_var.get())}",
                    f"!{float(self.dpv_step_var.get())}",
                    f"#{float(self.dpv_amp_var.get())}",
                ])
                self._send_command(cmd)
        except ValueError:
            messagebox.showwarning("Parameter tidak valid", f"Periksa parameter untuk metode {method}.")

    def _run_selected_method(self):
        method = self.method_var.get().strip()
        if not method:
            return

        if self.source_var.get() == "DUMMY":
            self._generate_dummy_for_method(method)
            return

        mapping = {
            "CV": "M",
            "SWV": "W",
            "DPV": "D",
            "CA": "A",
            "OCP": "O",
        }

        if method == "EIS":
            self._run_eis()
            return
        if method == "EIS SeeedStat":
            self._run_seeedstat()
            return

        self._apply_selected_method_params()

        command = mapping.get(method)
        if command is None:
            return

        self._send_command(command)
        if method in ("CV", "SWV", "DPV", "CA", "OCP"):
            self.filter_var.set(method)
            self._redraw_plot()

    def _load_log(self):
        path = filedialog.askopenfilename(filetypes=[("Text files", "*.txt;*.log;*.csv"), ("All files", "*.*")])
        if not path:
            return
        try:
            with open(path, "r", encoding="utf-8", errors="replace") as handle:
                for raw_line in handle:
                    line = raw_line.strip()
                    if line:
                        self._handle_line(line)
            self.status_var.set(f"Log dimuat: {path}")
        except Exception as exc:
            messagebox.showerror("Gagal memuat log", str(exc))

    def _load_cv_file(self):
        path = filedialog.askopenfilename(filetypes=[("Data files", "*.csv;*.txt;*.log"), ("All files", "*.*")])
        if not path:
            return

        loaded = 0
        try:
            with open(path, "r", encoding="utf-8", errors="replace") as handle:
                for raw_line in handle:
                    line = raw_line.strip()
                    if not line:
                        continue

                    parsed = self._parse_cv_pair(line)
                    if parsed is None:
                        continue

                    x_val, y_val = parsed
                    self.data_points.append(("CV", x_val, y_val))
                    self.mode_points["CV"].append((x_val, y_val))
                    loaded += 1

            if loaded == 0:
                messagebox.showinfo("Tidak ada data CV", "File terbaca, tapi pasangan angka CV tidak ditemukan.")
                return

            self.filter_var.set("CV")
            self._update_summary()
            self._redraw_plot()
            self.status_var.set(f"CV loaded: {loaded} titik dari {path}")
        except Exception as exc:
            messagebox.showerror("Gagal memuat CV", str(exc))

    def _load_dummy_cv(self):
        self._generate_dummy_for_method("CV")

    def _load_dummy_all(self):
        methods = ["EIS", "CV", "CA", "SWV", "DPV", "OCP"]
        for method in methods:
            self._generate_dummy_for_method(method)
        self.filter_var.set("NYQUIST")
        self._update_summary()
        self._redraw_plot()
        self.status_var.set("Dummy semua metode berhasil dimuat (tampilan default: NYQUIST)")

    def _clear(self):
        self.log_text.delete("1.0", tk.END)
        self.data_points.clear()
        for points in self.mode_points.values():
            points.clear()
        self.summary_var.set("Belum ada data")
        self.canvas.delete("all")
        self.status_var.set("Dibersihkan")

    def _export_csv(self):
        if not self.data_points:
            messagebox.showinfo("Belum ada data", "Data plot masih kosong.")
            return
        path = filedialog.asksaveasfilename(
            defaultextension=".csv",
            filetypes=[("CSV", "*.csv"), ("All files", "*.*")],
            title="Simpan hasil pengukuran",
        )
        if not path:
            return
        try:
            with open(path, "w", newline="", encoding="utf-8") as handle:
                writer = csv.writer(handle)
                writer.writerow(["mode", "x", "y"])
                writer.writerows(self.data_points)
            self.status_var.set(f"CSV tersimpan: {path}")
        except Exception as exc:
            messagebox.showerror("Gagal simpan CSV", str(exc))

    def _poll_queue(self):
        try:
            while True:
                kind, payload = self.queue.get_nowait()
                if kind == "line":
                    self._handle_line(payload)
                elif kind == "status":
                    self.status_var.set(payload)
        except queue.Empty:
            pass
        self.after(100, self._poll_queue)

    def _handle_line(self, line):
        self.log_text.insert(tk.END, line + "\n")
        self.log_text.see(tk.END)
        parsed = self._parse_measurement(line)
        if parsed is not None:
            mode, x_val, y_val = parsed
            self.data_points.append((mode, x_val, y_val))
            self.mode_points.setdefault(mode, []).append((x_val, y_val))
            self._update_summary()
            self._redraw_plot()

    def _parse_measurement(self, line):
        # Generic tagged mode format: MODE,x,y
        upper = line.upper()
        if upper.startswith(("OCP,", "EIS,", "CV,", "CA,", "SWV,", "DPV,")):
            parts = [part.strip() for part in line.split(",")]
            if len(parts) < 3:
                return None
            mode = parts[0].upper()
            try:
                x_val = float(parts[1])
                y_val = float(parts[2])
            except ValueError:
                return None
            return mode, x_val, y_val

        if upper.startswith("CV:"):
            parsed_cv = self._parse_cv_pair(line)
            if parsed_cv is not None:
                x_val, y_val = parsed_cv
                return "CV", x_val, y_val

        # SeeedStat EIS raw output: index=real,imag,
        match_raw = re.match(r"^\s*\d+\s*=\s*([-+]?\d*\.?\d+(?:[eE][-+]?\d+)?)\s*,\s*([-+]?\d*\.?\d+(?:[eE][-+]?\d+)?)\s*,?\s*$", line)
        if match_raw:
            real = float(match_raw.group(1))
            imag = float(match_raw.group(2))
            return "EIS_RAW", real, imag

        # SeeedStat Nyquist output: x,y,
        match_nyq = re.match(r"^\s*([-+]?\d*\.?\d+(?:[eE][-+]?\d+)?)\s*,\s*([-+]?\d*\.?\d+(?:[eE][-+]?\d+)?)\s*,?\s*$", line)
        if match_nyq:
            x_val = float(match_nyq.group(1))
            y_val = float(match_nyq.group(2))
            return "NYQUIST", x_val, y_val

        return None

    def _parse_cv_pair(self, line):
        cleaned = line.strip()
        cleaned_upper = cleaned.upper()
        if cleaned_upper.startswith("CV,"):
            cleaned = cleaned[3:]
        elif cleaned_upper.startswith("CV:"):
            cleaned = cleaned[3:]

        parts = re.split(r"[,;\t ]+", cleaned)
        parts = [part for part in parts if part]
        if len(parts) < 2:
            return None

        try:
            x_val = float(parts[0])
            y_val = float(parts[1])
        except ValueError:
            return None

        return x_val, y_val

    def _update_summary(self):
        total = len(self.data_points)
        counts = {mode: len(points) for mode, points in self.mode_points.items()}
        summary = [f"Total data: {total}"]
        summary.extend(f"{mode}: {count}" for mode, count in counts.items() if count)
        self.summary_var.set(" | ".join(summary))

    def _redraw_plot(self):
        self.canvas.delete("all")
        width = max(self.canvas.winfo_width(), 1)
        height = max(self.canvas.winfo_height(), 1)
        margin = 40

        self.canvas.create_rectangle(margin, margin, width - margin, height - margin, outline="#364152")
        self.canvas.create_text(margin + 6, margin + 6, anchor="nw", fill="#B8C4D0", text="y")
        self.canvas.create_text(width - margin - 12, height - margin + 8, anchor="ne", fill="#B8C4D0", text="x")

        mode_filter = self.filter_var.get()
        series = []
        if mode_filter == "ALL":
            for mode, points in self.mode_points.items():
                series.extend((mode, x, y) for x, y in points)
        else:
            series.extend((mode_filter, x, y) for x, y in self.mode_points.get(mode_filter, []))

        if not series:
            self.canvas.create_text(width / 2, height / 2, text="Belum ada data untuk diplot", fill="#94A3B8")
            return

        xs = [item[1] for item in series]
        ys = [item[2] for item in series]
        min_x, max_x = min(xs), max(xs)
        min_y, max_y = min(ys), max(ys)
        if min_x == max_x:
            max_x = min_x + 1.0
        if min_y == max_y:
            max_y = min_y + 1.0

        palette = {
            "OCP": "#22C55E",
            "EIS": "#38BDF8",
            "CV": "#FBBF24",
            "CA": "#F59E0B",
            "SWV": "#E879F9",
            "DPV": "#F87171",
            "EIS_RAW": "#7DD3FC",
            "NYQUIST": "#34D399",
        }

        def map_x(value):
            return margin + ((value - min_x) / (max_x - min_x)) * (width - 2 * margin)

        def map_y(value):
            return height - margin - ((value - min_y) / (max_y - min_y)) * (height - 2 * margin)

        last_by_mode = {}
        for mode, x_val, y_val in series:
            px = map_x(x_val)
            py = map_y(y_val)
            color = palette.get(mode, "#E2E8F0")
            self.canvas.create_oval(px - 3, py - 3, px + 3, py + 3, fill=color, outline=color)
            if mode in last_by_mode:
                last_x, last_y = last_by_mode[mode]
                self.canvas.create_line(last_x, last_y, px, py, fill=color, width=2)
            last_by_mode[mode] = (px, py)

    def _on_source_changed(self):
        source = self.source_var.get()
        if source == "DUMMY":
            self.status_var.set("Mode Dummy aktif: Run Selected akan menghasilkan data simulasi")
        else:
            self.status_var.set("Mode Board aktif: command akan dikirim ke serial")

    def _replace_mode_series(self, mode, points):
        self.mode_points.setdefault(mode, [])
        self.mode_points[mode].clear()
        self.data_points = [item for item in self.data_points if item[0] != mode]

        for x_val, y_val in points:
            self.mode_points[mode].append((x_val, y_val))
            self.data_points.append((mode, x_val, y_val))
            self.log_text.insert(tk.END, f"{mode},{x_val:.6f},{y_val:.6f}\n")

        self.log_text.see(tk.END)

    @staticmethod
    def _coerce_to_volt(value):
        return value / 1000.0 if abs(value) > 5.0 else value

    def _generate_dummy_for_method(self, method):
        method = method.strip()
        points = []
        target_filter = "CV"

        if method == "CV":
            forward = []
            reverse = []
            start_v = float(self.cv_start_var.get())
            end_v = float(self.cv_stop_var.get())
            n_points = 140
            for i in range(n_points):
                v = start_v + (end_v - start_v) * (i / (n_points - 1))
                i_base = 0.08 * v
                i_peak_ox = 0.9 * math.exp(-((v - 0.24) ** 2) / 0.015)
                current = i_base + i_peak_ox + random.uniform(-0.01, 0.01)
                forward.append((v, current))
            for i in range(n_points):
                v = end_v - (end_v - start_v) * (i / (n_points - 1))
                i_base = 0.07 * v
                i_peak_red = -0.72 * math.exp(-((v - 0.05) ** 2) / 0.018)
                current = i_base + i_peak_red + random.uniform(-0.01, 0.01)
                reverse.append((v, current))
            points = forward + reverse
            target_filter = "CV"
            self._replace_mode_series("CV", points)

        elif method in ("EIS", "EIS SeeedStat"):
            f_lo = max(0.1, float(self.freq_lo_var.get()))
            f_hi = max(f_lo + 0.1, float(self.freq_hi_var.get()))
            nfreq = max(8, int(float(self.nfreq_var.get())))
            nyq_points = []
            raw_points = []
            rs = 18.0
            rct = 160.0
            sigma = 20.0
            for i in range(nfreq):
                frac = i / max(nfreq - 1, 1)
                freq = f_hi * ((f_lo / f_hi) ** frac)
                theta = frac * math.pi * 0.92
                z_real = rs + 0.5 * rct * (1.0 - math.cos(theta))
                z_imag = -0.5 * rct * math.sin(theta)
                # Add a small Warburg-like tail at lower frequencies.
                if frac > 0.68:
                    w = frac - 0.68
                    z_real += sigma * w
                    z_imag -= sigma * w
                z_real += random.uniform(-1.0, 1.0)
                z_imag += random.uniform(-1.0, 1.0)
                nyq_points.append((z_real, z_imag))
                raw_points.append((freq, math.hypot(z_real, z_imag)))
            self._replace_mode_series("NYQUIST", nyq_points)
            self._replace_mode_series("EIS", raw_points)
            target_filter = "NYQUIST"

        elif method == "CA":
            duration = max(1.0, float(self.ca_duration_var.get()))
            rate = max(2.0, float(self.ca_rate_var.get()))
            total = min(2000, max(20, int(duration * rate)))
            step_v = self._coerce_to_volt(float(self.ca_voltage_var.get()))
            sign = 1.0 if step_v >= 0 else -1.0
            abs_step = abs(step_v)
            tau_dl = max(0.12 * duration, 0.15)
            tau_diff = max(0.28 * duration, 0.5)
            i0 = (1.4 + 6.0 * abs_step) * sign
            i_ss = (0.08 + 0.45 * abs_step) * sign
            for i in range(total):
                t = i / rate
                i_cottrell = i0 / math.sqrt(1.0 + t / tau_diff)
                i_dl = 0.45 * i0 * math.exp(-t / tau_dl)
                noise = random.uniform(-0.015, 0.015) * max(0.35, abs(i0))
                current = i_ss + i_cottrell + i_dl + noise
                points.append((t, current))
            self._replace_mode_series("CA", points)
            target_filter = "CA"

        elif method == "SWV":
            start_v_raw = float(self.swv_start_var.get())
            end_v_raw = float(self.swv_end_var.get())
            scale = 1000.0 if abs(end_v_raw - start_v_raw) > 5.0 else 1.0
            start_v = start_v_raw / scale
            end_v = end_v_raw / scale
            step_v = max(float(self.swv_step_var.get()) / scale, 1e-5)

            total = min(2500, max(10, int(abs(end_v - start_v) / step_v) + 1))
            direction = 1 if end_v >= start_v else -1

            slope = 0.6
            for i in range(total):
                v = start_v + direction * step_v * i
                current = slope * (v - start_v)
                points.append((v, current))
            self._replace_mode_series("SWV", points)
            target_filter = "SWV"

        elif method == "DPV":
            start_v_raw = float(self.dpv_start_var.get())
            end_v_raw = float(self.dpv_end_var.get())
            scale = 1000.0 if abs(end_v_raw - start_v_raw) > 5.0 else 1.0
            start_v = start_v_raw / scale
            end_v = end_v_raw / scale
            step_v = max(float(self.dpv_step_var.get()) / scale, 1e-6)
            pulse_amp = max(0.001, float(self.dpv_amp_var.get()) / scale)
            total = min(2500, max(10, int(abs(end_v - start_v) / step_v) + 1))
            direction = 1 if end_v >= start_v else -1
            e_span = max(abs(end_v - start_v), 0.05)
            e_peak = start_v + 0.62 * (end_v - start_v)
            width = max(0.045 * e_span, 0.01)
            peak_scale = 0.18 + 9.5 * pulse_amp
            for i in range(total):
                v = start_v + direction * step_v * i
                peak = peak_scale * math.exp(-((v - e_peak) ** 2) / (2.0 * width * width))
                shoulder = 0.18 * peak_scale * math.exp(-((v - (e_peak + 0.07 * direction)) ** 2) / (2.0 * (1.7 * width) ** 2))
                baseline = 0.04 * direction * (v - start_v)
                noise = random.uniform(-0.018, 0.018) * peak_scale
                current = baseline + peak + shoulder + noise
                points.append((v, current))
            self._replace_mode_series("DPV", points)
            target_filter = "DPV"

        elif method == "OCP":
            total = 240
            dt = 0.5
            t_end = (total - 1) * dt
            e_start = 0.18 + random.uniform(-0.02, 0.02)
            e_end = e_start + random.uniform(0.02, 0.05)
            drift = random.uniform(-1.2e-5, 1.2e-5)
            rw = 0.0
            for i in range(total):
                t = i * dt
                sqrt_frac = math.sqrt(max(t, 0.0) + 1e-9) / math.sqrt(t_end + 1e-9)
                rw = max(-0.0025, min(0.0025, rw + random.uniform(-4.0e-5, 4.0e-5)))
                noise = random.uniform(-4.0e-4, 4.0e-4)
                voltage = e_start + (e_end - e_start) * sqrt_frac + drift * t + rw + noise
                points.append((t, voltage))
            self._replace_mode_series("OCP", points)
            target_filter = "OCP"

        else:
            messagebox.showinfo("Dummy", f"Metode {method} belum didukung untuk dummy.")
            return

        self.filter_var.set(target_filter)
        self._update_summary()
        self._redraw_plot()
        self.status_var.set(f"Dummy data dibuat untuk {method}")

    def run(self):
        self.bind("<Configure>", lambda _event: self._redraw_plot())
        self.mainloop()


if __name__ == "__main__":
    app = TestUI()
    app.run()
