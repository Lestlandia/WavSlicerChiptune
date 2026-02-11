import tkinter as tk
from tkinter import filedialog, messagebox, ttk
import subprocess
import threading
import queue
import os
import sys
import tempfile
import shutil


def get_base_dir():
    """Return the directory where bundled resources live (PyInstaller or normal)."""
    if getattr(sys, '_MEIPASS', None):
        return sys._MEIPASS
    return os.path.dirname(os.path.abspath(__file__))


class SlicerGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("WavSlicer Chiptune")
        self.root.geometry("700x550")
        self.root.resizable(False, False)

        # Icon
        try:
            icon_path = os.path.join(get_base_dir(), "waveform-cut-icon.ico")
            self.root.iconbitmap(icon_path)
        except Exception:
            pass

        # Thread-safe callback queue (Python 3.13+ forbids all tkinter calls from worker threads)
        self._cb_queue = queue.Queue()
        self._poll_callbacks()

        # Style
        self.style = ttk.Style()
        self.style.configure("TLabel", font=("Helvetica", 10))
        self.style.configure("TButton", font=("Helvetica", 10))

        # Slicer variables
        self.file_path = tk.StringVar()
        self.bpm = tk.StringVar(value="120")
        self.rpb = tk.StringVar(value="4")
        self.pattern_rows = tk.StringVar(value="64")
        self.naming_mode = tk.StringVar(value="DEC")
        self.output_dir = tk.StringVar()
        self.slice_prefix = tk.StringVar()

        # Fur generator variables
        self.fur_bpm = tk.StringVar(value="120")
        self.fur_rpb = tk.StringVar(value="4")
        self.fur_pattern_rows = tk.StringVar(value="64")
        self.fur_output_path = tk.StringVar()
        self.fur_files = []  # list of full paths

        self.create_widgets()

    def _schedule(self, func, *args):
        """Thread-safe way to schedule a callback on the main thread."""
        self._cb_queue.put((func, args))

    def _poll_callbacks(self):
        """Main-thread timer that drains the callback queue every 50ms."""
        while True:
            try:
                func, args = self._cb_queue.get_nowait()
                func(*args)
            except queue.Empty:
                break
        self.root.after(50, self._poll_callbacks)

    def create_widgets(self):
        self.notebook = ttk.Notebook(self.root)
        self.notebook.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)

        # Tab 1: Audio Slicer
        slicer_tab = ttk.Frame(self.notebook, padding="20")
        self.notebook.add(slicer_tab, text="Audio Slicer")
        self.create_slicer_tab(slicer_tab)

        # Tab 2: Fur Generator
        fur_tab = ttk.Frame(self.notebook, padding="20")
        self.notebook.add(fur_tab, text="Fur Generator")
        self.create_fur_tab(fur_tab)

    # ── Slicer Tab ──────────────────────────────────────────────

    def create_slicer_tab(self, parent):
        # File Selection
        ttk.Label(parent, text="Audio File:").grid(row=0, column=0, sticky=tk.W, pady=5)
        ttk.Entry(parent, textvariable=self.file_path, width=50).grid(row=1, column=0, columnspan=2, sticky=tk.W)
        ttk.Button(parent, text="Browse", command=self.browse_file).grid(row=1, column=2, padx=5)

        # Output Settings
        output_frame = ttk.LabelFrame(parent, text="Output Settings", padding="10")
        output_frame.grid(row=2, column=0, columnspan=3, sticky=tk.EW, pady=10)

        ttk.Label(output_frame, text="Output Folder Name:").grid(row=0, column=0, sticky=tk.W)
        ttk.Entry(output_frame, textvariable=self.output_dir, width=25).grid(row=0, column=1, sticky=tk.W, padx=5)

        ttk.Label(output_frame, text="Slice Prefix (empty for 00.wav):").grid(row=1, column=0, sticky=tk.W)
        ttk.Entry(output_frame, textvariable=self.slice_prefix, width=25).grid(row=1, column=1, sticky=tk.W, padx=5)

        # Parameters
        params_frame = ttk.LabelFrame(parent, text="Parameters", padding="10")
        params_frame.grid(row=3, column=0, columnspan=3, sticky=tk.EW, pady=10)

        ttk.Label(params_frame, text="BPM:").grid(row=0, column=0, sticky=tk.W)
        ttk.Entry(params_frame, textvariable=self.bpm, width=10).grid(row=0, column=1, sticky=tk.W, padx=5, pady=5)

        ttk.Label(params_frame, text="Rows per Beat:").grid(row=0, column=2, sticky=tk.W, padx=10)
        ttk.Entry(params_frame, textvariable=self.rpb, width=10).grid(row=0, column=3, sticky=tk.W, padx=5, pady=5)

        ttk.Label(params_frame, text="Pattern Rows:").grid(row=1, column=0, sticky=tk.W)
        ttk.Entry(params_frame, textvariable=self.pattern_rows, width=10).grid(row=1, column=1, sticky=tk.W, padx=5, pady=5)

        ttk.Label(params_frame, text="Naming Mode:").grid(row=1, column=2, sticky=tk.W, padx=10)
        ttk.Combobox(params_frame, textvariable=self.naming_mode, values=["DEC", "HEX"], width=8, state="readonly").grid(row=1, column=3, sticky=tk.W, padx=5, pady=5)

        # Progress
        self.progress_label = ttk.Label(parent, text="Ready")
        self.progress_label.grid(row=4, column=0, columnspan=3, pady=(10, 0))

        self.progress_bar = ttk.Progressbar(parent, orient=tk.HORIZONTAL, length=540, mode='determinate')
        self.progress_bar.grid(row=5, column=0, columnspan=3, pady=5)

        # Slice Button
        self.slice_button = ttk.Button(parent, text="Start Slicing", command=self.start_slicing)
        self.slice_button.grid(row=6, column=0, columnspan=3, pady=20)

    def browse_file(self):
        filename = filedialog.askopenfilename(filetypes=[("Audio Files", "*.wav *.mp3 *.flac *.ogg"), ("All Files", "*.*")])
        if filename:
            self.file_path.set(filename)
            # Auto-populate output fields
            base_name = os.path.splitext(os.path.basename(filename))[0]
            self.output_dir.set(base_name)
            self.slice_prefix.set(base_name)

    def start_slicing(self):
        if not self.file_path.get():
            messagebox.showerror("Error", "Please select an audio file.")
            return

        if not self.output_dir.get():
             messagebox.showerror("Error", "Please enter an output folder name.")
             return

        # Disable button during processing
        self.slice_button.config(state=tk.DISABLED)
        self.progress_bar["value"] = 0
        self.progress_label.config(text="Slicing...")

        # Capture StringVar values on main thread (not safe from worker in Python 3.13+)
        args = {
            'file_path': self.file_path.get(),
            'bpm': self.bpm.get(),
            'rpb': self.rpb.get(),
            'pattern_rows': self.pattern_rows.get(),
            'naming_mode': self.naming_mode.get(),
            'output_dir': self.output_dir.get(),
            'slice_prefix': self.slice_prefix.get(),
        }

        # Run in a separate thread to keep UI responsive
        threading.Thread(target=self.run_slicer, args=(args,), daemon=True).start()

    def run_slicer(self, args):
        try:
            # Check if slicer executable exists (bundled or alongside)
            base_dir = get_base_dir()
            slicer_name = "slicer.exe" if os.name == 'nt' else "slicer"
            executable = os.path.join(base_dir, slicer_name)

            if not os.path.exists(executable):
                self._schedule(lambda: messagebox.showerror("Error", f"Executable '{executable}' not found. Please compile it first."))
                self._schedule(lambda: self.slice_button.config(state=tk.NORMAL))
                return

            cmd = [
                executable,
                args['file_path'],
                args['bpm'],
                args['rpb'],
                args['pattern_rows'],
                args['naming_mode'],
                args['output_dir'],
                args['slice_prefix'],
            ]

            process = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                bufsize=1,
                universal_newlines=True
            )

            total_slices = 0
            while True:
                line = process.stdout.readline()
                if not line:
                    break

                print(line.strip()) # Debug info to console

                if "Total slices:" in line:
                    try:
                        total_slices = int(line.split(":")[-1].strip())
                    except ValueError:
                        pass

                if "Processing slice" in line:
                    try:
                        # Format: "Processing slice X/Y: filename"
                        parts = line.split(" ")[2].split("/")
                        current = int(parts[0])
                        total = int(parts[1].rstrip(":"))
                        progress = (current / total) * 100
                        self._schedule(self.update_progress, progress, f"Processing slice {current}/{total}...")
                    except (IndexError, ValueError):
                        pass

            process.wait()

            if process.returncode == 0:
                self._schedule(self.finish_slicing, True)
            else:
                stderr = process.stderr.read()
                self._schedule(self.finish_slicing, False, stderr)

        except Exception as e:
            self._schedule(self.finish_slicing, False, str(e))

    def update_progress(self, value, text):
        self.progress_bar["value"] = value
        self.progress_label.config(text=text)

    def finish_slicing(self, success, error_msg=""):
        self.slice_button.config(state=tk.NORMAL)
        if success:
            self.progress_bar["value"] = 100
            self.progress_label.config(text="Finished successfully!")
            messagebox.showinfo("Success", "All slices processed successfully.")
        else:
            self.progress_label.config(text="Error occurred.")
            messagebox.showerror("Error", f"An error occurred:\n{error_msg}")

    # ── Fur Generator Tab ───────────────────────────────────────

    def create_fur_tab(self, parent):
        # File list section
        list_frame = ttk.LabelFrame(parent, text="WAV Files (in order)", padding="5")
        list_frame.grid(row=0, column=0, columnspan=2, sticky=tk.NSEW, pady=(0, 10))
        parent.rowconfigure(0, weight=1)
        parent.columnconfigure(0, weight=1)

        # Listbox with scrollbar
        lb_frame = ttk.Frame(list_frame)
        lb_frame.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        scrollbar = ttk.Scrollbar(lb_frame, orient=tk.VERTICAL)
        self.fur_listbox = tk.Listbox(lb_frame, height=10, selectmode=tk.EXTENDED,
                                      yscrollcommand=scrollbar.set)
        scrollbar.config(command=self.fur_listbox.yview)
        self.fur_listbox.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)

        # Buttons beside the listbox
        btn_frame = ttk.Frame(list_frame)
        btn_frame.pack(side=tk.RIGHT, fill=tk.Y, padx=(5, 0))

        ttk.Button(btn_frame, text="Add Files", command=self.fur_add_files).pack(fill=tk.X, pady=2)
        ttk.Button(btn_frame, text="Remove", command=self.fur_remove_files).pack(fill=tk.X, pady=2)
        ttk.Button(btn_frame, text="Move Up", command=self.fur_move_up).pack(fill=tk.X, pady=2)
        ttk.Button(btn_frame, text="Move Down", command=self.fur_move_down).pack(fill=tk.X, pady=2)
        ttk.Button(btn_frame, text="Clear All", command=self.fur_clear_files).pack(fill=tk.X, pady=2)

        # Parameters
        params_frame = ttk.LabelFrame(parent, text="Parameters", padding="10")
        params_frame.grid(row=1, column=0, columnspan=2, sticky=tk.EW, pady=(0, 10))

        ttk.Label(params_frame, text="BPM:").grid(row=0, column=0, sticky=tk.W)
        ttk.Entry(params_frame, textvariable=self.fur_bpm, width=10).grid(row=0, column=1, sticky=tk.W, padx=5, pady=3)

        ttk.Label(params_frame, text="Rows per Beat:").grid(row=0, column=2, sticky=tk.W, padx=10)
        ttk.Entry(params_frame, textvariable=self.fur_rpb, width=10).grid(row=0, column=3, sticky=tk.W, padx=5, pady=3)

        ttk.Label(params_frame, text="Pattern Rows:").grid(row=1, column=0, sticky=tk.W)
        ttk.Entry(params_frame, textvariable=self.fur_pattern_rows, width=10).grid(row=1, column=1, sticky=tk.W, padx=5, pady=3)


        # Output file
        out_frame = ttk.LabelFrame(parent, text="Output", padding="10")
        out_frame.grid(row=2, column=0, columnspan=2, sticky=tk.EW, pady=(0, 10))

        ttk.Entry(out_frame, textvariable=self.fur_output_path, width=55).pack(side=tk.LEFT, fill=tk.X, expand=True)
        ttk.Button(out_frame, text="Save As...", command=self.fur_browse_output).pack(side=tk.RIGHT, padx=(5, 0))

        # Progress + Generate
        bottom_frame = ttk.Frame(parent)
        bottom_frame.grid(row=3, column=0, columnspan=2, sticky=tk.EW)

        self.fur_progress_label = ttk.Label(bottom_frame, text="Ready")
        self.fur_progress_label.pack(pady=(0, 2))

        self.fur_progress_bar = ttk.Progressbar(bottom_frame, orient=tk.HORIZONTAL, length=540, mode='determinate')
        self.fur_progress_bar.pack(pady=(0, 5))

        self.fur_generate_button = ttk.Button(bottom_frame, text="Generate .fur", command=self.fur_start_generate)
        self.fur_generate_button.pack(pady=(5, 0))

    # ── Fur Generator: file list management ─────────────────────

    def fur_add_files(self):
        files = filedialog.askopenfilenames(filetypes=[("WAV Files", "*.wav"), ("All Files", "*.*")])
        if files:
            for f in files:
                if len(self.fur_files) >= 120:
                    messagebox.showwarning("Limit", "Maximum 120 samples reached.")
                    break
                self.fur_files.append(f)
                self.fur_listbox.insert(tk.END, os.path.basename(f))

    def fur_remove_files(self):
        sel = list(self.fur_listbox.curselection())
        if not sel:
            return
        for i in reversed(sel):
            self.fur_listbox.delete(i)
            del self.fur_files[i]

    def fur_move_up(self):
        sel = list(self.fur_listbox.curselection())
        if not sel or sel[0] == 0:
            return
        for i in sel:
            # Swap in data
            self.fur_files[i - 1], self.fur_files[i] = self.fur_files[i], self.fur_files[i - 1]
            # Swap in listbox
            text = self.fur_listbox.get(i)
            self.fur_listbox.delete(i)
            self.fur_listbox.insert(i - 1, text)
            self.fur_listbox.selection_set(i - 1)

    def fur_move_down(self):
        sel = list(self.fur_listbox.curselection())
        if not sel or sel[-1] == self.fur_listbox.size() - 1:
            return
        for i in reversed(sel):
            # Swap in data
            self.fur_files[i + 1], self.fur_files[i] = self.fur_files[i], self.fur_files[i + 1]
            # Swap in listbox
            text = self.fur_listbox.get(i)
            self.fur_listbox.delete(i)
            self.fur_listbox.insert(i + 1, text)
            self.fur_listbox.selection_set(i + 1)

    def fur_clear_files(self):
        self.fur_listbox.delete(0, tk.END)
        self.fur_files.clear()

    def fur_browse_output(self):
        path = filedialog.asksaveasfilename(
            defaultextension=".fur",
            filetypes=[("Furnace Files", "*.fur"), ("All Files", "*.*")]
        )
        if path:
            self.fur_output_path.set(path)

    # ── Fur Generator: generation ───────────────────────────────

    def fur_start_generate(self):
        if not self.fur_files:
            messagebox.showerror("Error", "Please add at least one WAV file.")
            return
        if not self.fur_output_path.get():
            messagebox.showerror("Error", "Please choose an output file path.")
            return

        # Capture StringVar values on main thread (not safe from worker in Python 3.13+)
        args = {
            'bpm': self.fur_bpm.get(),
            'rpb': self.fur_rpb.get(),
            'pattern_rows': self.fur_pattern_rows.get(),
            'output_path': self.fur_output_path.get(),
            'files': list(self.fur_files),
        }

        self.fur_generate_button.config(state=tk.DISABLED)
        self.fur_progress_bar["value"] = 0
        self.fur_progress_label.config(text="Generating...")

        threading.Thread(target=self.fur_run_generate, args=(args,), daemon=True).start()

    def fur_run_generate(self, args):
        tmp_dir = None
        try:
            # Find fur_gen executable
            base_dir = get_base_dir()
            exe_name = "fur_gen.exe" if os.name == 'nt' else "fur_gen"
            executable = os.path.join(base_dir, exe_name)

            if not os.path.exists(executable):
                self._schedule(lambda: messagebox.showerror("Error", f"Executable '{executable}' not found. Please compile it first."))
                self._schedule(lambda: self.fur_generate_button.config(state=tk.NORMAL))
                return

            # Copy WAVs to temp dir with sequential 3-digit names
            tmp_dir = tempfile.mkdtemp(prefix="wavslicerfur_")
            for i, src in enumerate(args['files']):
                dst = os.path.join(tmp_dir, f"{i:03d}.wav")
                shutil.copy2(src, dst)

            cmd = [
                executable,
                tmp_dir,
                args['bpm'],
                args['rpb'],
                args['pattern_rows'],
                args['output_path'],
            ]

            total_samples = len(args['files'])

            process = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                bufsize=1,
                universal_newlines=True
            )

            while True:
                line = process.stdout.readline()
                if not line:
                    break

                print(line.strip())

                # Parse: "  Sample X/Y written (Z bytes)."
                if "Sample" in line and "written" in line:
                    try:
                        # "  Sample 1/10 written (1234 bytes)."
                        part = line.strip().split()[1]  # "1/10"
                        current, total = part.split("/")
                        progress = (int(current) / int(total)) * 100
                        self._schedule(self.fur_update_progress, progress,
                                       f"Sample {current}/{total} written...")
                    except (IndexError, ValueError):
                        pass

            process.wait()

            if process.returncode == 0:
                self._schedule(self.fur_finish_generate, True)
            else:
                stderr = process.stderr.read()
                self._schedule(self.fur_finish_generate, False, stderr)

        except Exception as e:
            self._schedule(self.fur_finish_generate, False, str(e))
        finally:
            if tmp_dir and os.path.isdir(tmp_dir):
                shutil.rmtree(tmp_dir, ignore_errors=True)

    def fur_update_progress(self, value, text):
        self.fur_progress_bar["value"] = value
        self.fur_progress_label.config(text=text)

    def fur_finish_generate(self, success, error_msg=""):
        self.fur_generate_button.config(state=tk.NORMAL)
        if success:
            self.fur_progress_bar["value"] = 100
            self.fur_progress_label.config(text="Finished successfully!")
            messagebox.showinfo("Success", f".fur file generated:\n{self.fur_output_path.get()}")
        else:
            self.fur_progress_label.config(text="Error occurred.")
            messagebox.showerror("Error", f"An error occurred:\n{error_msg}")


if __name__ == "__main__":
    root = tk.Tk()
    app = SlicerGUI(root)
    root.mainloop()
