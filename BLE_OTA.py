import os
import sys
import asyncio
import struct
import time
import argparse
import threading
from collections import deque
from bleak import BleakClient, BleakScanner
from bleak.exc import BleakError
from queue import Queue
import tkinter as tk
from tkinter import filedialog, messagebox

SERVICE_UUID = "4e8cbb5e-bc0f-4aab-a6e8-55e662418bef"
CHARACTERISTIC_UUID = "513fcda9-f46d-4e41-ac4f-42b768495a85"


def calculate_time_remaining(elapsed_times_deque, bytes_remaining, chunk_size):
    if len(elapsed_times_deque) == 0:
        return "Calculating..."

    average_time = sum(elapsed_times_deque) / len(elapsed_times_deque)
    estimated_time_remaining = (bytes_remaining / chunk_size) * average_time

    minutes, seconds = divmod(estimated_time_remaining, 60)
    return f"{int(minutes)} minutes and {seconds:.2f} seconds remaining"


async def send_firmware(address, file_path):
    time_deque = deque(maxlen=10)
    device = await BleakScanner.find_device_by_address(address, timeout=10.0)
    disconnected_event = asyncio.Event()

    if not device:
        print(f"Device with address {address} could not be found.")
        return

    def handle_disconnect(_: BleakClient):
        print("Device disconnected")
        disconnected_event.set()

    async def send_data(client: BleakClient, data: bytearray, response: bool):
        start_time = time.time()
        await client.write_gatt_char(CHARACTERISTIC_UUID, data, response)
        end_time = time.time()
        elapsed_time = end_time - start_time
        return elapsed_time

    try:
        async with BleakClient(device, disconnected_callback=handle_disconnect) as client:
            mtu_size = client.mtu_size
            print(f"Negotiated MTU size: {mtu_size}")
            chunk_size = mtu_size - 3  # Adjust as needed
            print(f"Using chunk size: {chunk_size} bytes")

            file_size = os.path.getsize(file_path)
            await client.write_gatt_char(CHARACTERISTIC_UUID, struct.pack("<I", file_size), response=True)
            print(f"Sent file size: {file_size} bytes")

            total_packets = (file_size + chunk_size - 1) // chunk_size
            packet_number = 0

            total_sent = 0
            total_start_time = time.time()
            initial_estimated_time_printed = False

            with open(file_path, 'rb') as f:
                while True:
                    chunk = f.read(chunk_size)
                    if not chunk:
                        break
                    packet_number += 1

                    elapsed_time = await send_data(client, chunk, response=False)
                    time_deque.append(elapsed_time)
                    total_sent += len(chunk)

                    if packet_number == 5 and not initial_estimated_time_printed:
                        average_time = sum(time_deque) / len(time_deque)
                        estimated_time_total = average_time * total_packets
                        est_minutes, est_seconds = divmod(estimated_time_total, 60)
                        print(f"Initial estimated time to complete: {int(est_minutes)} minutes and {est_seconds:.2f} seconds")
                        initial_estimated_time_printed = True

                    percentage = (total_sent / file_size) * 100
                    bytes_remaining = file_size - total_sent
                    time_remaining = calculate_time_remaining(time_deque, bytes_remaining, chunk_size)
                    print(f"Packet {packet_number}/{total_packets}: Sent {total_sent}/{file_size} bytes ({percentage:.2f}%) in {elapsed_time:.4f} seconds. {time_remaining}")

            total_end_time = time.time()
            total_elapsed_time = total_end_time - total_start_time
            elapsed_minutes, elapsed_seconds = divmod(total_elapsed_time, 60)
            print(f"\nTotal time elapsed: {int(elapsed_minutes)} minutes and {elapsed_seconds:.2f} seconds")

            if initial_estimated_time_printed:
                time_difference = total_elapsed_time - estimated_time_total
                diff_minutes, diff_seconds = divmod(abs(time_difference), 60)
                if time_difference > 0:
                    print(f"The update took {int(diff_minutes)} minutes and {diff_seconds:.2f} seconds longer than estimated.")
                else:
                    print(f"The update was completed {int(diff_minutes)} minutes and {diff_seconds:.2f} seconds faster than estimated.")

            if packet_number > 0:
                average_time_per_packet = total_elapsed_time / packet_number
                throughput_bytes_per_second = total_sent / total_elapsed_time
                throughput_megabytes_per_second = throughput_bytes_per_second / (1024 * 1024)
                print(f"Average time per packet: {average_time_per_packet:.4f} seconds")
                print(f"Average throughput: {throughput_bytes_per_second:.2f} bytes/second ({throughput_megabytes_per_second:.2f} MB/s)")

            print("All data sent, waiting for the device to disconnect...")
            await disconnected_event.wait()

    except BleakError as e:
        print(f"Bleak error occurred: {e}")
    except OSError as e:
        print(f"An OS error occurred: {e}")
    except Exception as e:
        print(f"Unexpected error: {e}")


async def send_firmware_gui(address, file_path, update_output, on_complete):
    time_deque = deque(maxlen=10)
    device = await BleakScanner.find_device_by_address(address, timeout=10.0)
    disconnected_event = asyncio.Event()

    if not device:
        message = f"Device with address {address} could not be found."
        update_output(message)
        on_complete()
        return

    def handle_disconnect(_: BleakClient):
        message = "Device disconnected"
        update_output(message, color='green')
        disconnected_event.set()

    async def send_data(client: BleakClient, data: bytearray, response: bool):
        start_time = time.time()
        await client.write_gatt_char(CHARACTERISTIC_UUID, data, response)
        end_time = time.time()
        elapsed_time = end_time - start_time
        return elapsed_time

    try:
        async with BleakClient(device, disconnected_callback=handle_disconnect) as client:
            mtu_size = client.mtu_size
            message = f"Negotiated MTU size: {mtu_size}"
            update_output(message)
            chunk_size = mtu_size - 3  # Adjust as needed
            message = f"Using chunk size: {chunk_size} bytes"
            update_output(message)

            file_size = os.path.getsize(file_path)
            await client.write_gatt_char(CHARACTERISTIC_UUID, struct.pack("<I", file_size), response=True)
            message = f"Sent file size: {file_size} bytes"
            update_output(message)

            total_packets = (file_size + chunk_size - 1) // chunk_size
            packet_number = 0

            total_sent = 0
            total_start_time = time.time()
            initial_estimated_time_printed = False

            with open(file_path, 'rb') as f:
                while True:
                    chunk = f.read(chunk_size)
                    if not chunk:
                        break
                    packet_number += 1

                    elapsed_time = await send_data(client, chunk, response=False)
                    time_deque.append(elapsed_time)
                    total_sent += len(chunk)

                    if packet_number == 5 and not initial_estimated_time_printed:
                        average_time = sum(time_deque) / len(time_deque)
                        estimated_time_total = average_time * total_packets
                        est_minutes, est_seconds = divmod(estimated_time_total, 60)
                        message = f"Initial estimated time to complete: {int(est_minutes)} minutes and {est_seconds:.2f} seconds"
                        update_output(message)
                        initial_estimated_time_printed = True

                    percentage = (total_sent / file_size) * 100
                    bytes_remaining = file_size - total_sent
                    time_remaining = calculate_time_remaining(time_deque, bytes_remaining, chunk_size)
                    message = f"Packet {packet_number}/{total_packets}: Sent {total_sent}/{file_size} bytes ({percentage:.2f}%) in {elapsed_time:.4f} seconds. {time_remaining}"
                    update_output(message)

            total_end_time = time.time()
            total_elapsed_time = total_end_time - total_start_time
            elapsed_minutes, elapsed_seconds = divmod(total_elapsed_time, 60)
            message = f"\nTotal time elapsed: {int(elapsed_minutes)} minutes and {elapsed_seconds:.2f} seconds"
            update_output(message)

            if initial_estimated_time_printed:
                time_difference = total_elapsed_time - estimated_time_total
                diff_minutes, diff_seconds = divmod(abs(time_difference), 60)
                if time_difference > 0:
                    message = f"The update took {int(diff_minutes)} minutes and {diff_seconds:.2f} seconds longer than estimated."
                else:
                    message = f"The update was completed {int(diff_minutes)} minutes and {diff_seconds:.2f} seconds faster than estimated."
                update_output(message)

            if packet_number > 0:
                average_time_per_packet = total_elapsed_time / packet_number
                throughput_bytes_per_second = total_sent / total_elapsed_time
                throughput_megabytes_per_second = throughput_bytes_per_second / (1024 * 1024)
                message = f"Average time per packet: {average_time_per_packet:.4f} seconds"
                update_output(message)
                message = f"Average throughput: {throughput_bytes_per_second:.2f} bytes/second ({throughput_megabytes_per_second:.2f} MB/s)"
                update_output(message)

            message = "All data sent, waiting for the device to disconnect..."
            update_output(message)
            await disconnected_event.wait()

    except BleakError as e:
        message = f"Bleak error occurred: {e}"
        update_output(message)
    except OSError as e:
        message = f"An OS error occurred: {e}"
        update_output(message)
    except Exception as e:
        message = f"Unexpected error: {e}"
        update_output(message)
    finally:
        # Call the on_complete callback when the upload is finished
        on_complete()


def open_gui():
    root = tk.Tk()
    root.title("BLE OTA Firmware Uploader")

    # Increase window width by 1.25x
    default_width = 600  # Default width (adjust if needed)
    default_height = 400  # Default height (adjust if needed)
    root.geometry(f"{default_width}x{default_height}")

    selected_device_address = tk.StringVar()
    selected_device_name = tk.StringVar()
    firmware_file_path = tk.StringVar()
    message_queue = Queue()

    def select_device():
        devices = []

        async def scan_devices():
            nonlocal devices
            devices = await BleakScanner.discover(timeout=1.0)  # Changed scan time to 1 second

        def run_scan():
            asyncio.run(scan_devices())
            message_queue.put(("devices", devices))

        threading.Thread(target=run_scan).start()

    def select_file():
        file_path = filedialog.askopenfilename(filetypes=[("Binary files", "*.bin")])
        if file_path:
            firmware_file_path.set(file_path)
            if selected_device_address.get() and firmware_file_path.get():
                upload_button.config(state=tk.NORMAL)

    def start_upload():
        upload_button.config(state=tk.DISABLED)
        # Clear the output text
        output_text.configure(state=tk.NORMAL)
        output_text.delete('1.0', tk.END)
        output_text.configure(state=tk.NORMAL)
        address = selected_device_address.get()
        file_path = firmware_file_path.get()

        if not os.path.exists(file_path):
            messagebox.showerror("Error", f"File not found: {file_path}")
            upload_button.config(state=tk.NORMAL)
            return

        def on_upload_complete():
            root.after(0, upload_button.config, {'state': tk.NORMAL})

        threading.Thread(target=lambda: asyncio.run(send_firmware_gui(address, file_path, update_output, on_upload_complete))).start()

    def update_output(text, color=None):
        message_queue.put((text, color))

    def process_queue():
        if not message_queue.empty():
            output_text.configure(state=tk.NORMAL)
            while not message_queue.empty():
                item = message_queue.get()
                if isinstance(item, tuple):
                    if item[0] == "devices":
                        devices = item[1]
                        filtered_devices = [device for device in devices if device.name and device.name.strip()]
                        device_selection_window = tk.Toplevel(root)
                        device_selection_window.title("Select BLE Device")

                        lb = tk.Listbox(device_selection_window, width=50)
                        lb.pack(fill=tk.BOTH, expand=True)

                        for idx, device in enumerate(filtered_devices):
                            lb.insert(tk.END, f"{device.name} [{device.address}]")

                        def on_select(event):
                            selection = lb.curselection()
                            if selection:
                                index = selection[0]
                                device = filtered_devices[index]
                                selected_device_address.set(device.address)
                                selected_device_name.set(device.name)
                                device_selection_window.destroy()
                                if selected_device_address.get() and firmware_file_path.get():
                                    upload_button.config(state=tk.NORMAL)

                        lb.bind('<<ListboxSelect>>', on_select)
                    else:
                        text, color = item
                        if color:
                            output_text.insert(tk.END, text + '\n', (color,))
                        else:
                            output_text.insert(tk.END, text + '\n')
                        output_text.see(tk.END)
                else:
                    text = item
                    output_text.insert(tk.END, text + '\n')
                    output_text.see(tk.END)
            output_text.configure(state=tk.DISABLED)
        root.after(10, process_queue)

    tk.Button(root, text="Select BLE Device", command=select_device).pack(pady=5)
    tk.Label(root, textvariable=selected_device_name).pack()

    tk.Button(root, text="Select Firmware File", command=select_file).pack(pady=5)
    tk.Label(root, textvariable=firmware_file_path).pack()

    upload_button = tk.Button(root, text="Upload Firmware", command=start_upload, state=tk.DISABLED)
    upload_button.pack(pady=5)

    # Set smaller font size for output text
    output_font = ('TkDefaultFont', 8)  # Adjust font size as needed
    output_text = tk.Text(root, height=15, font=output_font)
    output_text.pack(fill=tk.BOTH, expand=True)
    output_text.tag_configure('green', foreground='green')
    # Prevent user from editing the output_text
    def disable_edit(event):
        return "break"
    output_text.bind("<Key>", disable_edit)

    root.after(10, process_queue)
    root.mainloop()


def main():
    if len(sys.argv) == 1:
        open_gui()
    else:
        parser = argparse.ArgumentParser(description="BLE OTA Firmware Uploader")
        parser.add_argument('--address', type=str, help='BLE device address', required=True)
        parser.add_argument('--file', type=str, help='Firmware file path', required=True)

        args = parser.parse_args()

        address = args.address
        firmware_path = args.file

        if not os.path.exists(firmware_path):
            print(f"File not found: {firmware_path}")
            sys.exit(1)

        asyncio.run(send_firmware(address, firmware_path))


if __name__ == "__main__":
    main()
