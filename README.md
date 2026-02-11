# ProCX
ProCX: Terminal-based multi-process manager

# ProCX - Multi-Process Manager

## 📌 Project Overview
ProCX is a terminal-based system designed to manage, monitor, and control multiple programs simultaneously.  
It allows running programs in **attached** and **detached** modes, tracks running processes in real-time, and terminates them if necessary.  

With a **monitor thread** and **message queue**, ProCX enables users to efficiently manage all processes quickly and effectively.

---

## ⚙️ Project Features

1. **Launch New Programs**
   - Users can launch programs in either attached or detached mode.
   - **Attached mode:** Program runs linked to the terminal.
   - **Detached mode:** Program runs independently, detached from the terminal.
   - Program information is stored in **shared memory** for tracking.

2. **List Running Programs**
   - The system lists all currently running programs.
   - For each program, the following information is displayed:
     - PID (Process ID)
     - Command
     - Mode (Attached/Detached)
     - Status (Running/Terminated)
     - Start time

3. **Terminate Programs**
   - Users can terminate a program by entering its PID.
   - The system sends a **SIGTERM signal** and updates the program’s status.

---

## 🛠 Technical Details

1. **Shared Memory & Semaphores**
   - Shared Memory allows fast data sharing between multiple processes.
   - In ProCX, all process information (PID, command, status, etc.) is stored in shared memory.
   - Semaphores ensure safe access to shared resources, preventing race conditions.
   - Example: When a process accesses shared memory, other processes wait until the semaphore is released.

2. **Monitor Thread**
   - The monitor thread checks all running processes every 2 seconds.
   - It updates the status of processes and notifies other instances if needed.
   - Attached processes are actively monitored; detached processes are only tracked, not terminated.
   - Example code snippet:
   ```c
   void *monitor_thread_func(void *arg) {
       while (g_monitor_running) {
           sleep(2);
           // Check child processes with waitpid
           // Check other instances’ processes with kill(pid,0)
       }
   }

3. Message Queue Notifications

Message queues send notifications to other instances.

For example, when a process terminates, an IPC message is sent to inform other running instances.

📈 Results

ProCX provides an effective solution for multi-process management and monitoring.

Real-time process tracking using the monitor thread.

Efficient notifications via message queues.

Flexible program management in attached and detached modes.

Safe and synchronized access to shared memory with semaphores.
