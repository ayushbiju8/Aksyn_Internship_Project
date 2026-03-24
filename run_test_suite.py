import os
import time
import subprocess
import pandas as pd
import matplotlib.pyplot as plt

def replace_frame_size(file_path, new_frames):
    with open(file_path, 'r', encoding='utf-8') as f:
        lines = f.readlines()
    with open(file_path, 'w', encoding='utf-8') as f:
        for line in lines:
            if line.startswith("#define FRAMES_PER_BUFFER"):
                f.write(f"#define FRAMES_PER_BUFFER {new_frames}\n")
            else:
                f.write(line)

def run_test(ms_target):
    frames = int((ms_target / 1000.0) * 48000)
    print(f"\n=============================================")
    print(f"Running Analysis for {ms_target}ms ({frames} frames)")
    print(f"=============================================")
    
    print("Modifying header files...")
    replace_frame_size("sender_audio.cpp", frames)
    replace_frame_size("receiver_audio.cpp", frames)
    
    print("Compiling C++ executables...")
    subprocess.run(["g++", "sender_audio.cpp", "-o", "test_sender.exe", "-lws2_32", "-lportaudio"], check=True)
    subprocess.run(["g++", "receiver_audio.cpp", "-o", "test_receiver.exe", "-lws2_32", "-lportaudio"], check=True)
    
    out_dir = f"results/{ms_target}ms_test"
    os.makedirs(out_dir, exist_ok=True)
    csv_path = os.path.join(out_dir, "latency_log.csv")
    
    print(f"Starting processes and logging to {csv_path}...")
    receiver = subprocess.Popen(["test_receiver.exe", csv_path])
    time.sleep(1) # Let receiver bind socket
    sender = subprocess.Popen(["test_sender.exe"])
    
    print("Recording audio stream data for 10 seconds...")
    time.sleep(10)
    
    print("Stopping processes gracefully...")
    subprocess.run(["taskkill", "/F", "/T", "/PID", str(sender.pid)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run(["taskkill", "/F", "/T", "/PID", str(receiver.pid)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1) # Let files flush
    
    generate_graphs(csv_path, out_dir, ms_target)

def generate_graphs(csv_path, out_dir, ms_target):
    print("Generating Matplotlib graphs...")
    if not os.path.exists(csv_path):
        print(f"Error: CSV file {csv_path} not found.")
        return
        
    df = pd.read_csv(csv_path)
    if len(df) == 0:
        print("CSV is empty.")
        return
        
    # Trim the first 50 packets to avoid startup/initialization spikes in the graph
    if len(df) > 50:
        df = df.iloc[50:]
        
    # 1. Expected vs Actual Total Latency
    plt.figure(figsize=(10, 5))
    plt.plot(df['Sequence'], df['Total_ms'], label='Actual Total Latency', color='red', alpha=0.8, linewidth=1.5)
    plt.plot(df['Sequence'], df['ExpectedTotal_ms'], label='Expected Total Latency', color='blue', linestyle='--', linewidth=2)
    plt.title(f'Expected vs Actual End-to-End Latency ({ms_target}ms frames)')
    plt.xlabel('Packet Sequence')
    plt.ylabel('Latency (ms)')
    plt.legend()
    plt.grid(True)
    plt.tight_layout()
    plt.savefig(os.path.join(out_dir, "expected_vs_actual.png"), dpi=150)
    plt.close()
    
    # 2. Network Latency vs Jitter Buffer Size/Latency
    plt.figure(figsize=(10, 5))
    plt.plot(df['Sequence'], df['Network_ms'], label='Network Transit Latency', color='green', alpha=0.7)
    plt.plot(df['Sequence'], df['Jitter_ms'], label='Jitter Buffer Retention Latency', color='purple', alpha=0.7)
    plt.title(f'Component Breakdown: Network Transit vs Jitter Buffering ({ms_target}ms frames)')
    plt.xlabel('Packet Sequence')
    plt.ylabel('Latency (ms)')
    plt.legend()
    plt.grid(True)
    plt.tight_layout()
    plt.savefig(os.path.join(out_dir, "components_breakdown.png"), dpi=150)
    plt.close()
    
    print(f"Graphs successfully generated and saved in: {out_dir}")

if __name__ == "__main__":
    os.makedirs("results", exist_ok=True)
    
    # Run tests automatically for 8ms, 4ms and 2ms targets
    run_test(8) # 8ms = 384 frames
    run_test(4) # 4ms = 192 frames
    run_test(2) # 2ms = 96 frames
    
    print("\nAutomated Audio Testing Suite Complete! Check the 'results' folder.")
